#pragma once

#include <cubey/vulkan/image_transitions.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>

namespace cubey::vulkan {

class CommandRecorder {
  public:
    explicit CommandRecorder(VkCommandBuffer command_buffer);

    [[nodiscard]] VkCommandBuffer handle() const noexcept {
        return command_buffer_;
    }

    void begin(VkCommandBufferUsageFlags flags) const;
    void end(const char* label) const;
    void transition_image_layout(const ImageLayoutTransition& transition) const;
    void pipeline_barrier(VkPipelineStageFlags src_stage_mask, VkPipelineStageFlags dst_stage_mask,
                          VkDependencyFlags dependency_flags,
                          std::span<const VkMemoryBarrier> memory_barriers = {},
                          std::span<const VkBufferMemoryBarrier> buffer_barriers = {},
                          std::span<const VkImageMemoryBarrier> image_barriers = {}) const;
    void begin_rendering(const VkRenderingInfo& rendering) const;
    void end_rendering() const;
    void bind_pipeline(VkPipelineBindPoint bind_point, VkPipeline pipeline) const;
    void bind_vertex_buffer(std::uint32_t first_binding, VkBuffer buffer,
                            VkDeviceSize offset = 0) const;
    void bind_vertex_buffers(std::uint32_t first_binding, std::span<const VkBuffer> buffers,
                             std::span<const VkDeviceSize> offsets) const;
    void bind_descriptor_set(VkPipelineBindPoint bind_point, VkPipelineLayout layout,
                             std::uint32_t first_set, VkDescriptorSet set,
                             std::span<const std::uint32_t> dynamic_offsets = {}) const;
    void bind_descriptor_sets(VkPipelineBindPoint bind_point, VkPipelineLayout layout,
                              std::uint32_t first_set, std::span<const VkDescriptorSet> sets,
                              std::span<const std::uint32_t> dynamic_offsets = {}) const;
    void push_constants_bytes(VkPipelineLayout layout, VkShaderStageFlags stage_flags,
                              std::uint32_t offset, std::uint32_t size, const void* data) const;

    template <typename T>
    void push_constants(VkPipelineLayout layout, VkShaderStageFlags stage_flags,
                        std::uint32_t offset, const T& value) const {
        push_constants_bytes(layout, stage_flags, offset, static_cast<std::uint32_t>(sizeof(T)),
                             &value);
    }

    void draw(std::uint32_t vertex_count, std::uint32_t instance_count = 1,
              std::uint32_t first_vertex = 0, std::uint32_t first_instance = 0) const;
    void draw_indirect(VkBuffer buffer, VkDeviceSize offset, std::uint32_t draw_count,
                       std::uint32_t stride) const;
    void draw_indexed(std::uint32_t index_count, std::uint32_t instance_count = 1,
                      std::uint32_t first_index = 0, std::int32_t vertex_offset = 0,
                      std::uint32_t first_instance = 0) const;
    void dispatch_indirect(VkBuffer buffer, VkDeviceSize offset) const;
    void dispatch(std::uint32_t group_count_x, std::uint32_t group_count_y,
                  std::uint32_t group_count_z) const;

  private:
    VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;
};

} // namespace cubey::vulkan
