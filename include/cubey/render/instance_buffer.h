#pragma once

#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/command_recorder.h>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <type_traits>

namespace cubey::render {

[[nodiscard]] VkDeviceSize instance_buffer_byte_size(std::size_t instance_count,
                                                     std::size_t instance_size);
[[nodiscard]] cubey::vulkan::BufferConfig instance_buffer_config(VkDeviceSize byte_size);

template <typename InstanceT>
[[nodiscard]] VkVertexInputBindingDescription instance_input_binding(std::uint32_t binding) {
    static_assert(std::is_trivially_copyable_v<InstanceT>,
                  "instance buffer data must be trivially copyable");
    return {
        .binding = binding,
        .stride = static_cast<std::uint32_t>(sizeof(InstanceT)),
        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
    };
}

template <typename InstanceT> class InstanceBuffer {
    static_assert(std::is_trivially_copyable_v<InstanceT>,
                  "instance buffer data must be trivially copyable");

  public:
    InstanceBuffer(cubey::vulkan::GpuRuntime& gpu, std::span<const InstanceT> instances)
        : buffer_(cubey::vulkan::upload_device_buffer(
              gpu, instances.data(), instance_buffer_byte_size(instances.size(), sizeof(InstanceT)),
              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)),
          count_(static_cast<std::uint32_t>(instances.size())) {}

    InstanceBuffer(const InstanceBuffer&) = delete;
    InstanceBuffer& operator=(const InstanceBuffer&) = delete;
    InstanceBuffer(InstanceBuffer&& other) noexcept = default;
    InstanceBuffer& operator=(InstanceBuffer&& other) noexcept = default;

    [[nodiscard]] const cubey::vulkan::Buffer& buffer() const noexcept {
        return buffer_;
    }
    [[nodiscard]] std::uint32_t count() const noexcept {
        return count_;
    }

    void bind(const cubey::vulkan::CommandRecorder& recorder, std::uint32_t binding) const {
        recorder.bind_vertex_buffer(binding, buffer_.handle());
    }

  private:
    cubey::vulkan::Buffer buffer_;
    std::uint32_t count_ = 0;
};

} // namespace cubey::render
