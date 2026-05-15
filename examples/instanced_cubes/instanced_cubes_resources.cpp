#include "instanced_cubes_app_internal.h"

#include "../common/forward_pass.h"

#include <cubey/render/color_space.h>
#include <cubey/render/material.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/render/primitive_resource.h>

#include <cstddef>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <vector>

namespace cubey::examples::instanced_cubes {
namespace {

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_INSTANCED_CUBES_SHADER_DIR) / filename;
}

struct PushConstants {
    cubey::math::Mat4 view_projection;
    cubey::math::Mat4 cube_spin;
};

cubey::render::MaterialPassInfo instanced_cubes_forward_pass_info() {
    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(PushConstants),
    };
    return cubey::render::MaterialPassInfo{
        .label = "instanced_cubes.forward",
        .kind = cubey::render::MaterialPassKind::ForwardColor,
        .push_constants = {push_constant_range},
        .depth_test = true,
        .depth_write = true,
    };
}

cubey::render::VertexInputLayout instanced_cube_vertex_input_layout() {
    cubey::render::VertexInputLayout layout =
        cubey::render::vertex_position_color_normal_input_layout();
    layout.vertex_bindings.push_back(cubey::render::instance_input_binding<CubeInstanceData>(1));
    layout.attributes.push_back(cubey::render::vertex_input_attribute(
        3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(CubeInstanceData, model)));
    layout.attributes.push_back(cubey::render::vertex_input_attribute(
        4, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
        offsetof(CubeInstanceData, model) + sizeof(cubey::math::Vec4)));
    layout.attributes.push_back(cubey::render::vertex_input_attribute(
        5, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
        offsetof(CubeInstanceData, model) + (sizeof(cubey::math::Vec4) * 2U)));
    layout.attributes.push_back(cubey::render::vertex_input_attribute(
        6, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
        offsetof(CubeInstanceData, model) + (sizeof(cubey::math::Vec4) * 3U)));
    layout.attributes.push_back(cubey::render::vertex_input_attribute(
        7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(CubeInstanceData, color)));
    return layout;
}

std::vector<CubeInstanceData> create_instance_data() {
    std::vector<CubeInstanceData> instances;
    instances.reserve(kInstanceCount);
    for (std::uint32_t row = 0; row < kGridRows; ++row) {
        for (std::uint32_t column = 0; column < kGridColumns; ++column) {
            const float x = (static_cast<float>(column) - 4.0F) * kGridSpacing;
            const float y = (2.0F - static_cast<float>(row)) * kGridSpacing;
            const float phase = static_cast<float>((row * kGridColumns) + column) * 0.19F;
            const cubey::math::Mat4 model = cubey::math::translation(x, y, 0.0F) *
                                            cubey::math::rotation_y(phase) *
                                            cubey::math::scale({0.42F, 0.42F, 0.42F});
            const float mix_x = static_cast<float>(column) / static_cast<float>(kGridColumns - 1U);
            const float mix_z = static_cast<float>(row) / static_cast<float>(kGridRows - 1U);
            const cubey::math::Vec4 color = cubey::render::srgb_to_linear_rgba({
                0.45F + (0.45F * mix_x),
                0.72F - (0.26F * mix_z),
                0.62F + (0.28F * mix_z),
                1.0F,
            });
            instances.push_back({
                .model = model,
                .color = color,
            });
        }
    }
    return instances;
}

} // namespace

void InstancedCubesApp::create_global_resources_if_needed(
    cubey::host::WindowedAppContext& context) {
    if (meshes_.contains(cube_mesh_handle_)) {
        return;
    }
    cube_material_handle_ =
        engine_.render_resources().create_material("instanced_cubes.material");
    cube_mesh_handle_ = cubey::render::create_primitive_mesh_resource(
        engine_.render_resources(), meshes_, context.gpu(), "instanced_cubes.cube",
        cubey::render::make_cube_position_color_normal_mesh());
    const std::vector<CubeInstanceData> instances = create_instance_data();
    instance_buffer_.emplace(context.gpu(), std::span<const CubeInstanceData>(instances));
    create_scene();
}

void InstancedCubesApp::create_swapchain_resources(cubey::host::WindowedAppContext& context) {
    create_forward_pass(context);
}

void InstancedCubesApp::destroy_swapchain_resources() {
    forward_pass_.reset();
}

void InstancedCubesApp::destroy_all_resources() {
    destroy_swapchain_resources();
    destroy_scene_if_needed();
    destroy_render_handles();
    instance_buffer_.reset();
}

void InstancedCubesApp::create_forward_pass(cubey::host::WindowedAppContext& context) {
    cubey::examples::common::emplace_forward_scene_pass_3d(
        forward_pass_, context.device(),
        {
            .extent = context.swapchain().extent(),
            .color_format = context.swapchain().format(),
            .vertex_shader = shader_path("instanced_cubes.vert.spv"),
            .fragment_shader = shader_path("instanced_cubes.frag.spv"),
            .vertex_input = instanced_cube_vertex_input_layout(),
            .material_pass = instanced_cubes_forward_pass_info(),
            .clear =
                {
                    .color = cubey::render::color_clear_value(0.018F, 0.02F, 0.026F, 1.0F),
                    .depth = cubey::render::depth_clear_value(),
                },
        });
}

void InstancedCubesApp::destroy_render_handles() {
    cubey::render::destroy_mesh_resource(engine_.render_resources(), meshes_, cube_mesh_handle_);
    if (engine_.render_resources().is_alive(cube_material_handle_)) {
        engine_.render_resources().destroy_material(cube_material_handle_);
        cube_material_handle_ = {};
    }
}

const cubey::render::InstanceBuffer<CubeInstanceData>& InstancedCubesApp::instance_buffer() const {
    if (!instance_buffer_.has_value()) {
        throw std::runtime_error("instance buffer is not initialized");
    }
    return instance_buffer_.value();
}

const cubey::render::ForwardScenePass3D& InstancedCubesApp::forward_pass() const {
    if (!forward_pass_.has_value()) {
        throw std::runtime_error("forward pass is not initialized");
    }
    return forward_pass_.value();
}

} // namespace cubey::examples::instanced_cubes
