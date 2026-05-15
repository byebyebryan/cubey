#include "headless_cube_app_internal.h"

#include "../common/forward_pass.h"

#include <cubey/render/material.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/primitive_mesh.h>

#include <filesystem>

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

} // namespace

void HeadlessCubeApp::create_global_resources_if_needed(cubey::host::HeadlessPngContext& context) {
    const cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColor> mesh_data =
        cubey::render::make_cube_position_color_mesh();
    cube_mesh_.emplace(context.gpu(), mesh_data.mesh_config());
    create_forward_pass(context.device(), context.render_target());
}

void HeadlessCubeApp::create_forward_pass(cubey::vulkan::Device& device,
                                          const cubey::host::HeadlessRenderTarget& target) {
    cubey::examples::common::emplace_forward_scene_pass_3d(
        forward_pass_, device,
        {
            .extent = target.extent,
            .color_format = target.format,
            .vertex_shader = shader_path("headless_cube.vert.spv"),
            .fragment_shader = shader_path("headless_cube.frag.spv"),
            .vertex_input = cubey::render::vertex_position_color_input_layout(),
            .material_pass = headless_cube_pass_info(),
            .clear =
                {
                    .color = cubey::render::color_clear_value(0.04F, 0.055F, 0.075F, 1.0F),
                    .depth = cubey::render::depth_clear_value(),
                },
        });
}

void HeadlessCubeApp::destroy_resources() {
    forward_pass_.reset();
    cube_mesh_.reset();
}

} // namespace cubey::examples::headless_cube
