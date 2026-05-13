#include <cubey/render/material.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/primitive_mesh.h>

#include <vulkan/vulkan.h>

#include <array>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Action> void require_throws(Action&& action, const char* message) {
    try {
        action();
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error(message);
}

[[nodiscard]] cubey::render::MaterialPassInfo forward_pass_info() {
    return cubey::render::MaterialPassInfo{
        .label = "pipeline resource forward",
        .descriptor_sets =
            {
                cubey::render::MaterialDescriptorSetLayout{
                    .set = 0,
                    .bindings =
                        {
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = 0,
                                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                .stage_flags = VK_SHADER_STAGE_VERTEX_BIT,
                            },
                        },
                },
            },
        .push_constants =
            {
                VkPushConstantRange{
                    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                    .offset = 0,
                    .size = 64,
                },
            },
        .cull_mode = VK_CULL_MODE_BACK_BIT,
        .depth_test = true,
        .depth_write = true,
    };
}

} // namespace

void test_render_pipeline_resource_builds_layout_and_dynamic_pipeline_info() {
    static_assert(!std::is_copy_constructible_v<cubey::render::GraphicsPipelineResource>);
    static_assert(!std::is_copy_assignable_v<cubey::render::GraphicsPipelineResource>);
    static_assert(!std::is_copy_constructible_v<cubey::render::ShaderProgram>);
    static_assert(!std::is_copy_assignable_v<cubey::render::ShaderProgram>);

    const VkDescriptorSetLayout descriptor_layout = reinterpret_cast<VkDescriptorSetLayout>(0x10);
    const VkPipelineShaderStageCreateInfo vertex_stage = cubey::vulkan::shader_stage(
        VK_SHADER_STAGE_VERTEX_BIT, reinterpret_cast<VkShaderModule>(0x20));
    const VkPipelineShaderStageCreateInfo fragment_stage = cubey::vulkan::shader_stage(
        VK_SHADER_STAGE_FRAGMENT_BIT, reinterpret_cast<VkShaderModule>(0x30), "frag_main");
    const std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages{
        vertex_stage,
        fragment_stage,
    };
    const cubey::render::VertexInputLayout vertex_input =
        cubey::render::vertex_position_color_normal_input_layout();
    const cubey::render::MaterialPassInfo material_pass = forward_pass_info();

    const cubey::render::GraphicsPipelineResourceConfig config{
        .extent = {800, 600},
        .color_format = VK_FORMAT_B8G8R8A8_SRGB,
        .depth_format = VK_FORMAT_D32_SFLOAT,
        .shader_stages = shader_stages,
        .vertex_bindings = vertex_input.bindings(),
        .vertex_attributes = vertex_input.attribute_descriptions(),
        .descriptor_set_layouts = {&descriptor_layout, 1},
        .material_pass = material_pass,
    };

    const cubey::vulkan::PipelineLayoutInfo layout_info =
        cubey::render::graphics_pipeline_layout_info(config);
    require(layout_info.create_info().setLayoutCount == 1,
            "graphics pipeline layout info should use descriptor set layouts");
    require(layout_info.create_info().pSetLayouts != &descriptor_layout,
            "graphics pipeline layout info should copy descriptor set layouts");
    require(layout_info.create_info().pSetLayouts[0] == descriptor_layout,
            "graphics pipeline layout info should preserve descriptor set layout");
    require(layout_info.create_info().pushConstantRangeCount == 1,
            "graphics pipeline layout info should use material push constants");
    require(layout_info.create_info().pPushConstantRanges[0].size == 64,
            "graphics pipeline layout info should preserve push constant range");

    const VkPipelineLayout pipeline_layout = reinterpret_cast<VkPipelineLayout>(0x40);
    const cubey::vulkan::DynamicGraphicsPipelineConfig pipeline_config =
        cubey::render::dynamic_graphics_pipeline_config(config, pipeline_layout);
    require(pipeline_config.layout == pipeline_layout,
            "dynamic graphics pipeline config should use provided layout");
    require(pipeline_config.extent.width == 800 && pipeline_config.extent.height == 600,
            "dynamic graphics pipeline config should preserve extent");
    require(pipeline_config.shader_stages.size() == 2,
            "dynamic graphics pipeline config should preserve shader stages");
    require(std::string_view(pipeline_config.shader_stages[1].pName) == "frag_main",
            "dynamic graphics pipeline config should preserve shader entry points");
    require(pipeline_config.vertex_bindings.size() == 1,
            "dynamic graphics pipeline config should preserve vertex binding count");
    require(pipeline_config.vertex_attributes.size() == 3,
            "dynamic graphics pipeline config should preserve vertex attribute count");
    require(pipeline_config.cull_mode == VK_CULL_MODE_BACK_BIT,
            "dynamic graphics pipeline config should apply material cull mode");
    require(pipeline_config.depth_test && pipeline_config.depth_write,
            "dynamic graphics pipeline config should apply material depth state");

    const cubey::vulkan::DynamicGraphicsPipelineInfo pipeline_info(pipeline_config);
    require(pipeline_info.create_info().stageCount == 2,
            "dynamic graphics pipeline info should accept pipeline resource config output");
}

void test_render_pipeline_resource_allows_vertexless_fullscreen_pipeline_shape() {
    const VkPipelineShaderStageCreateInfo vertex_stage = cubey::vulkan::shader_stage(
        VK_SHADER_STAGE_VERTEX_BIT, reinterpret_cast<VkShaderModule>(0x50));
    const VkPipelineShaderStageCreateInfo fragment_stage = cubey::vulkan::shader_stage(
        VK_SHADER_STAGE_FRAGMENT_BIT, reinterpret_cast<VkShaderModule>(0x60));
    const std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages{
        vertex_stage,
        fragment_stage,
    };
    const cubey::render::GraphicsPipelineResourceConfig config{
        .extent = {320, 200},
        .color_format = VK_FORMAT_R8G8B8A8_UNORM,
        .shader_stages = shader_stages,
        .material_pass =
            cubey::render::MaterialPassInfo{
                .label = "fullscreen present",
            },
    };

    const cubey::vulkan::DynamicGraphicsPipelineConfig pipeline_config =
        cubey::render::dynamic_graphics_pipeline_config(config,
                                                        reinterpret_cast<VkPipelineLayout>(0x70));
    const cubey::vulkan::DynamicGraphicsPipelineInfo pipeline_info(pipeline_config);

    require(pipeline_info.create_info().pVertexInputState->vertexBindingDescriptionCount == 0,
            "vertexless fullscreen pipeline should not require vertex bindings");
    require(pipeline_info.create_info().pVertexInputState->vertexAttributeDescriptionCount == 0,
            "vertexless fullscreen pipeline should not require vertex attributes");
    require(pipeline_info.create_info().pColorBlendState->attachmentCount == 1,
            "fullscreen present pipeline should still describe the color target");
}

void test_render_pipeline_resource_builds_compute_pipeline_info() {
    static_assert(!std::is_copy_constructible_v<cubey::render::ComputePipelineResource>);
    static_assert(!std::is_copy_assignable_v<cubey::render::ComputePipelineResource>);

    const VkDescriptorSetLayout descriptor_layout = reinterpret_cast<VkDescriptorSetLayout>(0x80);
    const VkPushConstantRange push_constant{
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = 32,
    };
    const cubey::render::ComputePipelineResourceConfig config{
        .shader_stage =
            {
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .path = "particles.comp.spv",
                .entry_point = "compute_main",
            },
        .descriptor_set_layouts = {&descriptor_layout, 1},
        .push_constants = {&push_constant, 1},
    };

    const cubey::vulkan::PipelineLayoutInfo layout_info =
        cubey::render::compute_pipeline_layout_info(config);
    require(layout_info.create_info().setLayoutCount == 1,
            "compute pipeline layout info should use descriptor set layouts");
    require(layout_info.create_info().pSetLayouts != &descriptor_layout,
            "compute pipeline layout info should copy descriptor set layouts");
    require(layout_info.create_info().pSetLayouts[0] == descriptor_layout,
            "compute pipeline layout info should preserve descriptor set layout");
    require(layout_info.create_info().pushConstantRangeCount == 1,
            "compute pipeline layout info should use push constants");
    require(layout_info.create_info().pPushConstantRanges[0].size == 32,
            "compute pipeline layout info should preserve push constant range");

    const VkPipelineLayout pipeline_layout = reinterpret_cast<VkPipelineLayout>(0x90);
    const VkPipelineShaderStageCreateInfo shader_stage = cubey::vulkan::shader_stage(
        VK_SHADER_STAGE_COMPUTE_BIT, reinterpret_cast<VkShaderModule>(0xA0), "compute_main");
    const cubey::vulkan::ComputePipelineConfig pipeline_config =
        cubey::render::compute_pipeline_config(config, pipeline_layout, shader_stage);
    require(pipeline_config.layout == pipeline_layout,
            "compute pipeline config should use provided layout");
    require(pipeline_config.shader_stage.stage == VK_SHADER_STAGE_COMPUTE_BIT,
            "compute pipeline config should preserve compute stage");
    require(std::string_view(pipeline_config.shader_stage.pName) == "compute_main",
            "compute pipeline config should preserve shader entry point");

    const cubey::vulkan::ComputePipelineInfo pipeline_info(pipeline_config);
    require(pipeline_info.create_info().layout == pipeline_layout,
            "compute pipeline info should accept pipeline resource config output");

    require_throws(
        [&config, shader_stage] {
            (void)cubey::render::compute_pipeline_config(config, VK_NULL_HANDLE, shader_stage);
        },
        "compute pipeline config should reject a null pipeline layout");

    const cubey::render::ComputePipelineResourceConfig wrong_stage_config{
        .shader_stage =
            {
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .path = "wrong.vert.spv",
            },
    };
    require_throws(
        [&wrong_stage_config] {
            (void)cubey::render::compute_pipeline_layout_info(wrong_stage_config);
        },
        "compute pipeline layout info should reject non-compute shader stages");
}
