#include "spinning_cube_app_internal.h"

#include "../common/forward_pass.h"

#include <cubey/render/material.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/render/primitive_resource.h>

#include <array>
#include <filesystem>

namespace cubey::examples::spinning_cube {
namespace {

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_SPINNING_CUBE_SHADER_DIR) / filename;
}

struct PushConstants {
    cubey::math::Mat4 mvp;
};

cubey::render::MaterialPassInfo spinning_cube_forward_pass_info() {
    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(PushConstants),
    };
    return cubey::render::MaterialPassInfo{
        .label = "spinning_cube.forward",
        .kind = cubey::render::MaterialPassKind::ForwardColor,
        .push_constants = {push_constant_range},
        .depth_test = true,
        .depth_write = true,
    };
}

} // namespace

void SpinningCubeApp::create_global_resources_if_needed(cubey::host::WindowedAppContext& context) {
    if (meshes_.contains(cube_mesh_handle_)) {
        return;
    }
    cube_material_handle_ = engine_.render_resources().create_material("spinning_cube.material");
    cube_mesh_handle_ = cubey::render::create_primitive_mesh_resource(
        engine_.render_resources(), meshes_, context.gpu(), "spinning_cube.cube",
        cubey::render::make_cube_position_color_mesh());
    create_scene();
}

void SpinningCubeApp::create_swapchain_resources(cubey::host::WindowedAppContext& context) {
    create_forward_pass(context);
}

void SpinningCubeApp::destroy_swapchain_resources() {
    forward_pass_.reset();
}

void SpinningCubeApp::destroy_all_resources() {
    destroy_swapchain_resources();
    destroy_scene_if_needed();
    destroy_render_handles();
}

void SpinningCubeApp::create_forward_pass(cubey::host::WindowedAppContext& context) {
    cubey::examples::common::emplace_forward_scene_pass_3d(
        forward_pass_, context.device(),
        {
            .extent = context.swapchain().extent(),
            .color_format = context.swapchain().format(),
            .vertex_shader = shader_path("spinning_cube.vert.spv"),
            .fragment_shader = shader_path("spinning_cube.frag.spv"),
            .vertex_input = cubey::render::vertex_position_color_input_layout(),
            .material_pass = spinning_cube_forward_pass_info(),
            .clear =
                {
                    .color = cubey::render::color_clear_value(0.015F, 0.017F, 0.024F, 1.0F),
                    .depth = cubey::render::depth_clear_value(),
                },
        });
}

void SpinningCubeApp::destroy_render_handles() {
    cubey::render::destroy_mesh_resource(engine_.render_resources(), meshes_, cube_mesh_handle_);
    if (engine_.render_resources().is_alive(cube_material_handle_)) {
        engine_.render_resources().destroy_material(cube_material_handle_);
        cube_material_handle_ = {};
    }
}

} // namespace cubey::examples::spinning_cube
