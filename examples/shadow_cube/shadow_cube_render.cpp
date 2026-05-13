#include "shadow_cube_render.h"

#include <glm/geometric.hpp>

#ifndef CUBEY_SHADOW_CUBE_SHADER_DIR
#error "CUBEY_SHADOW_CUBE_SHADER_DIR must be defined by the shadow_cube CMake target"
#endif

namespace cubey::examples::shadow_cube::detail {
namespace {

const cubey::math::Vec3 kLightDirection =
    glm::normalize(cubey::math::Vec3{0.45F, 0.82F, 0.35F});

} // namespace

const cubey::math::Vec3& light_direction() {
    return kLightDirection;
}

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_SHADOW_CUBE_SHADER_DIR) / filename;
}

cubey::render::MaterialPassInfo shadow_depth_pass_info() {
    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(ShadowPushConstants),
    };
    return cubey::render::MaterialPassInfo{
        .label = "shadow_cube.depth",
        .kind = cubey::render::MaterialPassKind::DepthOnly,
        .push_constants = {push_constant_range},
        .depth_test = true,
        .depth_write = true,
    };
}

cubey::render::MaterialPassInfo shadow_scene_pass_info() {
    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(ScenePushConstants),
    };
    return cubey::render::MaterialPassInfo{
        .label = "shadow_cube.forward",
        .kind = cubey::render::MaterialPassKind::ForwardColor,
        .descriptor_sets =
            {
                cubey::render::MaterialDescriptorSetLayout{
                    .set = 0,
                    .bindings =
                        {
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = 0,
                                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                        },
                },
            },
        .push_constants = {push_constant_range},
        .depth_test = true,
        .depth_write = true,
    };
}

cubey::render::MaterialPassInfo shadow_present_pass_info() {
    return cubey::render::MaterialPassInfo{
        .label = "shadow_cube.present",
        .kind = cubey::render::MaterialPassKind::ForwardColor,
        .descriptor_sets =
            {
                cubey::render::MaterialDescriptorSetLayout{
                    .set = 0,
                    .bindings =
                        {
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = 0,
                                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                        },
                },
            },
    };
}

cubey::render::RenderGraphTextureState undefined_texture_state() {
    return {
        .layout = VK_IMAGE_LAYOUT_UNDEFINED,
        .access_mask = 0,
        .stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    };
}

cubey::render::RenderGraphTextureState sampled_depth_texture_state() {
    return {
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        .access_mask = VK_ACCESS_SHADER_READ_BIT,
        .stage_mask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    };
}

cubey::render::RenderGraphTextureState present_texture_state() {
    return {
        .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .access_mask = 0,
        .stage_mask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    };
}

} // namespace cubey::examples::shadow_cube::detail
