#include <cubey/vulkan/command_recorder.h>

#include <cubey/vulkan/command_pool.h>

#include <limits>
#include <stdexcept>

namespace cubey::vulkan {
namespace {

void validate_pipeline_layout(VkPipelineLayout layout, const char* message) {
    if (layout == VK_NULL_HANDLE) {
        throw std::runtime_error(message);
    }
}

std::uint32_t span_size_u32(std::size_t size, const char* message) {
    if (size > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error(message);
    }
    return static_cast<std::uint32_t>(size);
}

} // namespace

CommandRecorder::CommandRecorder(VkCommandBuffer command_buffer) : command_buffer_(command_buffer) {
    if (command_buffer_ == VK_NULL_HANDLE) {
        throw std::runtime_error("command recorder requires a command buffer");
    }
}

void CommandRecorder::begin(VkCommandBufferUsageFlags flags) const {
    begin_command_buffer(command_buffer_, flags);
}

void CommandRecorder::end(const char* label) const {
    end_command_buffer(command_buffer_, label);
}

void CommandRecorder::transition_image_layout(const ImageLayoutTransition& transition) const {
    if (transition.image == VK_NULL_HANDLE) {
        throw std::runtime_error("command recorder image transition requires an image");
    }
    cubey::vulkan::transition_image_layout(command_buffer_, transition);
}

void CommandRecorder::begin_rendering(const VkRenderingInfo& rendering) const {
    if (rendering.sType != VK_STRUCTURE_TYPE_RENDERING_INFO) {
        throw std::runtime_error("command recorder begin rendering requires VkRenderingInfo");
    }
    vkCmdBeginRendering(command_buffer_, &rendering);
}

void CommandRecorder::end_rendering() const {
    vkCmdEndRendering(command_buffer_);
}

void CommandRecorder::bind_pipeline(VkPipelineBindPoint bind_point, VkPipeline pipeline) const {
    if (pipeline == VK_NULL_HANDLE) {
        throw std::runtime_error("command recorder bind pipeline requires a pipeline");
    }
    vkCmdBindPipeline(command_buffer_, bind_point, pipeline);
}

void CommandRecorder::bind_descriptor_set(VkPipelineBindPoint bind_point, VkPipelineLayout layout,
                                          std::uint32_t first_set, VkDescriptorSet set,
                                          std::span<const std::uint32_t> dynamic_offsets) const {
    bind_descriptor_sets(bind_point, layout, first_set, std::span<const VkDescriptorSet>(&set, 1),
                         dynamic_offsets);
}

void CommandRecorder::bind_descriptor_sets(VkPipelineBindPoint bind_point, VkPipelineLayout layout,
                                           std::uint32_t first_set,
                                           std::span<const VkDescriptorSet> sets,
                                           std::span<const std::uint32_t> dynamic_offsets) const {
    validate_pipeline_layout(layout,
                             "command recorder bind descriptor sets requires a pipeline layout");
    if (sets.empty()) {
        throw std::runtime_error("command recorder bind descriptor sets requires descriptor sets");
    }
    for (const VkDescriptorSet set : sets) {
        if (set == VK_NULL_HANDLE) {
            throw std::runtime_error("command recorder bind descriptor sets requires valid sets");
        }
    }

    vkCmdBindDescriptorSets(command_buffer_, bind_point, layout, first_set,
                            span_size_u32(sets.size(), "descriptor set count overflow"),
                            sets.data(),
                            span_size_u32(dynamic_offsets.size(), "dynamic offset count overflow"),
                            dynamic_offsets.data());
}

void CommandRecorder::push_constants_bytes(VkPipelineLayout layout, VkShaderStageFlags stage_flags,
                                           std::uint32_t offset, std::uint32_t size,
                                           const void* data) const {
    validate_pipeline_layout(layout, "command recorder push constants requires a pipeline layout");
    if (stage_flags == 0) {
        throw std::runtime_error("command recorder push constants requires stage flags");
    }
    if (size == 0) {
        throw std::runtime_error("command recorder push constants requires a positive size");
    }
    if (data == nullptr) {
        throw std::runtime_error("command recorder push constants requires data");
    }

    vkCmdPushConstants(command_buffer_, layout, stage_flags, offset, size, data);
}

void CommandRecorder::draw(std::uint32_t vertex_count, std::uint32_t instance_count,
                           std::uint32_t first_vertex, std::uint32_t first_instance) const {
    vkCmdDraw(command_buffer_, vertex_count, instance_count, first_vertex, first_instance);
}

void CommandRecorder::draw_indexed(std::uint32_t index_count, std::uint32_t instance_count,
                                   std::uint32_t first_index, std::int32_t vertex_offset,
                                   std::uint32_t first_instance) const {
    vkCmdDrawIndexed(command_buffer_, index_count, instance_count, first_index, vertex_offset,
                     first_instance);
}

void CommandRecorder::dispatch(std::uint32_t group_count_x, std::uint32_t group_count_y,
                               std::uint32_t group_count_z) const {
    vkCmdDispatch(command_buffer_, group_count_x, group_count_y, group_count_z);
}

} // namespace cubey::vulkan
