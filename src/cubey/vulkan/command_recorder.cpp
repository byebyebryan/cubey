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

void CommandRecorder::pipeline_barrier(VkPipelineStageFlags src_stage_mask,
                                       VkPipelineStageFlags dst_stage_mask,
                                       VkDependencyFlags dependency_flags,
                                       std::span<const VkMemoryBarrier> memory_barriers,
                                       std::span<const VkBufferMemoryBarrier> buffer_barriers,
                                       std::span<const VkImageMemoryBarrier> image_barriers) const {
    if (src_stage_mask == 0) {
        throw std::runtime_error("command recorder pipeline barrier requires source stages");
    }
    if (dst_stage_mask == 0) {
        throw std::runtime_error("command recorder pipeline barrier requires destination stages");
    }
    if (memory_barriers.empty() && buffer_barriers.empty() && image_barriers.empty()) {
        throw std::runtime_error("command recorder pipeline barrier requires barriers");
    }
    for (const VkMemoryBarrier& barrier : memory_barriers) {
        if (barrier.sType != VK_STRUCTURE_TYPE_MEMORY_BARRIER) {
            throw std::runtime_error("command recorder pipeline barrier requires memory sType");
        }
    }
    for (const VkBufferMemoryBarrier& barrier : buffer_barriers) {
        if (barrier.sType != VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER) {
            throw std::runtime_error("command recorder pipeline barrier requires buffer sType");
        }
        if (barrier.buffer == VK_NULL_HANDLE) {
            throw std::runtime_error("command recorder pipeline barrier requires buffer handles");
        }
        if (barrier.size == 0) {
            throw std::runtime_error("command recorder pipeline barrier requires buffer size");
        }
    }
    for (const VkImageMemoryBarrier& barrier : image_barriers) {
        if (barrier.sType != VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER) {
            throw std::runtime_error("command recorder pipeline barrier requires image sType");
        }
        if (barrier.image == VK_NULL_HANDLE) {
            throw std::runtime_error("command recorder pipeline barrier requires image handles");
        }
        if (barrier.subresourceRange.aspectMask == 0) {
            throw std::runtime_error("command recorder pipeline barrier requires image aspects");
        }
    }

    vkCmdPipelineBarrier(command_buffer_, src_stage_mask, dst_stage_mask, dependency_flags,
                         span_size_u32(memory_barriers.size(), "memory barrier count overflow"),
                         memory_barriers.data(),
                         span_size_u32(buffer_barriers.size(), "buffer barrier count overflow"),
                         buffer_barriers.data(),
                         span_size_u32(image_barriers.size(), "image barrier count overflow"),
                         image_barriers.data());
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

void CommandRecorder::bind_vertex_buffer(std::uint32_t first_binding, VkBuffer buffer,
                                         VkDeviceSize offset) const {
    bind_vertex_buffers(first_binding, std::span<const VkBuffer>(&buffer, 1),
                        std::span<const VkDeviceSize>(&offset, 1));
}

void CommandRecorder::bind_vertex_buffers(std::uint32_t first_binding,
                                          std::span<const VkBuffer> buffers,
                                          std::span<const VkDeviceSize> offsets) const {
    if (buffers.empty()) {
        throw std::runtime_error("command recorder bind vertex buffers requires buffers");
    }
    if (buffers.size() != offsets.size()) {
        throw std::runtime_error("command recorder bind vertex buffers requires matching offsets");
    }
    for (const VkBuffer buffer : buffers) {
        if (buffer == VK_NULL_HANDLE) {
            throw std::runtime_error("command recorder bind vertex buffers requires valid buffers");
        }
    }
    vkCmdBindVertexBuffers(command_buffer_, first_binding,
                           span_size_u32(buffers.size(), "vertex buffer count overflow"),
                           buffers.data(), offsets.data());
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
