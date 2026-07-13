#include <cubey/vulkan/pipeline.h>

#include <vulkan/vulkan.h>

#include <array>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Function>
void require_throws(Function&& function, const char* message) {
    try {
        function();
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error(message);
}

} // namespace

void test_pipeline_helpers_describe_dynamic_graphics_pipeline_setup() {
    const VkShaderModule vertex_module = reinterpret_cast<VkShaderModule>(0x10);
    const VkShaderModule fragment_module = reinterpret_cast<VkShaderModule>(0x20);
    const VkPipelineShaderStageCreateInfo vertex_stage =
        cubey::vulkan::shader_stage(VK_SHADER_STAGE_VERTEX_BIT, vertex_module);
    require(vertex_stage.sType == VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            "shader stage should use pipeline shader stage create info");
    require(vertex_stage.stage == VK_SHADER_STAGE_VERTEX_BIT,
            "shader stage should preserve stage flag");
    require(vertex_stage.module == vertex_module, "shader stage should preserve shader module");
    require(std::string_view(vertex_stage.pName) == "main",
            "shader stage should default to main entry point");

    const VkPipelineShaderStageCreateInfo fragment_stage =
        cubey::vulkan::shader_stage(VK_SHADER_STAGE_FRAGMENT_BIT, fragment_module, "fragment_main");
    require(fragment_stage.pName == std::string_view("fragment_main"),
            "shader stage should preserve custom entry point");

    const std::array<VkPipelineShaderStageCreateInfo, 2> stages{
        vertex_stage,
        fragment_stage,
    };

    VkVertexInputBindingDescription vertex_binding{};
    vertex_binding.binding = 0;
    vertex_binding.stride = 32;
    vertex_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> vertex_attributes{};
    vertex_attributes[0].location = 0;
    vertex_attributes[0].binding = 0;
    vertex_attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attributes[0].offset = 0;
    vertex_attributes[1].location = 1;
    vertex_attributes[1].binding = 0;
    vertex_attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
    vertex_attributes[1].offset = 24;

    cubey::vulkan::DynamicGraphicsPipelineConfig config;
    config.layout = reinterpret_cast<VkPipelineLayout>(0x30);
    config.color_format = VK_FORMAT_B8G8R8A8_SRGB;
    config.depth_format = VK_FORMAT_D32_SFLOAT;
    config.shader_stages = stages;
    config.vertex_bindings = {&vertex_binding, 1};
    config.vertex_attributes = vertex_attributes;
    config.depth_test = true;
    config.depth_write = true;
    config.blend_enable = true;
    config.src_color_blend_factor = VK_BLEND_FACTOR_ONE;
    config.dst_color_blend_factor = VK_BLEND_FACTOR_ONE;
    config.color_blend_op = VK_BLEND_OP_ADD;
    config.src_alpha_blend_factor = VK_BLEND_FACTOR_ONE;
    config.dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    config.alpha_blend_op = VK_BLEND_OP_ADD;

    const cubey::vulkan::DynamicGraphicsPipelineInfo info(config);
    const VkGraphicsPipelineCreateInfo& create_info = info.create_info();
    require(create_info.sType == VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            "graphics pipeline should use graphics pipeline create info");
    require(create_info.stageCount == stages.size(), "pipeline should reference shader stages");
    require(create_info.pStages != stages.data(), "pipeline should copy shader stages");
    require(create_info.pStages[0].module == vertex_module, "pipeline should copy vertex stage");
    require(create_info.pStages[1].module == fragment_module,
            "pipeline should copy fragment stage");
    require(create_info.layout == config.layout, "pipeline should preserve pipeline layout");

    const auto* rendering_info =
        static_cast<const VkPipelineRenderingCreateInfo*>(create_info.pNext);
    require(rendering_info != nullptr, "pipeline should attach dynamic rendering info");
    require(rendering_info->sType == VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            "dynamic rendering info should use pipeline rendering create info");
    require(rendering_info->colorAttachmentCount == 1,
            "dynamic rendering info should describe one color attachment");
    require(*rendering_info->pColorAttachmentFormats == config.color_format,
            "dynamic rendering info should preserve color format");
    require(rendering_info->depthAttachmentFormat == config.depth_format,
            "dynamic rendering info should preserve depth format");

    require(create_info.pVertexInputState->vertexBindingDescriptionCount == 1,
            "vertex input should preserve binding count");
    require(create_info.pVertexInputState->pVertexBindingDescriptions != &vertex_binding,
            "vertex input should copy bindings");
    require(create_info.pVertexInputState->pVertexBindingDescriptions[0].stride ==
                vertex_binding.stride,
            "vertex input should copy binding data");
    require(create_info.pVertexInputState->vertexAttributeDescriptionCount ==
                vertex_attributes.size(),
            "vertex input should preserve attribute count");
    require(create_info.pVertexInputState->pVertexAttributeDescriptions != vertex_attributes.data(),
            "vertex input should copy attributes");
    require(create_info.pVertexInputState->pVertexAttributeDescriptions[1].offset ==
                vertex_attributes[1].offset,
            "vertex input should copy attribute data");

    require(create_info.pInputAssemblyState->topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            "input assembly should default to triangle list");
    require(create_info.pViewportState->viewportCount == 1,
            "viewport state should describe one dynamic viewport");
    require(create_info.pViewportState->pViewports == nullptr,
            "dynamic viewport should not bake viewport dimensions");
    require(create_info.pViewportState->scissorCount == 1,
            "viewport state should describe one dynamic scissor");
    require(create_info.pViewportState->pScissors == nullptr,
            "dynamic scissor should not bake scissor dimensions");
    require(create_info.pDynamicState != nullptr, "pipeline should enable dynamic state");
    require(create_info.pDynamicState->dynamicStateCount == 2,
            "pipeline should enable viewport and scissor dynamic states");
    require(create_info.pDynamicState->pDynamicStates[0] == VK_DYNAMIC_STATE_VIEWPORT,
            "pipeline should enable dynamic viewport");
    require(create_info.pDynamicState->pDynamicStates[1] == VK_DYNAMIC_STATE_SCISSOR,
            "pipeline should enable dynamic scissor");
    require(create_info.pRasterizationState->cullMode == VK_CULL_MODE_NONE,
            "rasterizer should default to no culling");
    require(create_info.pMultisampleState->rasterizationSamples == VK_SAMPLE_COUNT_1_BIT,
            "multisample should default to one sample");
    require(create_info.pDepthStencilState->depthTestEnable == VK_TRUE,
            "depth state should enable depth testing when requested");
    require(create_info.pDepthStencilState->depthWriteEnable == VK_TRUE,
            "depth state should enable depth writes when requested");
    require(create_info.pColorBlendState->attachmentCount == 1,
            "color blending should describe one attachment");
    require(create_info.pColorBlendState->pAttachments[0].blendEnable == VK_TRUE,
            "color blending should be enabled when requested");
    require(create_info.pColorBlendState->pAttachments[0].srcColorBlendFactor ==
                VK_BLEND_FACTOR_ONE,
            "color blending should preserve the source color factor");
    require(create_info.pColorBlendState->pAttachments[0].dstColorBlendFactor ==
                VK_BLEND_FACTOR_ONE,
            "color blending should preserve the destination color factor");
    require(create_info.pColorBlendState->pAttachments[0].srcAlphaBlendFactor ==
                VK_BLEND_FACTOR_ONE,
            "color blending should preserve the source alpha factor");
    require(create_info.pColorBlendState->pAttachments[0].dstAlphaBlendFactor ==
                VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            "color blending should preserve the destination alpha factor");
}

void test_pipeline_helpers_describe_depth_only_dynamic_graphics_pipeline_setup() {
    const VkPipelineShaderStageCreateInfo vertex_stage = cubey::vulkan::shader_stage(
        VK_SHADER_STAGE_VERTEX_BIT, reinterpret_cast<VkShaderModule>(0x40));

    VkVertexInputBindingDescription vertex_binding{};
    vertex_binding.binding = 0;
    vertex_binding.stride = 12;
    vertex_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription vertex_attribute{};
    vertex_attribute.location = 0;
    vertex_attribute.binding = 0;
    vertex_attribute.format = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attribute.offset = 0;

    cubey::vulkan::DynamicGraphicsPipelineConfig config;
    config.layout = reinterpret_cast<VkPipelineLayout>(0x50);
    config.color_format = VK_FORMAT_UNDEFINED;
    config.depth_format = VK_FORMAT_D32_SFLOAT;
    config.shader_stages = {&vertex_stage, 1};
    config.vertex_bindings = {&vertex_binding, 1};
    config.vertex_attributes = {&vertex_attribute, 1};
    config.depth_test = true;
    config.depth_write = true;

    const cubey::vulkan::DynamicGraphicsPipelineInfo info(config);
    const auto* rendering_info =
        static_cast<const VkPipelineRenderingCreateInfo*>(info.create_info().pNext);
    require(rendering_info->colorAttachmentCount == 0,
            "depth-only pipeline should not describe color attachments");
    require(rendering_info->pColorAttachmentFormats == nullptr,
            "depth-only pipeline should not point at color formats");
    require(rendering_info->depthAttachmentFormat == VK_FORMAT_D32_SFLOAT,
            "depth-only pipeline should preserve depth format");
    require(info.create_info().pColorBlendState->attachmentCount == 0,
            "depth-only pipeline should not describe color blend attachments");
    require(info.create_info().pColorBlendState->pAttachments == nullptr,
            "depth-only pipeline should not point at color blend attachments");
}

void test_pipeline_helpers_describe_multi_color_dynamic_graphics_pipeline_setup() {
    const VkPipelineShaderStageCreateInfo fragment_stage = cubey::vulkan::shader_stage(
        VK_SHADER_STAGE_FRAGMENT_BIT, reinterpret_cast<VkShaderModule>(0x60));
    const std::array<VkFormat, 2> color_formats{
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R16G16B16A16_SFLOAT,
    };

    cubey::vulkan::DynamicGraphicsPipelineConfig config;
    config.layout = reinterpret_cast<VkPipelineLayout>(0x70);
    config.color_formats = {color_formats.begin(), color_formats.end()};
    config.shader_stages = {&fragment_stage, 1};

    const cubey::vulkan::DynamicGraphicsPipelineInfo info(config);
    const auto* rendering_info =
        static_cast<const VkPipelineRenderingCreateInfo*>(info.create_info().pNext);

    require(rendering_info->colorAttachmentCount == color_formats.size(),
            "multi-color pipeline should describe every color attachment");
    require(rendering_info->pColorAttachmentFormats[0] == color_formats[0],
            "multi-color pipeline should preserve first format");
    require(rendering_info->pColorAttachmentFormats[1] == color_formats[1],
            "multi-color pipeline should preserve second format");
    require(info.create_info().pColorBlendState->attachmentCount == color_formats.size(),
            "multi-color pipeline should describe blend state for every attachment");
    require(info.create_info().pColorBlendState->pAttachments[1].colorWriteMask ==
                (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT),
            "multi-color pipeline should use full color write masks");
}

void test_pipeline_helpers_describe_tessellated_graphics_pipeline_setup() {
    const std::array<VkPipelineShaderStageCreateInfo, 4> stages{
        cubey::vulkan::shader_stage(VK_SHADER_STAGE_VERTEX_BIT,
                                    reinterpret_cast<VkShaderModule>(0x81)),
        cubey::vulkan::shader_stage(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
                                    reinterpret_cast<VkShaderModule>(0x82)),
        cubey::vulkan::shader_stage(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
                                    reinterpret_cast<VkShaderModule>(0x83)),
        cubey::vulkan::shader_stage(VK_SHADER_STAGE_FRAGMENT_BIT,
                                    reinterpret_cast<VkShaderModule>(0x84)),
    };
    cubey::vulkan::DynamicGraphicsPipelineConfig config;
    config.layout = reinterpret_cast<VkPipelineLayout>(0x85);
    config.color_format = VK_FORMAT_R8G8B8A8_UNORM;
    config.shader_stages = stages;
    config.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    config.patch_control_points = 4;

    const cubey::vulkan::DynamicGraphicsPipelineInfo info(config);
    require(info.create_info().pTessellationState != nullptr,
            "patch pipeline should attach tessellation state");
    require(info.create_info().pTessellationState->patchControlPoints == 4,
            "patch pipeline should preserve control-point count");

    cubey::vulkan::DynamicGraphicsPipelineConfig missing_points = config;
    missing_points.patch_control_points = 0;
    require_throws(
        [&missing_points] {
            static_cast<void>(cubey::vulkan::DynamicGraphicsPipelineInfo(missing_points));
        },
        "patch pipeline should reject missing control points");

    cubey::vulkan::DynamicGraphicsPipelineConfig triangle = config;
    triangle.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    require_throws(
        [&triangle] { static_cast<void>(cubey::vulkan::DynamicGraphicsPipelineInfo(triangle)); },
        "triangle pipeline should reject tessellation stages and control points");
}
