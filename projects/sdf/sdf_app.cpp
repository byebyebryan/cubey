#include "sdf_config.h"
#include "sdf_field.h"
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
struct Camera {
    math::Mat4 inverse_vp;
    math::Vec4 eye, sunlight, options, forward;
};
static_assert(sizeof(Camera) == 128);
class App {
  public:
    explicit App(SdfConfig config)
        : config_(std::move(config)),
          recipe_(make_recipe(config_.scene, config_.seed, config_.count)) {
        if (config_.debug > 3 || config_.shadows > 1 || !std::isfinite(config_.yaw) ||
            !std::isfinite(config_.pitch) || !std::isfinite(config_.distance) ||
            config_.distance < 0.6F)
            throw std::invalid_argument("invalid SDF view options");
        orbit_.set_pitch(config_.pitch);
        orbit_.set_distance(config_.distance);
    }
    int run() {
        if (config_.common.headless) {
            host::HeadlessPngHostCallbacks callbacks;
            callbacks.create_resources = [&](host::HeadlessPngContext& ctx) {
                initialize(ctx.device(), ctx.gpu(), ctx.render_target());
            };
            callbacks.record_frame = [&](host::HeadlessPngContext&,
                                         const host::HeadlessCaptureFrame&, VkCommandBuffer command,
                                         const host::HeadlessRenderTarget& target) {
                draw(command, target);
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
                                               .format = ctx.swapchain().format()});
        };
        callbacks.destroy_swapchain_resources = [&](host::WindowedAppContext&) { destroy_view(); };
        callbacks.update = [&](host::WindowedAppContext& ctx, const FrameTiming& timing) {
            orbit_.update_from_input(ctx.filtered_input(), timing.delta_seconds);
        };
        callbacks.record_frame = [&](host::WindowedAppContext&,
                                     const host::WindowedRenderFrame& frame) {
            const vulkan::CommandRecorder recorder(frame.command_buffer);
            recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
            render::record_present_render_target(recorder,
                                                 render::render_target_view(frame.color_target),
                                                 [&](const vulkan::CommandRecorder&) {
                                                     draw(frame.command_buffer, frame.color_target);
                                                 });
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
                    const render::ColorTargetView& target) {
        runtime_.attach_gpu_if_needed(gpu);
        if (!field_) {
            field_ = std::make_unique<Field>(device, runtime_.gpu(), config_.atlas_axis);
            if (config_.validate_batches)
                validate_batches(device, runtime_.gpu(), *field_);
            field_->build(recipe_.edits);
            if (config_.validate)
                validate_field(device, runtime_.gpu(), *field_, recipe_);
            const std::array<vulkan::DescriptorSetBindingConfig, 2> bindings{
                {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT},
                 {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT}}};
            descriptors_ = std::make_unique<vulkan::DescriptorSetBundle>(
                device, vulkan::DescriptorSetInfo(bindings));
            vulkan::DescriptorWriteBatch writes;
            writes.storage_buffer(descriptors_->set(), 0, field_->nodes().handle(),
                                  field_->nodes().size());
            writes.combined_image_sampler(descriptors_->set(), 1,
                                          field_->atlas().sampler().handle(),
                                          field_->atlas().view(), VK_IMAGE_LAYOUT_GENERAL);
            writes.update(device);
            std::cout << "sdf: recipe hash=" << std::hex << recipe_hash(recipe_) << std::dec
                      << '\n';
        }
        const auto path = std::filesystem::path(CUBEY_SDF_SHADER_DIR);
        const std::array stages{render::vertex_shader_file(path / "sdf.vert.spv"),
                                render::fragment_shader_file(path / "sdf.frag.spv")};
        const std::array layouts{descriptors_->layout()};
        render::MaterialPassInfo pass{
            .label = "sdf.trace",
            .push_constants = {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Camera)}},
            .depth_test = true,
            .depth_write = true,
            .depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL};
        pipeline_.emplace(
            device, render::GraphicsPipelineFileResourceConfig{.extent = target.extent,
                                                               .color_format = target.format,
                                                               .depth_format = VK_FORMAT_D32_SFLOAT,
                                                               .shader_stage_files = stages,
                                                               .descriptor_set_layouts = layouts,
                                                               .material_pass = pass});
        const std::array raster_stages{render::vertex_shader_file(path / "occluder.vert.spv"),
                                       render::fragment_shader_file(path / "occluder.frag.spv")};
        pass.push_constants = {{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Camera)}};
        occluder_pipeline_.emplace(
            device, render::GraphicsPipelineFileResourceConfig{.extent = target.extent,
                                                               .color_format = target.format,
                                                               .depth_format = VK_FORMAT_D32_SFLOAT,
                                                               .shader_stage_files = raster_stages,
                                                               .material_pass = pass});
        depth_.emplace(device, render::DepthTextureConfig{.extent = target.extent,
                                                          .format = VK_FORMAT_D32_SFLOAT,
                                                          .create_sampler = false,
                                                          .sampler = {}});
    }
    void destroy_view() {
        pipeline_.reset();
        occluder_pipeline_.reset();
        depth_.reset();
    }
    void shutdown() {
        destroy_view();
        descriptors_.reset();
        field_.reset();
        runtime_.detach_gpu_if_attached();
    }
    void draw(VkCommandBuffer command, const render::ColorTargetView& target) {
        const float yaw = config_.yaw + orbit_.yaw(), pitch = orbit_.pitch();
        const math::Vec3 eye = math::Vec3{std::cos(pitch) * std::sin(yaw), std::sin(pitch),
                                          std::cos(pitch) * std::cos(yaw)} *
                               orbit_.distance();
        const auto vp = math::perspective(glm::radians(45.0F),
                                          static_cast<float>(target.extent.width) /
                                              static_cast<float>(target.extent.height),
                                          0.01F, 100.0F) *
                        glm::lookAt(eye, math::Vec3{0}, math::Vec3{0, 1, 0});
        const Camera camera{glm::inverse(vp),
                            {eye, static_cast<float>(config_.atlas_axis)},
                            {glm::normalize(math::Vec3{-0.5F, 0.8F, 0.4F}), 0},
                            {static_cast<float>(config_.debug), static_cast<float>(config_.shadows),
                             config_.ground ? 1.0F : 0.0F, 0},
                            {glm::normalize(-eye), 0}};
        const VkMemoryBarrier memory{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                     .pNext = nullptr,
                                     .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                                     .dstAccessMask =
                                         VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &memory, 0, nullptr, 0,
                             nullptr);
        vulkan::transition_image_layout(
            command, vulkan::begin_depth_attachment_transition(depth_->handle()));
        const vulkan::CommandRecorder recorder(command);
        render::record_render_target_pass(
            recorder, render::render_target_view(target, render::depth_target_view(*depth_)),
            {.color = render::color_clear_value(0, 0, 0, 1),
             .depth = VkClearValue{.depthStencil = {1, 0}}},
            [&](const vulkan::CommandRecorder& pass) {
                pass.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->pipeline());
                pass.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->layout(), 0,
                                         descriptors_->set());
                pass.push_constants(pipeline_->layout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, camera);
                render::record_fullscreen_triangle(pass);
                if (config_.occluder) {
                    pass.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       occluder_pipeline_->pipeline());
                    pass.push_constants(occluder_pipeline_->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                                        camera);
                    pass.draw(6);
                }
            });
    }
    SdfConfig config_;
    Recipe recipe_;
    ProjectRuntimeAdapter runtime_{1};
    OrbitController orbit_{{.distance = 1.35F, .min_distance = 0.6F, .max_distance = 5.0F}};
    std::unique_ptr<Field> field_;
    std::unique_ptr<vulkan::DescriptorSetBundle> descriptors_;
    std::optional<render::GraphicsPipelineResource> pipeline_;
    std::optional<render::GraphicsPipelineResource> occluder_pipeline_;
    std::optional<render::DepthTexture> depth_;
};
} // namespace
int run_sdf(const SdfConfig& config) {
    return App(config).run();
}
} // namespace cubey::projects::sdf
