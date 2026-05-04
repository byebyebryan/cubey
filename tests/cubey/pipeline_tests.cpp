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
    config.extent = {640, 480};
    config.color_format = VK_FORMAT_B8G8R8A8_SRGB;
    config.depth_format = VK_FORMAT_D32_SFLOAT;
    config.shader_stages = stages;
    config.vertex_bindings = {&vertex_binding, 1};
    config.vertex_attributes = vertex_attributes;
    config.depth_test = true;
    config.depth_write = true;

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
    require(create_info.pViewportState->pViewports->width == 640.0F,
            "viewport should use configured extent width");
    require(create_info.pViewportState->pViewports->height == 480.0F,
            "viewport should use configured extent height");
    require(create_info.pViewportState->pScissors->extent.width == 640,
            "scissor should use configured extent width");
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
}
