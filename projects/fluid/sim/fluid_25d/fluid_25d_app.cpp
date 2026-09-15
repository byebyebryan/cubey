#include "fluid_25d_app.h"

#include "fluid_25d_commands.h"
#include "fluid_25d_gpu_resources.h"
#include "fluid_25d_oracle.h"
#include "fluid_25d_scenarios.h"

#include <cubey/engine/project_gpu_services.h>
#include <cubey/engine/project_runtime.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/immediate_commands.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {
namespace {

inline constexpr float kDepthToleranceM = 0.0005F;
inline constexpr float kFluxToleranceM3PerS = 0.002F;
inline constexpr double kLedgerToleranceM3 = 0.003;
inline constexpr float kCatchmentCameraBaseYaw = -0.52F;
inline constexpr float kCatchmentCameraBasePitch = -0.92F;

[[nodiscard]] std::uint32_t headless_frame_count(const host::CommonRunConfig& config) {
    return config.frames == 0U ? 120U : config.frames;
}

[[nodiscard]] FrameTiming fixed_headless_timing(const Fluid25DConfig& config,
                                                std::uint64_t frame_index) {
    if (frame_index == 0U) {
        throw std::runtime_error("fluid 2.5D fixed headless frame index must be positive");
    }
    return {
        .delta_seconds = config.fixed_delta_seconds,
        .elapsed_seconds =
            static_cast<double>(config.fixed_delta_seconds) * static_cast<double>(frame_index),
        .frame_index = frame_index,
    };
}

template <typename Value>
[[nodiscard]] std::vector<Value> readback_values(cubey::ProjectGpuServices& gpu,
                                                 const cubey::vulkan::Buffer& buffer,
                                                 std::size_t value_count, const char* label) {
    const VkDeviceSize expected_size = static_cast<VkDeviceSize>(value_count * sizeof(Value));
    if (buffer.size() != expected_size) {
        throw std::runtime_error(std::string("fluid 2.5D ") + label +
                                 " buffer has an unexpected byte size");
    }
    const std::vector<std::uint8_t> bytes =
        gpu.readback_buffer(buffer.handle(), buffer.size(), label);
    if (bytes.size() != static_cast<std::size_t>(expected_size)) {
        throw std::runtime_error(std::string("fluid 2.5D ") + label +
                                 " readback has an unexpected byte size");
    }
    std::vector<Value> values(value_count);
    std::memcpy(values.data(), bytes.data(), bytes.size());
    return values;
}

[[nodiscard]] bool finite(float value) {
    return std::isfinite(value);
}

class Fluid25DApp {
  public:
    explicit Fluid25DApp(Fluid25DProjectConfig config)
        : config_(std::move(config)),
          scenario_(make_fluid_25d_scenario(
              config_.simulation.scenario, config_.simulation.grid_width,
              config_.simulation.grid_height, config_.simulation.cell_size_m)),
          presentation_view_(fluid_25d_presentation_view_from_name(config_.view)),
          debug_view_(fluid_25d_debug_view_from_name(config_.debug_view)) {
        configure_catchment_camera();
        if (config_.gpu_oracle_validation) {
            oracle_.emplace(config_.simulation, scenario_);
        }
    }

    Fluid25DApp(const Fluid25DApp&) = delete;
    Fluid25DApp& operator=(const Fluid25DApp&) = delete;

    int run() {
        return config_.common.headless ? run_headless() : run_windowed();
    }

  private:
    int run_windowed() {
        cubey::host::WindowedAppCallbacks callbacks;
        callbacks.create_global_resources = [this](cubey::host::WindowedAppContext& context) {
            create_global_resources_if_needed(context.device(), context.gpu());
        };
        callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            resources_.create_render_pipelines(context.device(), context.swapchain().format(),
                                               VK_FORMAT_D32_SFLOAT, context.swapchain().extent());
            graph_executor_.clear();
            graph_executor_.resize(context.frame_slot_count());
        };
        callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext&) {
            graph_executor_.clear();
            resources_.destroy_swapchain_resources();
        };
        callbacks.update = [this](cubey::host::WindowedAppContext& context,
                                  const FrameTiming& timing) {
            const auto input = context.filtered_input();
            orbit_controller_.update_pointer_input(input, timing.delta_seconds);
            if (input.key_pressed(cubey::input::Key::Space)) {
                paused_ = !paused_;
            }
            if (input.key_pressed(cubey::input::Key::R)) {
                reset_requested_ = true;
            }
            if (input.key_pressed(cubey::input::Key::D)) {
                debug_view_ = static_cast<Fluid25DDebugView>(
                    (static_cast<std::uint32_t>(debug_view_) + 1U) % 6U);
            }
            if (input.key_pressed(cubey::input::Key::A)) {
                presentation_view_ = presentation_view_ == Fluid25DPresentationView::Catchment
                                         ? Fluid25DPresentationView::Diagnostics
                                         : Fluid25DPresentationView::Catchment;
            }
        };
        callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& frame) {
            record_windowed_frame(context, frame);
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext&) {
            graph_executor_.clear();
            resources_.destroy_all_resources();
            runtime_.detach_gpu_if_attached();
        };

        return cubey::host::run_windowed_app(
            {
                .run_config = config_.common,
                .app_name = "fluid_25d",
                .ready_status = "rendering River V0 terrain-water catchment",
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
                .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .require_dynamic_rendering = true,
                .close_on_escape = true,
            },
            std::move(callbacks));
    }

    void create_global_resources_if_needed(cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu) {
        runtime_.attach_gpu_if_needed(gpu);
        resources_.create_global_resources_if_needed(device, runtime_.gpu(), config_.simulation,
                                                     scenario_);
    }

    void record_windowed_frame(cubey::host::WindowedAppContext& context,
                               const cubey::host::WindowedRenderFrame& render_frame) {
        static_cast<void>(runtime_.frame_for_timing(render_frame.timing));
        const cubey::render::CompiledRenderGraph graph = build_fluid_25d_frame_graph(
            render_frame.color_target, resources_, config_.simulation, presentation_view_,
            debug_view_, render_camera(render_frame.color_target.extent),
            Fluid25DRenderTargetMode::Present, true, paused_, reset_requested_);
        const cubey::vulkan::CommandRecorder recorder(render_frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        graph_executor_.record(
            cubey::render::RenderGraphFrameRecordInfo{
                .device = &context.device(),
                .command_buffer = render_frame.command_buffer,
                .frame_slot = render_frame.frame_slot,
                .label = "vkEndCommandBuffer fluid_25d",
                .command_buffer_mode =
                    cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
            },
            graph);
        recorder.end("vkEndCommandBuffer fluid_25d");
    }

    void configure_catchment_camera() {
        const auto [terrain_minimum, terrain_maximum] = std::minmax_element(
            scenario_.terrain_height_m.begin(), scenario_.terrain_height_m.end());
        const float world_width =
            static_cast<float>(config_.simulation.grid_width - 1U) * config_.simulation.cell_size_m;
        const float world_height = static_cast<float>(config_.simulation.grid_height - 1U) *
                                   config_.simulation.cell_size_m;
        const float horizontal_extent = std::max(world_width, world_height);
        const float scaled_terrain_span =
            (*terrain_maximum - *terrain_minimum) * kFluid25DCatchmentHeightScale;
        catchment_target_ = {
            0.0F,
            (*terrain_minimum + *terrain_maximum) * 0.5F * kFluid25DCatchmentHeightScale,
            0.0F,
        };
        const float camera_distance =
            std::max(horizontal_extent * 1.05F, scaled_terrain_span * 6.0F + 16.0F);
        orbit_controller_.set_distance_limits(std::max(8.0F, horizontal_extent * 0.30F),
                                              std::max(48.0F, horizontal_extent * 4.0F));
        orbit_controller_.set_pitch_limits(-0.38F, 0.38F);
        orbit_controller_.set_home_distance(camera_distance);
        camera_.set_projection(std::numbers::pi_v<float> / 3.0F, 0.1F,
                               camera_distance * 5.0F + scaled_terrain_span + 64.0F);
    }

    [[nodiscard]] cubey::Transform3D render_camera_transform() const {
        return cubey::orbit_camera_transform({
            .target = catchment_target_,
            .distance = orbit_controller_.distance(),
            .yaw = kCatchmentCameraBaseYaw + orbit_controller_.yaw(),
            .pitch = kCatchmentCameraBasePitch + orbit_controller_.pitch(),
        });
    }

    [[nodiscard]] Fluid25DRenderCamera render_camera(VkExtent2D extent) const {
        const cubey::Transform3D transform = render_camera_transform();
        const float aspect = extent.height == 0U ? 1.0F
                                                 : static_cast<float>(extent.width) /
                                                       static_cast<float>(extent.height);
        return {
            .view_projection = camera_.view_projection_matrix(transform, aspect),
            .position = transform.translation,
        };
    }

    void record_headless_simulation_frame(cubey::ProjectGpuServices& gpu,
                                          const cubey::host::HeadlessCaptureFrame& frame) {
        static_cast<void>(runtime_.frame_for_timing(frame.timing));
        static_cast<void>(gpu.submit_and_wait({
            .label = "fluid_25d headless simulation frame",
            .work =
                [this](cubey::vulkan::GpuOwnerContext& gpu_context) {
                    cubey::vulkan::ImmediateCommands commands(gpu_context);
                    record_fluid_25d_compute(commands.command_buffer(), resources_,
                                             config_.simulation, false, reset_requested_, false);
                    commands.submit_and_wait();
                },
        }));
        if (oracle_.has_value()) {
            const Fluid25DStepLedger step_ledger = oracle_->step();
            expected_source_volume_m3_ += step_ledger.source_volume_m3;
            expected_sink_volume_m3_ += step_ledger.sink_volume_m3;
        }
    }

    void validate_gpu_oracle(cubey::ProjectGpuServices& gpu) {
        if (!oracle_.has_value()) {
            return;
        }
        const std::size_t cells = fluid_25d_cell_count(config_.simulation);
        const cubey::vulkan::Buffer& current_depth =
            resources_.current_depth_is_a() ? resources_.depth_a() : resources_.depth_b();
        const std::vector<float> actual_depth =
            readback_values<float>(gpu, current_depth, cells, "oracle depth");
        const std::vector<Fluid25DFluxGpu> actual_flux =
            readback_values<Fluid25DFluxGpu>(gpu, resources_.flux(), cells, "oracle flux");
        const std::vector<Fluid25DLedgerGpu> actual_ledger =
            readback_values<Fluid25DLedgerGpu>(gpu, resources_.ledger(), cells, "oracle ledger");
        const std::vector<Fluid25DVelocityGpu> actual_velocity =
            readback_values<Fluid25DVelocityGpu>(gpu, resources_.velocity(), cells,
                                                 "oracle velocity");

        float maximum_depth_error = 0.0F;
        float maximum_flux_error = 0.0F;
        double actual_source_volume_m3 = 0.0;
        double actual_sink_volume_m3 = 0.0;
        for (std::size_t index = 0; index < cells; ++index) {
            if (!finite(actual_depth[index]) || actual_depth[index] < 0.0F) {
                throw std::runtime_error(
                    "fluid 2.5D GPU oracle observed nonfinite or negative depth");
            }
            maximum_depth_error =
                std::max(maximum_depth_error,
                         std::abs(actual_depth[index] - oracle_->water_depth_m()[index]));
            for (std::size_t face = 0; face < 4U; ++face) {
                const float actual = actual_flux[index].faces_m3_per_s[face];
                if (!finite(actual) || actual < 0.0F) {
                    throw std::runtime_error("fluid 2.5D GPU oracle observed invalid face flux");
                }
                maximum_flux_error =
                    std::max(maximum_flux_error,
                             std::abs(actual - oracle_->outgoing_flux_m3_per_s()[index][face]));
            }
            for (const float component : actual_velocity[index].velocity_wet) {
                if (!finite(component)) {
                    throw std::runtime_error(
                        "fluid 2.5D GPU oracle observed nonfinite velocity state");
                }
            }
            actual_source_volume_m3 += actual_ledger[index].source_sink_m3[0];
            actual_sink_volume_m3 += actual_ledger[index].source_sink_m3[1];
        }
        const double source_ledger_error =
            std::abs(actual_source_volume_m3 - expected_source_volume_m3_);
        const double sink_ledger_error = std::abs(actual_sink_volume_m3 - expected_sink_volume_m3_);
        if (maximum_depth_error > kDepthToleranceM || maximum_flux_error > kFluxToleranceM3PerS ||
            source_ledger_error > kLedgerToleranceM3 || sink_ledger_error > kLedgerToleranceM3) {
            throw std::runtime_error(
                "fluid 2.5D GPU oracle mismatch: max_depth=" + std::to_string(maximum_depth_error) +
                " max_flux=" + std::to_string(maximum_flux_error) +
                " source_ledger=" + std::to_string(source_ledger_error) +
                " sink_ledger=" + std::to_string(sink_ledger_error));
        }
        std::printf("fluid_25d_gpu_oracle: PASS scenario=%s max_depth=%.7f max_flux=%.7f "
                    "source_ledger=%.7f sink_ledger=%.7f\n",
                    fluid_25d_scenario_name(config_.simulation.scenario), maximum_depth_error,
                    maximum_flux_error, source_ledger_error, sink_ledger_error);
    }

    int run_headless() {
        if (!config_.gpu_oracle_validation) {
            std::printf("fluid_25d: GPU oracle validation disabled; simulation state remains "
                        "GPU-resident\n");
        }
        cubey::host::HeadlessPngHostConfig host_config;
        host_config.run_config = config_.common;
        host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;

        cubey::host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
            create_global_resources_if_needed(context.device(), context.gpu());
            const cubey::host::HeadlessRenderTarget& target = context.render_target();
            resources_.create_render_pipelines(context.device(), target.format,
                                               VK_FORMAT_D32_SFLOAT, target.extent);
            graph_executor_.clear();
            graph_executor_.resize(1U);
        };
        cubey::host::install_headless_simulation_driver(
            callbacks, config_.common,
            {
                .png_frame_count = headless_frame_count(config_.common),
                .png_timing =
                    [this](std::uint64_t frame_index) {
                        return fixed_headless_timing(config_.simulation, frame_index);
                    },
                .simulate_frame =
                    [this](cubey::host::HeadlessPngContext& context,
                           const cubey::host::HeadlessCaptureFrame& frame) {
                        record_headless_simulation_frame(runtime_.gpu(), frame);
                        (void)context;
                    },
            });
        callbacks.record_capture = [this](cubey::host::HeadlessPngContext& context,
                                          VkCommandBuffer command_buffer,
                                          const cubey::host::HeadlessRenderTarget& target) {
            validate_gpu_oracle(runtime_.gpu());
            const cubey::render::CompiledRenderGraph graph = build_fluid_25d_frame_graph(
                target, resources_, config_.simulation, presentation_view_, debug_view_,
                render_camera(target.extent), Fluid25DRenderTargetMode::ColorAttachment, false,
                false, reset_requested_);
            graph_executor_.record(
                {
                    .device = &context.device(),
                    .command_buffer = command_buffer,
                    .frame_slot = {.index = 0U, .count = 1U},
                    .label = "vkEndCommandBuffer fluid_25d headless graph",
                    .command_buffer_mode =
                        cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
                },
                graph);
        };
        callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) {
            graph_executor_.clear();
            resources_.destroy_all_resources();
            runtime_.detach_gpu_if_attached();
        };
        cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
        return host.run();
    }

    Fluid25DProjectConfig config_;
    Fluid25DScenarioData scenario_;
    cubey::ProjectRuntimeAdapter runtime_{1};
    Fluid25DGpuResources resources_;
    cubey::render::RenderGraphFrameExecutor graph_executor_;
    cubey::Camera3D camera_;
    cubey::OrbitController orbit_controller_;
    cubey::math::Vec3 catchment_target_{0.0F, 0.0F, 0.0F};
    Fluid25DPresentationView presentation_view_ = Fluid25DPresentationView::Catchment;
    Fluid25DDebugView debug_view_ = Fluid25DDebugView::Terrain;
    std::optional<Fluid25DOracle> oracle_;
    double expected_source_volume_m3_ = 0.0;
    double expected_sink_volume_m3_ = 0.0;
    bool paused_ = false;
    bool reset_requested_ = false;
};

} // namespace

int run_fluid_25d(const Fluid25DProjectConfig& config) {
    Fluid25DApp app(config);
    return app.run();
}

} // namespace cubey::projects::fluid::fluid_25d
