#include "headless_cube_app.h"

#include "../common/cube_scene.h"

#include <cubey/core/math.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/render/forward_pass.h>
#include <cubey/render/material.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/scene/transform_3d.h>
#include <cubey/vulkan/command_recorder.h>

#include <vulkan/vulkan.h>

#include <array>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <utility>

#ifndef CUBEY_HEADLESS_CUBE_SHADER_DIR
#error "CUBEY_HEADLESS_CUBE_SHADER_DIR must be defined by the headless_cube CMake target"
#endif

namespace cubey::examples::headless_cube {
namespace {

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_HEADLESS_CUBE_SHADER_DIR) / filename;
}

struct PushConstants {
    cubey::math::Mat4 mvp;
};

cubey::render::MaterialPassInfo headless_cube_pass_info() {
    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(PushConstants),
    };
    return cubey::render::MaterialPassInfo{
        .label = "headless_cube.forward",
        .kind = cubey::render::MaterialPassKind::ForwardColor,
        .push_constants = {push_constant_range},
        .depth_test = true,
        .depth_write = true,
    };
}

class HeadlessCubeApp {
  public:
    explicit HeadlessCubeApp(RunConfig config) : config_(std::move(config)) {}

    HeadlessCubeApp(const HeadlessCubeApp&) = delete;
    HeadlessCubeApp& operator=(const HeadlessCubeApp&) = delete;

    int run() {
        cubey::host::HeadlessPngHostConfig host_config;
        host_config.run_config = config_;
        host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT;

        cubey::host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
            create_resources(context);
        };
        callbacks.record_capture = [this](cubey::host::HeadlessPngContext&,
                                          VkCommandBuffer command_buffer,
                                          const cubey::host::HeadlessRenderTarget& target) {
            record_cube(command_buffer, target);
        };
        callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) { destroy_resources(); };

        cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
        return host.run();
    }

  private:
    void create_resources(cubey::host::HeadlessPngContext& context) {
        const cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColor> mesh_data =
            cubey::render::make_cube_position_color_mesh();
        cube_mesh_.emplace(context.gpu(), mesh_data.mesh_config());
        create_forward_pass(context.device(), context.render_target());
    }

    void create_forward_pass(cubey::vulkan::Device& device,
                             const cubey::host::HeadlessRenderTarget& target) {
        const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
            cubey::render::vertex_shader_file(shader_path("headless_cube.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("headless_cube.frag.spv")),
        };
        const cubey::render::VertexInputLayout vertex_input =
            cubey::render::vertex_position_color_input_layout();
        forward_pass_.emplace(
            device,
            cubey::render::GraphicsPipelineTargetInfo{
                .extent = target.extent,
                .color_format = target.format,
            },
            cubey::render::ForwardScenePass3DConfig{
                .pipeline =
                    {
                        .shader_stage_files = shader_stage_files,
                        .vertex_bindings = vertex_input.bindings(),
                        .vertex_attributes = vertex_input.attribute_descriptions(),
                        .material_pass = headless_cube_pass_info(),
                    },
                .clear =
                    {
                        .color = cubey::render::color_clear_value(0.04F, 0.055F, 0.075F, 1.0F),
                        .depth = cubey::render::depth_clear_value(),
                    },
            });
    }

    [[nodiscard]] PushConstants push_constants(VkExtent2D extent) const {
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const cubey::Transform3D camera_transform = cubey::orbit_camera_transform({
            .distance = 4.5F,
            .yaw = 0.62F,
            .pitch = -0.34F,
        });
        const cubey::Camera3D camera;
        const cubey::Transform3D cube_transform =
            cubey::examples::common::cube_spin_transform(0.75F);
        return {
            .mvp = camera.view_projection_matrix(camera_transform, aspect) *
                   cube_transform.affine_matrix(),
        };
    }

    void record_cube(VkCommandBuffer command_buffer,
                     const cubey::host::HeadlessRenderTarget& target) const {
        const cubey::vulkan::CommandRecorder recorder(command_buffer);
        forward_pass().record_to_target(
            recorder, target, [this, &target](const cubey::vulkan::CommandRecorder& pass_recorder) {
                pass_recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            forward_pass().pipeline().pipeline());
                pass_recorder.push_constants(forward_pass().pipeline().layout(),
                                             VK_SHADER_STAGE_VERTEX_BIT, 0,
                                             push_constants(target.extent));
                cubey::render::record_draw_item(pass_recorder.handle(), {.mesh = &cube_mesh()});
            });
    }

    void destroy_resources() {
        forward_pass_.reset();
        cube_mesh_.reset();
    }

    [[nodiscard]] const cubey::render::Mesh& cube_mesh() const {
        if (!cube_mesh_.has_value()) {
            throw std::runtime_error("headless cube mesh is not initialized");
        }
        return cube_mesh_.value();
    }

    [[nodiscard]] const cubey::render::ForwardScenePass3D& forward_pass() const {
        if (!forward_pass_.has_value()) {
            throw std::runtime_error("headless cube forward pass is not initialized");
        }
        return forward_pass_.value();
    }

    RunConfig config_;
    std::optional<cubey::render::Mesh> cube_mesh_;
    std::optional<cubey::render::ForwardScenePass3D> forward_pass_;
};

} // namespace

int run_headless_cube(const RunConfig& config) {
    HeadlessCubeApp app(config);
    return app.run();
}

} // namespace cubey::examples::headless_cube
