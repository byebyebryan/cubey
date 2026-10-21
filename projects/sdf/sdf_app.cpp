#include "sdf_config.h"
#include "sdf_field.h"
#include "sdf_renderer.h"
#include "sdf_validation.h"

#include <cubey/engine/project_runtime.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/pass.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/image_transitions.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>

namespace cubey::projects::sdf {
namespace {
class App {
  public:
    explicit App(SdfConfig config)
        : config_(std::move(config)),
          recipe_(make_recipe(config_.scene, config_.seed, config_.count)) {
        if (config_.debug > 3 || config_.shadows > 2 || !std::isfinite(config_.yaw) ||
            !std::isfinite(config_.pitch) || !std::isfinite(config_.distance) ||
            config_.distance < 0.6F || (config_.supersample != 1 && config_.supersample != 2) ||
            !std::isfinite(config_.exposure) || !std::isfinite(config_.sun_elevation) ||
            !std::isfinite(config_.sun_azimuth) || !std::isfinite(config_.roughness) ||
            config_.roughness < 0.05F || config_.roughness > 1.0F ||
            !std::isfinite(config_.shadow_softness) || config_.shadow_softness < 0 ||
            config_.shadow_softness > 0.3F)
            throw std::invalid_argument("invalid SDF view options");
        orbit_.set_pitch(config_.pitch);
        orbit_.set_distance(config_.distance);
    }
    int run() {
        if (config_.common.headless) {
            host::HeadlessPngHostCallbacks callbacks;
            callbacks.create_resources = [&](host::HeadlessPngContext& ctx) {
                initialize(ctx.device(), ctx.gpu(), ctx.render_target(),
                           host::headless_capture_frame_slot_count(config_.common));
            };
            callbacks.record_frame =
                [&](host::HeadlessPngContext& ctx, const host::HeadlessCaptureFrame& frame,
                    VkCommandBuffer command, const host::HeadlessRenderTarget& target) {
                    renderer_.record(ctx.device(), command, target, frame.frame_slot, false,
                                     config_, config_.yaw + orbit_.yaw(), orbit_.pitch(),
                                     orbit_.distance(), ctx.profile_recorder(), frame.index);
                };
            callbacks.shutdown = [&](host::HeadlessPngContext&) { shutdown(); };
            return host::HeadlessPngHost(
                       {.run_config = config_.common,
                        .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT},
                       std::move(callbacks))
                .run();
        }
        host::WindowedAppCallbacks callbacks;
        callbacks.create_swapchain_resources = [&](host::WindowedAppContext& ctx) {
            initialize(ctx.device(), ctx.gpu(),
                       render::ColorTargetView{.extent = ctx.swapchain().extent(),
                                               .format = ctx.swapchain().format()},
                       ctx.frame_slot_count());
        };
        callbacks.destroy_swapchain_resources = [&](host::WindowedAppContext&) {
            renderer_.destroy();
        };
        callbacks.update = [&](host::WindowedAppContext& ctx, const FrameTiming& timing) {
            orbit_.update_from_input(ctx.filtered_input(), timing.delta_seconds);
        };
        callbacks.record_frame = [&](host::WindowedAppContext& ctx,
                                     const host::WindowedRenderFrame& frame) {
            const vulkan::CommandRecorder recorder(frame.command_buffer);
            recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
            renderer_.record(ctx.device(), frame.command_buffer, frame.color_target,
                             frame.frame_slot, true, config_, config_.yaw + orbit_.yaw(),
                             orbit_.pitch(), orbit_.distance(), ctx.profile_recorder(),
                             frame.timing.frame_index);
            recorder.end("sdf frame");
        };
        callbacks.shutdown = [&](host::WindowedAppContext&) { shutdown(); };
        return host::run_windowed_app(
            {.run_config = config_.common,
             .app_name = "sdf",
             .ready_status = "rendering sparse SDF bricks",
             .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
             .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
             .require_dynamic_rendering = true,
             .close_on_escape = true},
            std::move(callbacks));
    }

  private:
    void initialize(vulkan::Device& device, vulkan::GpuRuntime& gpu,
                    const render::ColorTargetView& target, std::uint32_t slots) {
        runtime_.attach_gpu_if_needed(gpu);
        if (!field_) {
            field_ = std::make_unique<Field>(device, runtime_.gpu(), config_.atlas_axis);
            if (config_.validate_batches)
                validate_batches(device, runtime_.gpu(), *field_);
            field_->build(recipe_.edits);
            if (config_.validate)
                validate_field(device, runtime_.gpu(), *field_, recipe_);
            std::cout << "sdf: recipe hash=" << std::hex << recipe_hash(recipe_) << std::dec
                      << '\n';
        }
        renderer_.create(device, target.extent, target.format, slots, *field_, config_);
    }
    void shutdown() {
        renderer_.destroy();
        field_.reset();
        runtime_.detach_gpu_if_attached();
    }
    SdfConfig config_;
    Recipe recipe_;
    ProjectRuntimeAdapter runtime_{1};
    OrbitController orbit_{{.distance = 1.35F, .min_distance = 0.6F, .max_distance = 5.0F}};
    std::unique_ptr<Field> field_;
    Renderer renderer_;
};
} // namespace
int run_sdf(const SdfConfig& config) {
    return App(config).run();
}
} // namespace cubey::projects::sdf
