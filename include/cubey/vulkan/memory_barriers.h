#pragma once

#include <vulkan/vulkan.h>

#include <stdexcept>

namespace cubey::vulkan {

struct MemoryBarrierConfig {
    VkPipelineStageFlags src_stage = 0;
    VkPipelineStageFlags dst_stage = 0;
    VkAccessFlags src_access = 0;
    VkAccessFlags dst_access = 0;
};

[[nodiscard]] inline VkMemoryBarrier memory_barrier(VkAccessFlags src_access,
                                                    VkAccessFlags dst_access) {
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = src_access;
    barrier.dstAccessMask = dst_access;
    return barrier;
}

inline void record_memory_barrier(VkCommandBuffer command_buffer, MemoryBarrierConfig config) {
    if (command_buffer == VK_NULL_HANDLE) {
        throw std::runtime_error("memory barrier requires a command buffer");
    }
    if (config.src_stage == 0 || config.dst_stage == 0) {
        throw std::runtime_error("memory barrier requires source and destination stages");
    }
    const VkMemoryBarrier barrier = memory_barrier(config.src_access, config.dst_access);
    vkCmdPipelineBarrier(command_buffer, config.src_stage, config.dst_stage, 0, 1, &barrier, 0,
                         nullptr, 0, nullptr);
}

inline void record_shader_write_barrier(VkCommandBuffer command_buffer,
                                        VkPipelineStageFlags dst_stage, VkAccessFlags dst_access) {
    record_memory_barrier(command_buffer, {
                                              .src_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                              .dst_stage = dst_stage,
                                              .src_access = VK_ACCESS_SHADER_WRITE_BIT,
                                              .dst_access = dst_access,
                                          });
}

inline void record_transfer_write_barrier(VkCommandBuffer command_buffer,
                                          VkPipelineStageFlags dst_stage,
                                          VkAccessFlags dst_access) {
    record_memory_barrier(command_buffer, {
                                              .src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                                              .dst_stage = dst_stage,
                                              .src_access = VK_ACCESS_TRANSFER_WRITE_BIT,
                                              .dst_access = dst_access,
                                          });
}

inline void record_compute_shader_write_barrier(VkCommandBuffer command_buffer) {
    record_shader_write_barrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
}

inline void record_compute_indirect_shader_write_barrier(VkCommandBuffer command_buffer) {
    record_shader_write_barrier(
        command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
            VK_ACCESS_INDIRECT_COMMAND_READ_BIT);
}

inline void record_compute_render_shader_write_barrier(VkCommandBuffer command_buffer) {
    record_shader_write_barrier(command_buffer,
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
}

} // namespace cubey::vulkan
