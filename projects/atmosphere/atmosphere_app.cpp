#include "atmosphere_app.h"

#include "atmosphere_config.h"
#include "atmosphere_environment.h"
#include "atmosphere_ui.h"

#include <cubey/core/jobs.h>
#include <cubey/core/math.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/input.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/atmosphere_background_frame.h>
#include <cubey/render/atmosphere_lunar_atlas.h>
#include <cubey/render/atmosphere_night_sky_atlas.h>
#include <cubey/render/hdr_post_frame.h>
#include <cubey/render/pbr.h>
#include <cubey/render/render_graph.h>
#include <cubey/render/target.h>
#include <cubey/render/texture.h>
#include <cubey/render/view_ray_basis_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/sampler.h>
#include <cubey/vulkan/vk_check.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <numbers>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef CUBEY_ATMOSPHERE_SHADER_DIR
#error "CUBEY_ATMOSPHERE_SHADER_DIR must be defined by the atmosphere CMake target"
#endif

namespace cubey::projects::atmosphere {
namespace {

using cubey::FrameTiming;
using cubey::host::FrameStatsSample;
using cubey::host::FrameStatsSnapshot;
using cubey::render::generate_lunar_atlas;
using cubey::render::generate_night_sky_atlas;
using cubey::render::LunarAtlas;
using cubey::render::NightSkyAtlas;
using cubey::render::NightSkyAtlasConfig;

constexpr float kBaseYaw = 0.0F;
constexpr float kBasePitch = 0.0F;
constexpr float kDefaultFovyRadians = 65.0F * (std::numbers::pi_v<float> / 180.0F);
constexpr VkFormat kAtmosphereSceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

struct ResolvedNightSkyAtlas {
    float procedural_variation = 0.0F;
    NightSkyLayerView layer = NightSkyLayerView::Final;
};

struct GeneratedNightSkyAtlas {
    ResolvedNightSkyAtlas resolved{};
    NightSkyAtlas atlas{};
};

struct PendingNightSkyAtlasJob {
    ResolvedNightSkyAtlas resolved{};
    cubey::jobs::JobHandle<GeneratedNightSkyAtlas> job;
};

struct CompiledAtmosphereGraph {
    cubey::render::CompiledRenderGraph graph{};
    cubey::render::RenderGraphTextureHandle scene_color{};
};

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_ATMOSPHERE_SHADER_DIR) / filename;
}

[[nodiscard]] ResolvedNightSkyAtlas resolve_night_sky_atlas(const AtmosphereConfig& config) {
    return {
        .procedural_variation = config.night_sky.procedural_variation,
        .layer = config.night_sky.layer,
    };
}

[[nodiscard]] bool same_night_sky_atlas(const ResolvedNightSkyAtlas& lhs,
                                        const ResolvedNightSkyAtlas& rhs) {
    return lhs.procedural_variation == rhs.procedural_variation && lhs.layer == rhs.layer;
}

[[nodiscard]] NightSkyAtlasConfig night_sky_atlas_config(const ResolvedNightSkyAtlas& resolved) {
    return {
        .procedural_variation = resolved.procedural_variation,
        .layer = resolved.layer,
    };
}

[[nodiscard]] GeneratedNightSkyAtlas
generate_resolved_night_sky_atlas(ResolvedNightSkyAtlas resolved) {
    return {
        .resolved = resolved,
        .atlas = generate_night_sky_atlas(night_sky_atlas_config(resolved)),
    };
}

class AtmosphereApp {
  public:
    explicit AtmosphereApp(RunConfig config)
        : run_config_(std::move(config)),
          atmosphere_config_(atmosphere_config_from_run_config(run_config_)),
          render_view_(atmosphere_config_.render_view),
          headless_base_time_hours_(atmosphere_config_.time_of_day.time_hours),
          headless_base_day_of_year_(atmosphere_config_.time_of_day.day_of_year) {
        view_controller_.set_auto_rotation_speed(0.0F);
    }

    AtmosphereApp(const AtmosphereApp&) = delete;
    AtmosphereApp& operator=(const AtmosphereApp&) = delete;

    ~AtmosphereApp() {
        destroy_swapchain_resources();
        destroy_global_resources();
    }

    int run() {
        if (run_config_.headless) {
            return run_headless();
        }
        return run_windowed();
    }

  private:
    int run_windowed() {
        start_windowed_atlas_jobs();

        cubey::host::WindowedAppCallbacks callbacks;
        callbacks.create_global_resources = [this](cubey::host::WindowedAppContext& context) {
            create_windowed_global_resources(context.device(), context.gpu(),
                                             context.frame_slot_count());
        };
        callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            create_pipeline(context.device(), context.swapchain().format(),
                            context.swapchain().extent());
        };
        callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext&) {
            destroy_swapchain_resources();
        };
        callbacks.update = [this](cubey::host::WindowedAppContext& context,
                                  const FrameTiming& timing) { update_windowed(context, timing); };
        callbacks.draw_ui = [this](cubey::host::WindowedAppContext&) {
            refresh_loading_status();
            draw_atmosphere_ui({
                .config = atmosphere_config_,
                .latest_frame_stats = latest_frame_stats_,
                .render_view = render_view_,
                .reset_requested = reset_requested_,
                .loading_status = loading_status_,
                .latest_fps = latest_fps_,
                .latest_frame_ms = latest_frame_ms_,
            });
        };
        callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& frame) {
            record_windowed_frame(context.device(), frame);
        };
        callbacks.frame_stats_sample =
            [this](cubey::host::WindowedAppContext& context,
                   const FrameTiming& timing) -> std::optional<FrameStatsSample> {
            return record_frame_stats(context.swapchain().extent(), timing);
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext&) {
            destroy_swapchain_resources();
            destroy_global_resources();
        };

        return cubey::host::run_windowed_app(
            {
                .run_config = run_config_,
                .app_name = "atmosphere",
                .ready_status = "rendering atmosphere project",
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT,
                .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .require_dynamic_rendering = true,
                .close_on_escape = true,
            },
            std::move(callbacks));
    }

    int run_headless() {
        cubey::host::HeadlessPngHostConfig host_config;
        host_config.run_config = run_config_;
        host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT;
        host_config.output_format = VK_FORMAT_R8G8B8A8_UNORM;
        host_config.require_dynamic_rendering = true;

        cubey::host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
            const cubey::host::HeadlessRenderTarget& target = context.render_target();
            create_gpu_resources(context.device(), context.gpu(), target.format, target.extent,
                                 cubey::host::headless_capture_frame_slot_count(context.config()));
        };
        callbacks.record_frame = [this](cubey::host::HeadlessPngContext& context,
                                        const cubey::host::HeadlessCaptureFrame& frame,
                                        VkCommandBuffer command_buffer,
                                        const cubey::host::HeadlessRenderTarget& target) {
            update_headless_time(frame);
            record_atmosphere_target(context.device(), command_buffer, target, frame.frame_slot);
        };
        callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) {
            destroy_swapchain_resources();
            destroy_global_resources();
        };

        cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
        return host.run();
    }

    void update_windowed(cubey::host::WindowedAppContext& context, const FrameTiming& timing) {
        const auto input = context.filtered_input();
        if (input.key_pressed(cubey::input::Key::R)) {
            reset_requested_ = true;
        }
        if (input.key_pressed(cubey::input::Key::D)) {
            render_view_ = next_atmosphere_render_view(render_view_);
        }
        if (input.key_pressed(cubey::input::Key::Space) &&
            atmosphere_config_.time_of_day.mode == SunControlMode::SolarClock) {
            atmosphere_config_.time_of_day.playing = !atmosphere_config_.time_of_day.playing;
        }
        view_controller_.update_pointer_input(input, timing.delta_seconds);
        if (reset_requested_) {
            const AtmospherePreset preset = atmosphere_config_.preset;
            atmosphere_config_ = atmosphere_config_for_preset(preset);
            headless_base_time_hours_ = atmosphere_config_.time_of_day.time_hours;
            headless_base_day_of_year_ = atmosphere_config_.time_of_day.day_of_year;
            render_view_ = atmosphere_config_.render_view;
            view_controller_.reset();
            reset_requested_ = false;
        }
        advance_atmosphere_time_of_day(atmosphere_config_, timing.delta_seconds);
        resolve_atmosphere_time_of_day(atmosphere_config_);
        atmosphere_config_.render_view = render_view_;
        validate_atmosphere_config(atmosphere_config_);
        if (windowed_update_count_ > 0U) {
            refresh_async_atlases_if_needed(context.device(), context.gpu());
        }
        ++windowed_update_count_;
        refresh_loading_status();
    }

    void update_headless_time(const cubey::host::HeadlessCaptureFrame& frame) {
        if (run_config_.capture_mode == CaptureMode::Video &&
            atmosphere_config_.time_of_day.mode == SunControlMode::SolarClock &&
            atmosphere_config_.time_of_day.playing) {
            set_atmosphere_time_from_elapsed(atmosphere_config_.time_of_day,
                                             headless_base_time_hours_, headless_base_day_of_year_,
                                             frame.timing.elapsed_seconds);
        }
        resolve_atmosphere_time_of_day(atmosphere_config_);
        validate_atmosphere_config(atmosphere_config_);
    }

    std::optional<FrameStatsSample> record_frame_stats(VkExtent2D extent,
                                                       const FrameTiming& timing) {
        latest_frame_ms_ = timing.delta_seconds * 1000.0;
        latest_fps_ = timing.delta_seconds > 0.0 ? 1.0 / timing.delta_seconds : 0.0;

        const FrameStatsSample sample{
            .delta_seconds = timing.delta_seconds,
            .width = extent.width,
            .height = extent.height,
            .triangles = 1,
        };
        if (std::optional<FrameStatsSnapshot> stats = ui_frame_stats_.record_frame(sample);
            stats.has_value()) {
            latest_frame_stats_ = stats.value();
        }
        return sample;
    }

    void create_gpu_resources(cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
                              VkFormat color_format, VkExtent2D extent,
                              std::uint32_t frame_slot_count) {
        create_synchronous_atlas_resources(device, gpu, frame_slot_count);
        create_pipeline(device, color_format, extent);
    }

    void create_windowed_global_resources(cubey::vulkan::Device& device,
                                          cubey::vulkan::GpuRuntime& gpu,
                                          std::uint32_t frame_slot_count) {
        atmosphere_atlases_.emplace(
            cubey::render::create_atmosphere_background_placeholder_textures(device, gpu));
        lunar_atlas_ready_ = false;
        current_night_sky_atlas_.reset();
        create_atmosphere_descriptors(device, frame_slot_count);
        update_atmosphere_descriptor_bindings(device);
        refresh_loading_status();
    }

    void create_synchronous_atlas_resources(cubey::vulkan::Device& device,
                                            cubey::vulkan::GpuRuntime& gpu,
                                            std::uint32_t frame_slot_count) {
        const ResolvedNightSkyAtlas resolved = resolve_night_sky_atlas(atmosphere_config_);
        const GeneratedNightSkyAtlas generated = generate_resolved_night_sky_atlas(resolved);
        atmosphere_atlases_.emplace(cubey::render::create_atmosphere_background_atlas_resources(
            device, gpu, generate_lunar_atlas(), generated.atlas));
        lunar_atlas_ready_ = true;
        lunar_atlas_error_.clear();
        current_night_sky_atlas_ = generated.resolved;
        night_sky_atlas_error_.clear();

        create_atmosphere_descriptors(device, frame_slot_count);
        update_atmosphere_descriptor_bindings(device);
        refresh_loading_status();
    }

    void upload_lunar_atlas(cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
                            const LunarAtlas& atlas) {
        if (!atmosphere_atlases_.has_value()) {
            throw std::runtime_error("atmosphere atlas resources are not initialized");
        }

        atmosphere_atlases_->lunar =
            cubey::render::create_atmosphere_lunar_atlas_texture(device, gpu, atlas);
        atmosphere_atlases_->lunar_placeholder = false;
        lunar_atlas_ready_ = true;
        lunar_atlas_error_.clear();
    }

    void create_atmosphere_descriptors(cubey::vulkan::Device& device,
                                       std::uint32_t frame_slot_count) {
        atmosphere_background_.create_materials(device, {
                                                            .frame_slot_count = frame_slot_count,
                                                            .textures = atmosphere_textures(),
                                                        });
        hdr_post_frame_.create_materials(device, {
                                                     .frame_slot_count = frame_slot_count,
                                                 });
        graph_executor_.resize(frame_slot_count);
    }

    void update_atmosphere_descriptor_bindings(cubey::vulkan::Device& device) const {
        atmosphere_background_.update_texture_bindings(device, atmosphere_textures());
    }

    void upload_night_sky_atlas(cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
                                const GeneratedNightSkyAtlas& generated) {
        if (!atmosphere_atlases_.has_value()) {
            throw std::runtime_error("atmosphere atlas resources are not initialized");
        }

        atmosphere_atlases_->night_sky =
            cubey::render::create_atmosphere_night_sky_atlas_texture(device, gpu, generated.atlas);
        atmosphere_atlases_->night_sky_placeholder = false;
        current_night_sky_atlas_ = generated.resolved;
        night_sky_atlas_error_.clear();
    }

    void start_windowed_atlas_jobs() {
        if (atlas_shutdown_requested_) {
            return;
        }
        if (!pending_lunar_atlas_.has_value() && !lunar_atlas_ready_) {
            pending_lunar_atlas_.emplace(atlas_jobs_.submit([] { return generate_lunar_atlas(); }));
        }
        request_night_sky_atlas_if_needed(desired_windowed_night_sky_atlas());
        refresh_loading_status();
    }

    [[nodiscard]] ResolvedNightSkyAtlas desired_windowed_night_sky_atlas() const {
        return resolve_night_sky_atlas(atmosphere_config_);
    }

    void request_night_sky_atlas_if_needed(const ResolvedNightSkyAtlas& resolved) {
        if (atlas_shutdown_requested_) {
            return;
        }
        if (current_night_sky_atlas_.has_value() &&
            same_night_sky_atlas(current_night_sky_atlas_.value(), resolved)) {
            return;
        }
        if (pending_night_sky_atlas_.has_value()) {
            return;
        }
        pending_night_sky_atlas_.emplace(PendingNightSkyAtlasJob{
            .resolved = resolved,
            .job = atlas_jobs_.submit(
                [resolved] { return generate_resolved_night_sky_atlas(resolved); }),
        });
    }

    void refresh_async_atlases_if_needed(cubey::vulkan::Device& device,
                                         cubey::vulkan::GpuRuntime& gpu) {
        if (atlas_shutdown_requested_) {
            return;
        }
        if (!atmosphere_background_.materials_created()) {
            return;
        }
        poll_lunar_atlas_job(device, gpu);
        poll_night_sky_atlas_job(device, gpu);
        const ResolvedNightSkyAtlas desired = desired_windowed_night_sky_atlas();
        request_night_sky_atlas_if_needed(desired);
    }

    void poll_lunar_atlas_job(cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu) {
        if (!pending_lunar_atlas_.has_value() || !pending_lunar_atlas_->ready()) {
            return;
        }
        try {
            const LunarAtlas atlas = pending_lunar_atlas_->get();
            pending_lunar_atlas_.reset();
            wait_for_idle_before_descriptor_update(device);
            upload_lunar_atlas(device, gpu, atlas);
            update_atmosphere_descriptor_bindings(device);
        } catch (const std::exception& error) {
            pending_lunar_atlas_.reset();
            lunar_atlas_error_ = error.what();
        }
    }

    void poll_night_sky_atlas_job(cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu) {
        if (!pending_night_sky_atlas_.has_value() || !pending_night_sky_atlas_->job.ready()) {
            return;
        }

        try {
            GeneratedNightSkyAtlas generated = pending_night_sky_atlas_->job.get();
            pending_night_sky_atlas_.reset();
            const ResolvedNightSkyAtlas desired = desired_windowed_night_sky_atlas();
            if (!same_night_sky_atlas(generated.resolved, desired)) {
                return;
            }
            wait_for_idle_before_descriptor_update(device);
            upload_night_sky_atlas(device, gpu, generated);
            update_atmosphere_descriptor_bindings(device);
        } catch (const std::exception& error) {
            pending_night_sky_atlas_.reset();
            night_sky_atlas_error_ = error.what();
        }
    }

    void wait_for_idle_before_descriptor_update(cubey::vulkan::Device& device) const {
        cubey::vulkan::check(vkDeviceWaitIdle(device.handle()),
                             "vkDeviceWaitIdle before atmosphere atlas descriptor update");
    }

    void refresh_loading_status() {
        loading_status_.moon_pending = pending_lunar_atlas_.has_value();
        loading_status_.night_sky_pending = pending_night_sky_atlas_.has_value();
        loading_status_.moon_placeholder = !lunar_atlas_ready_;
        loading_status_.night_sky_placeholder = !current_night_sky_atlas_.has_value();
        loading_status_.moon_error = lunar_atlas_error_;
        loading_status_.night_sky_error = night_sky_atlas_error_;
    }

    void create_pipeline(cubey::vulkan::Device& device, VkFormat color_format, VkExtent2D extent) {
        const std::array<cubey::render::ShaderStageFile, 2> atmosphere_shader_stage_files{
            cubey::render::ShaderStageFile{
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .path = shader_path("atmosphere.vert.spv"),
            },
            cubey::render::ShaderStageFile{
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .path = shader_path("atmosphere.frag.spv"),
            },
        };
        atmosphere_background_.create_pipeline(
            device, {
                        .extent = extent,
                        .color_format = kAtmosphereSceneColorFormat,
                        .shader_stage_files = atmosphere_shader_stage_files,
                    });

        const std::array<cubey::render::ShaderStageFile, 2> post_shader_stage_files{
            cubey::render::ShaderStageFile{
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .path = shader_path("forward_pbr_post.vert.spv"),
            },
            cubey::render::ShaderStageFile{
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .path = shader_path("forward_pbr_post.frag.spv"),
            },
        };
        hdr_post_frame_.create_pipeline(device, {
                                                    .extent = extent,
                                                    .color_format = color_format,
                                                    .shader_stage_files = post_shader_stage_files,
                                                });
        pipeline_color_format_ = color_format;
    }

    void destroy_swapchain_resources() {
        const std::uint32_t frame_slot_count = graph_executor_.frame_slot_count();
        graph_executor_.clear();
        if (frame_slot_count != 0U) {
            graph_executor_.resize(frame_slot_count);
        }
        hdr_post_frame_.destroy_pipeline();
        atmosphere_background_.destroy_pipeline();
        pipeline_color_format_ = VK_FORMAT_UNDEFINED;
    }

    void destroy_global_resources() {
        shutdown_atlas_jobs();
        graph_executor_.clear();
        hdr_post_frame_.destroy();
        atmosphere_background_.destroy();
        atmosphere_atlases_.reset();
        current_night_sky_atlas_.reset();
        lunar_atlas_ready_ = false;
        refresh_loading_status();
    }

    void shutdown_atlas_jobs() {
        atlas_shutdown_requested_ = true;
        atlas_jobs_.shutdown();
        pending_lunar_atlas_.reset();
        pending_night_sky_atlas_.reset();
    }

    [[nodiscard]] AtmosphereFrameUniforms frame_uniforms(VkExtent2D extent) const {
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const cubey::math::Quat rotation =
            cubey::math::angle_axis_quat(kBaseYaw + view_controller_.yaw(), {0.0F, 1.0F, 0.0F}) *
            cubey::math::angle_axis_quat(kBasePitch + view_controller_.pitch(), {1.0F, 0.0F, 0.0F});
        const cubey::render::ViewRayBasis3D view_rays =
            cubey::render::view_ray_basis_3d(rotation, aspect, kDefaultFovyRadians);
        return atmosphere_frame_uniforms(atmosphere_config_, {
                                                                 .view_rays = view_rays,
                                                                 .render_view = render_view_,
                                                             });
    }

    [[nodiscard]] cubey::render::PbrPostUniforms post_uniforms() const {
        return cubey::render::hdr_post_uniforms(pipeline_color_format_,
                                                atmosphere_config_.exposure);
    }

    void record_atmosphere_scene_pass(const cubey::vulkan::CommandRecorder& recorder,
                                      const cubey::render::ColorTargetView& target,
                                      cubey::render::FrameSlot frame_slot) const {
        atmosphere_background_.upload(frame_slot, frame_uniforms(target.extent));
        atmosphere_background_.record_pass(recorder, target, frame_slot);
    }

    void record_atmosphere_post_pass(const cubey::vulkan::CommandRecorder& recorder,
                                     const cubey::render::ColorTargetView& target,
                                     cubey::render::FrameSlot frame_slot) const {
        hdr_post_frame_.record_pass(recorder, target, frame_slot);
    }

    [[nodiscard]] CompiledAtmosphereGraph
    current_render_graph(cubey::render::ColorTargetView target, cubey::render::FrameSlot frame_slot,
                         cubey::render::RenderGraphTextureState target_initial_state,
                         cubey::render::RenderGraphTextureState target_final_state) const {
        cubey::render::RenderGraphBuilder graph;
        const cubey::render::RenderGraphTextureHandle backbuffer = graph.import_color_target(
            "backbuffer", target, target_initial_state, target_final_state);
        const cubey::render::RenderGraphTextureHandle scene_color =
            graph.create_texture(cubey::render::hdr_scene_color_texture_desc(
                "atmosphere scene color", target.extent, kAtmosphereSceneColorFormat));

        graph.add_pass("atmosphere sky", cubey::render::RenderGraphQueueDomain::Graphics)
            .write_color(scene_color)
            .material_pass(cubey::render::atmosphere_background_pass_info())
            .execute([this, scene_color,
                      frame_slot](const cubey::render::RenderGraphExecutionContext& context) {
                record_atmosphere_scene_pass(
                    context.recorder(),
                    cubey::render::resolved_color_target_view(context, scene_color), frame_slot);
            });
        graph.add_pass("atmosphere post", cubey::render::RenderGraphQueueDomain::Graphics)
            .read_texture(scene_color)
            .write_color(backbuffer)
            .material_pass(cubey::render::pbr_post_pass_info())
            .execute([this, target,
                      frame_slot](const cubey::render::RenderGraphExecutionContext& context) {
                record_atmosphere_post_pass(context.recorder(), target, frame_slot);
            });

        return {
            .graph = graph.compile(),
            .scene_color = scene_color,
        };
    }

    void update_post_descriptor(const cubey::vulkan::Device& device,
                                cubey::render::FrameSlot frame_slot,
                                const cubey::render::CompiledRenderGraph& graph,
                                const cubey::render::RenderGraphResourceSet& resources,
                                cubey::render::RenderGraphTextureHandle scene_color) const {
        hdr_post_frame_.update_scene_color_descriptor(device, frame_slot, graph, resources,
                                                      scene_color);
    }

    void record_atmosphere_graph(cubey::vulkan::Device& device, VkCommandBuffer command_buffer,
                                 cubey::render::ColorTargetView target,
                                 cubey::render::FrameSlot frame_slot,
                                 cubey::render::RenderGraphTextureState target_initial_state,
                                 cubey::render::RenderGraphTextureState target_final_state,
                                 cubey::render::RenderGraphCommandBufferMode command_buffer_mode) {
        hdr_post_frame_.upload(frame_slot, post_uniforms());
        const CompiledAtmosphereGraph render_graph =
            current_render_graph(target, frame_slot, target_initial_state, target_final_state);
        graph_executor_.record(
            cubey::render::RenderGraphFrameRecordInfo{
                .device = &device,
                .command_buffer = command_buffer,
                .frame_slot = frame_slot,
                .label = "vkEndCommandBuffer atmosphere",
                .command_buffer_mode = command_buffer_mode,
            },
            render_graph.graph,
            [this, &device, frame_slot,
             &render_graph](const cubey::render::RenderGraphResourceSet& resources) {
                update_post_descriptor(device, frame_slot, render_graph.graph, resources,
                                       render_graph.scene_color);
            });
    }

    void record_windowed_frame(cubey::vulkan::Device& device,
                               const cubey::host::WindowedRenderFrame& frame) {
        record_atmosphere_graph(device, frame.command_buffer, frame.color_target, frame.frame_slot,
                                cubey::render::render_graph_undefined_texture_state(),
                                cubey::render::render_graph_present_texture_state(),
                                cubey::render::RenderGraphCommandBufferMode::BeginAndEnd);
    }

    void record_atmosphere_target(cubey::vulkan::Device& device, VkCommandBuffer command_buffer,
                                  const cubey::host::HeadlessRenderTarget& target,
                                  cubey::render::FrameSlot frame_slot) {
        record_atmosphere_graph(device, command_buffer, target, frame_slot,
                                cubey::render::render_graph_color_attachment_texture_state(),
                                cubey::render::render_graph_color_attachment_texture_state(),
                                cubey::render::RenderGraphCommandBufferMode::AlreadyRecording);
    }

    [[nodiscard]] cubey::render::AtmosphereBackgroundTextureBindings atmosphere_textures() const {
        if (!atmosphere_atlases_.has_value()) {
            throw std::runtime_error("atmosphere atlas textures are not initialized");
        }
        return atmosphere_atlases_->bindings();
    }

    RunConfig run_config_;
    AtmosphereConfig atmosphere_config_;
    AtmosphereRenderView render_view_ = AtmosphereRenderView::Final;
    cubey::OrbitController view_controller_{cubey::OrbitControllerConfig{
        .distance = 1.0F,
        .min_distance = 1.0F,
        .max_distance = 1.0F,
    }};
    cubey::host::FrameStats ui_frame_stats_;
    std::optional<FrameStatsSnapshot> latest_frame_stats_;
    AtmosphereLoadingStatus loading_status_{};
    double latest_fps_ = 0.0;
    double latest_frame_ms_ = 0.0;
    float headless_base_time_hours_ = 12.0F;
    float headless_base_day_of_year_ = 80.0F;
    std::uint32_t windowed_update_count_ = 0;
    bool reset_requested_ = false;

    VkFormat pipeline_color_format_ = VK_FORMAT_UNDEFINED;
    std::optional<cubey::render::AtmosphereBackgroundAtlasResources> atmosphere_atlases_;
    std::optional<ResolvedNightSkyAtlas> current_night_sky_atlas_;
    cubey::render::AtmosphereBackgroundFrame atmosphere_background_;
    cubey::render::HdrPostFrame hdr_post_frame_;
    cubey::render::RenderGraphFrameExecutor graph_executor_{};
    cubey::jobs::JobSystem atlas_jobs_{2};
    std::optional<cubey::jobs::JobHandle<LunarAtlas>> pending_lunar_atlas_;
    std::optional<PendingNightSkyAtlasJob> pending_night_sky_atlas_;
    bool atlas_shutdown_requested_ = false;
    bool lunar_atlas_ready_ = false;
    std::string lunar_atlas_error_{};
    std::string night_sky_atlas_error_{};
};

} // namespace

int run_atmosphere(const RunConfig& config) {
    AtmosphereApp app(config);
    return app.run();
}

} // namespace cubey::projects::atmosphere
