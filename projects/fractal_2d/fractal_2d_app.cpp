#include "fractal_2d_app.h"

#include "fractal_2d_view.h"

#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/pan_zoom_2d_controller.h>
#include <cubey/render/pass.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/target.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <utility>

#ifndef CUBEY_FRACTAL_2D_SHADER_DIR
#error "CUBEY_FRACTAL_2D_SHADER_DIR must be defined by the fractal_2d CMake target"
#endif

namespace cubey::projects::fractal_2d {
namespace {

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_FRACTAL_2D_SHADER_DIR) / filename;
}

[[nodiscard]] cubey::render::MaterialPassInfo fractal_pass_info() {
    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(FractalPushConstants),
    };
    return cubey::render::MaterialPassInfo{
        .label = "fractal.fullscreen",
        .push_constants = {push_constant_range},
    };
}

class FractalApp {
  public:
    explicit FractalApp(Fractal2dConfig config) : config_(std::move(config)) {}

    FractalApp(const FractalApp&) = delete;
    FractalApp& operator=(const FractalApp&) = delete;

    ~FractalApp() {
        pipeline_resource_.reset();
    }

    int run() {
        if (config_.common.headless) {
            return run_headless();
        }

        cubey::host::WindowedAppCallbacks callbacks;
        callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            create_pipeline(context.device(), context.swapchain().format(),
                            context.swapchain().extent());
        };
        callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            (void)context;
            destroy_swapchain_resources();
        };
        callbacks.update = [this](cubey::host::WindowedAppContext& context,
                                  const FrameTiming& timing) {
            (void)timing;
            update_input(context);
        };
        callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& frame) {
            (void)context;
            record_fractal_frame(frame);
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext& context) {
            (void)context;
            destroy_swapchain_resources();
        };

        return cubey::host::run_windowed_app(
            {
                .run_config = config_.common,
                .app_name = "fractal_2d",
                .ready_status = "rendering fullscreen fractal",
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT,
                .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .require_dynamic_rendering = true,
                .close_on_escape = true,
            },
            std::move(callbacks));
    }

  private:
    int run_headless() {
        cubey::host::HeadlessPngHostConfig host_config;
        host_config.run_config = config_.common;
        host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT;

        cubey::host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
            const cubey::host::HeadlessRenderTarget& target = context.render_target();
            create_pipeline(context.device(), target.format, target.extent);
        };
        callbacks.record_capture = [this](cubey::host::HeadlessPngContext&,
                                          VkCommandBuffer command_buffer,
                                          const cubey::host::HeadlessRenderTarget& target) {
            const cubey::vulkan::CommandRecorder recorder(command_buffer);
            record_fractal_draw(recorder, target);
        };
        callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) {
            destroy_swapchain_resources();
        };

        cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
        return host.run();
    }

    void update_input(cubey::host::WindowedAppContext& context) {
        const auto input = context.filtered_input();
        if (input.key_pressed(cubey::input::Key::R)) {
            view_controller_.reset();
        }

        const VkExtent2D extent = context.swapchain().extent();
        view_controller_.update_from_input(input, static_cast<float>(extent.width),
                                           static_cast<float>(extent.height));
    }

    void destroy_swapchain_resources() {
        pipeline_resource_.reset();
    }

    void create_pipeline(cubey::vulkan::Device& device, VkFormat color_format, VkExtent2D extent) {
        const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
            cubey::render::ShaderStageFile{
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .path = shader_path("fractal.vert.spv"),
            },
            cubey::render::ShaderStageFile{
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .path = shader_path("fractal.frag.spv"),
            },
        };

        const cubey::render::MaterialPassInfo material_pass = fractal_pass_info();
        pipeline_resource_.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                               .extent = extent,
                                               .color_format = color_format,
                                               .shader_stage_files = shader_stage_files,
                                               .material_pass = material_pass,
                                           });
    }

    [[nodiscard]] FractalPushConstants push_constants(VkExtent2D extent) const {
        return view_.push_constants(view_controller_.camera(), extent.width, extent.height);
    }

    void record_fractal_draw(const cubey::vulkan::CommandRecorder& recorder,
                             const cubey::render::ColorTargetView& target) const {
        const FractalPushConstants constants = push_constants(target.extent);
        cubey::render::record_render_target_pass(
            recorder, cubey::render::render_target_view(target),
            cubey::render::RenderClearValues{
                .color = cubey::render::color_clear_value(0.015F, 0.018F, 0.026F, 1.0F),
            },
            [this, constants](const cubey::vulkan::CommandRecorder& pass_recorder) {
                cubey::render::record_fullscreen_pipeline_draw(
                    pass_recorder, {.pipeline = &pipeline_resource()}, VK_SHADER_STAGE_FRAGMENT_BIT,
                    constants);
            });
    }

    void record_fractal_frame(const cubey::host::WindowedRenderFrame& frame) {
        const cubey::vulkan::CommandRecorder recorder(frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        cubey::render::record_present_render_target(
            recorder, cubey::render::render_target_view(frame.color_target),
            [this, &frame](const cubey::vulkan::CommandRecorder& present_recorder) {
                record_fractal_draw(present_recorder, frame.color_target);
            });

        recorder.end("vkEndCommandBuffer fractal_2d");
    }

    [[nodiscard]] const cubey::render::GraphicsPipelineResource& pipeline_resource() const {
        if (!pipeline_resource_.has_value()) {
            throw std::runtime_error("pipeline resource is not initialized");
        }
        return pipeline_resource_.value();
    }

    Fractal2dConfig config_;
    FractalView view_;
    cubey::input::PanZoom2DController view_controller_{cubey::Camera2D({
        .center = {-0.5F, 0.0F},
        .scale = 1.35F,
    })};

    std::optional<cubey::render::GraphicsPipelineResource> pipeline_resource_;
};

} // namespace

int run_fractal_2d(const Fractal2dConfig& config) {
    FractalApp app(config);
    return app.run();
}

} // namespace cubey::projects::fractal_2d
