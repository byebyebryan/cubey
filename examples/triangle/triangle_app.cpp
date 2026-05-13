#include "triangle_app.h"

#include <cubey/host/windowed_app.h>
#include <cubey/render/pass.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/target.h>
#include <cubey/vulkan/command_recorder.h>

#include <vulkan/vulkan.h>

#include <array>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <utility>

#ifndef CUBEY_TRIANGLE_SHADER_DIR
#error "CUBEY_TRIANGLE_SHADER_DIR must be defined by the triangle CMake target"
#endif

namespace cubey::examples::triangle {
namespace {

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_TRIANGLE_SHADER_DIR) / filename;
}

[[nodiscard]] cubey::render::MaterialPassInfo triangle_pass_info() {
    return cubey::render::MaterialPassInfo{
        .label = "triangle.fullscreen",
    };
}

class TriangleApp {
  public:
    explicit TriangleApp(RunConfig config) : config_(std::move(config)) {}

    TriangleApp(const TriangleApp&) = delete;
    TriangleApp& operator=(const TriangleApp&) = delete;

    int run() {
        cubey::host::WindowedAppCallbacks callbacks;
        callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            create_pipeline(context);
        };
        callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            (void)context;
            destroy_swapchain_resources();
        };
        callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& frame) {
            (void)context;
            record_triangle_frame(frame);
        };

        return cubey::host::run_windowed_app(
            {
                .run_config = config_,
                .app_name = "triangle",
                .ready_status = "rendering dynamic triangle",
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT,
                .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .require_dynamic_rendering = true,
            },
            std::move(callbacks));
    }

  private:
    void destroy_swapchain_resources() {
        pipeline_resource_.reset();
    }

    void create_pipeline(cubey::host::WindowedAppContext& context) {
        const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
            cubey::render::ShaderStageFile{
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .path = shader_path("triangle.vert.spv"),
            },
            cubey::render::ShaderStageFile{
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .path = shader_path("triangle.frag.spv"),
            },
        };
        const cubey::render::MaterialPassInfo material_pass = triangle_pass_info();
        pipeline_resource_.emplace(context.device(),
                                   cubey::render::GraphicsPipelineFileResourceConfig{
                                       .extent = context.swapchain().extent(),
                                       .color_format = context.swapchain().format(),
                                       .shader_stage_files = shader_stage_files,
                                       .material_pass = material_pass,
                                   });
    }

    void record_triangle_frame(const cubey::host::WindowedRenderFrame& frame) {
        const cubey::vulkan::CommandRecorder recorder(frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        const cubey::render::RenderTargetView target =
            cubey::render::render_target_view(frame.color_target);
        cubey::render::record_present_render_target_pass(
            recorder, target,
            cubey::render::RenderClearValues{
                .color = cubey::render::color_clear_value(0.015F, 0.017F, 0.024F, 1.0F),
            },
            [this](const cubey::vulkan::CommandRecorder& pass_recorder) {
                cubey::render::record_fullscreen_pipeline_draw(pass_recorder,
                                                               {.pipeline = &pipeline_resource()});
            });

        recorder.end("vkEndCommandBuffer triangle");
    }

    [[nodiscard]] const cubey::render::GraphicsPipelineResource& pipeline_resource() const {
        if (!pipeline_resource_.has_value()) {
            throw std::runtime_error("pipeline resource is not initialized");
        }
        return pipeline_resource_.value();
    }

    RunConfig config_;
    std::optional<cubey::render::GraphicsPipelineResource> pipeline_resource_;
};

} // namespace

int run_triangle(const RunConfig& config) {
    TriangleApp app(config);
    return app.run();
}

} // namespace cubey::examples::triangle
