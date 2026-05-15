#include "textured_cube_app_internal.h"

#include "../common/forward_pass.h"

#include <cubey/render/generated_texture.h>
#include <cubey/render/material.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/render/primitive_resource.h>

#include <array>
#include <filesystem>
#include <stdexcept>

namespace cubey::examples::textured_cube {
namespace {

constexpr std::uint32_t kTextureWidth = 64;
constexpr std::uint32_t kTextureHeight = 64;
constexpr VkFormat kTextureFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr std::uint32_t kTextureComputeGroupSize = 8;

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_TEXTURED_CUBE_SHADER_DIR) / filename;
}

cubey::render::MaterialPassInfo textured_cube_forward_pass_info() {
    return cubey::render::MaterialPassInfo{
        .label = "textured_cube.forward",
        .kind = cubey::render::MaterialPassKind::ForwardColor,
        .descriptor_sets =
            {
                cubey::render::MaterialDescriptorSetLayout{
                    .set = 0,
                    .bindings =
                        {
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = 0,
                                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                .stage_flags =
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = 1,
                                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                        },
                },
            },
        .depth_test = true,
        .depth_write = true,
    };
}

constexpr std::array<cubey::render::PrimitiveVec3, 6> kCubeFaceColors{
    cubey::render::PrimitiveVec3{1.0F, 0.92F, 0.86F},
    cubey::render::PrimitiveVec3{0.86F, 0.94F, 1.0F},
    cubey::render::PrimitiveVec3{0.9F, 1.0F, 0.9F},
    cubey::render::PrimitiveVec3{1.0F, 0.96F, 0.78F},
    cubey::render::PrimitiveVec3{0.96F, 0.9F, 1.0F},
    cubey::render::PrimitiveVec3{0.86F, 1.0F, 1.0F},
};

} // namespace

void TexturedCubeApp::create_global_resources_if_needed(cubey::host::WindowedAppContext& context) {
    if (meshes_.contains(cube_mesh_handle_)) {
        return;
    }
    cube_material_handle_ = engine_.render_resources().create_material("textured_cube.material");
    cube_mesh_handle_ = cubey::render::create_primitive_mesh_resource(
        engine_.render_resources(), meshes_, context.gpu(), "textured_cube.cube",
        cubey::render::make_cube_position_color_normal_uv_mesh({
            .face_colors = kCubeFaceColors,
        }));
    create_scene();
    create_texture_resources(context);
}

void TexturedCubeApp::create_swapchain_resources(cubey::host::WindowedAppContext& context) {
    create_forward_pass(context);
}

void TexturedCubeApp::destroy_swapchain_resources() {
    forward_pass_.reset();
}

void TexturedCubeApp::destroy_all_resources() {
    destroy_swapchain_resources();
    destroy_scene_if_needed();
    destroy_render_handles();
    texture_.reset();
    material_.reset();
}

void TexturedCubeApp::create_forward_pass(cubey::host::WindowedAppContext& context) {
    const std::array<VkDescriptorSetLayout, 1> set_layouts{material().layout()};
    cubey::examples::common::emplace_forward_scene_pass_3d(
        forward_pass_, context.device(),
        {
            .extent = context.swapchain().extent(),
            .color_format = context.swapchain().format(),
            .vertex_shader = shader_path("textured_cube.vert.spv"),
            .fragment_shader = shader_path("textured_cube.frag.spv"),
            .vertex_input = cubey::render::vertex_position_color_normal_uv_input_layout(),
            .descriptor_set_layouts = set_layouts,
            .material_pass = textured_cube_forward_pass_info(),
            .clear =
                {
                    .color = cubey::render::color_clear_value(0.014F, 0.016F, 0.022F, 1.0F),
                    .depth = cubey::render::depth_clear_value(),
                },
        });
}

void TexturedCubeApp::create_texture_resources(cubey::host::WindowedAppContext& context) {
    texture_.emplace(cubey::render::create_compute_generated_texture_2d(
        context.device(), context.gpu(),
        {
            .label = "textured cube compute texture dispatch",
            .extent = {kTextureWidth, kTextureHeight},
            .format = kTextureFormat,
            .shader = cubey::render::compute_shader_file(shader_path("textured_cube.comp.spv")),
            .group_size_x = kTextureComputeGroupSize,
            .group_size_y = kTextureComputeGroupSize,
            .group_size_z = 1,
            .create_sampler = true,
        }));
    material_.emplace(context.device(), cubey::render::FrameUniformMaterialInstanceConfig{
                                            .material_pass = textured_cube_forward_pass_info(),
                                            .descriptor_set = 0,
                                            .frame_slot_count = context.frame_slot_count(),
                                            .uniform_binding = 0,
                                            .sampled_images =
                                                {
                                                    cubey::render::SampledImageMaterialBinding{
                                                        .binding = 1,
                                                        .sampler = texture().sampler().handle(),
                                                        .image_view = texture().view(),
                                                    },
                                                },
                                        });
}

void TexturedCubeApp::destroy_render_handles() {
    cubey::render::destroy_mesh_resource(engine_.render_resources(), meshes_, cube_mesh_handle_);
    if (engine_.render_resources().is_alive(cube_material_handle_)) {
        engine_.render_resources().destroy_material(cube_material_handle_);
        cube_material_handle_ = {};
    }
}

const cubey::render::Texture2D& TexturedCubeApp::texture() const {
    if (!texture_.has_value()) {
        throw std::runtime_error("texture is not initialized");
    }
    return texture_.value();
}

const cubey::render::FrameUniformMaterialInstance<SceneUniforms>& TexturedCubeApp::material()
    const {
    if (!material_.has_value()) {
        throw std::runtime_error("material is not initialized");
    }
    return material_.value();
}

const cubey::render::ForwardScenePass3D& TexturedCubeApp::forward_pass() const {
    if (!forward_pass_.has_value()) {
        throw std::runtime_error("forward pass is not initialized");
    }
    return forward_pass_.value();
}

} // namespace cubey::examples::textured_cube
