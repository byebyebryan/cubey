#pragma once

#include <cubey/render/forward_pass.h>
#include <cubey/render/material.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/render/target.h>
#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <array>
#include <filesystem>
#include <optional>
#include <span>

namespace cubey::examples::common {

struct ExampleForwardPass3DConfig {
    VkExtent2D extent{};
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    std::filesystem::path vertex_shader{};
    std::filesystem::path fragment_shader{};
    render::VertexInputLayout vertex_input{};
    std::span<const VkDescriptorSetLayout> descriptor_set_layouts{};
    render::MaterialPassInfo material_pass{};
    render::RenderClearValues clear{};
};

inline void emplace_forward_scene_pass_3d(std::optional<render::ForwardScenePass3D>& forward_pass,
                                          const vulkan::Device& device,
                                          const ExampleForwardPass3DConfig& config) {
    const std::array<render::ShaderStageFile, 2> shader_stage_files{
        render::vertex_shader_file(config.vertex_shader),
        render::fragment_shader_file(config.fragment_shader),
    };
    forward_pass.emplace(
        device,
        render::GraphicsPipelineTargetInfo{
            .extent = config.extent,
            .color_format = config.color_format,
        },
        render::ForwardScenePass3DConfig{
            .pipeline =
                {
                    .shader_stage_files = shader_stage_files,
                    .vertex_bindings = config.vertex_input.bindings(),
                    .vertex_attributes = config.vertex_input.attribute_descriptions(),
                    .descriptor_set_layouts = config.descriptor_set_layouts,
                    .material_pass = config.material_pass,
                },
            .clear = config.clear,
        });
}

} // namespace cubey::examples::common
