#include <cubey/vulkan/pipeline.h>

#include <vulkan/vulkan.h>

#include <array>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_compute_helpers_describe_pipeline_and_layout_setup() {
    const VkDescriptorSetLayout descriptor_layout = reinterpret_cast<VkDescriptorSetLayout>(0x10);
    const VkPushConstantRange push_constant{
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = 16,
    };
    const std::array<VkDescriptorSetLayout, 1> set_layouts{descriptor_layout};
    const std::array<VkPushConstantRange, 1> push_constants{push_constant};

    cubey::vulkan::PipelineLayoutInfo layout_info({set_layouts, push_constants});
    const VkPipelineLayoutCreateInfo& create_info = layout_info.create_info();
    require(create_info.sType == VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            "pipeline layout info should use pipeline layout create info");
    require(create_info.setLayoutCount == set_layouts.size(),
            "pipeline layout info should preserve set layout count");
    require(create_info.pSetLayouts != set_layouts.data(),
            "pipeline layout info should copy set layouts");
    require(create_info.pSetLayouts[0] == descriptor_layout,
            "pipeline layout info should preserve set layout handles");
    require(create_info.pushConstantRangeCount == push_constants.size(),
            "pipeline layout info should preserve push constant count");
    require(create_info.pPushConstantRanges != push_constants.data(),
            "pipeline layout info should copy push constants");
    require(create_info.pPushConstantRanges[0].size == push_constant.size,
            "pipeline layout info should preserve push constant data");

    const VkShaderModule compute_module = reinterpret_cast<VkShaderModule>(0x20);
    const VkPipelineShaderStageCreateInfo stage =
        cubey::vulkan::shader_stage(VK_SHADER_STAGE_COMPUTE_BIT, compute_module);

    cubey::vulkan::ComputePipelineInfo pipeline_info({
        .layout = reinterpret_cast<VkPipelineLayout>(0x30),
        .shader_stage = stage,
    });
    const VkComputePipelineCreateInfo& compute_info = pipeline_info.create_info();
    require(compute_info.sType == VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            "compute pipeline info should use compute pipeline create info");
    require(compute_info.layout == reinterpret_cast<VkPipelineLayout>(0x30),
            "compute pipeline info should preserve layout");
    require(compute_info.stage.stage == VK_SHADER_STAGE_COMPUTE_BIT,
            "compute pipeline info should preserve compute stage");
    require(compute_info.stage.module == compute_module,
            "compute pipeline info should preserve shader module");
}
