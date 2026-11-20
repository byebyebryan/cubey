#pragma once

#include <cubey/vulkan/buffer.h>

#include <cstddef>
#include <span>
#include <stdexcept>

namespace cubey::projects::fluid {

inline void record_source_buffer_update(VkCommandBuffer command_buffer,
                                        const cubey::vulkan::Buffer& buffer,
                                        std::span<const std::byte> sources) {
    if (sources.empty()) {
        return;
    }
    if (sources.size() > buffer.size()) {
        throw std::runtime_error("fluid source update exceeds source buffer size");
    }
    // This range is updated in place and read by the injection/combustion shader each frame.
    VkBufferMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = buffer.handle(),
        .offset = 0,
        .size = sources.size(),
    };
    vkCmdPipelineBarrier(command_buffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);
    vkCmdUpdateBuffer(command_buffer, buffer.handle(), 0, sources.size(), sources.data());
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &barrier, 0,
                         nullptr);
}

} // namespace cubey::projects::fluid
