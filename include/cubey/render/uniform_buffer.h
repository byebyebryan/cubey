#pragma once

#include <cubey/render/frame_data.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace cubey::render {

[[nodiscard]] cubey::vulkan::BufferConfig frame_uniform_buffer_config(VkDeviceSize byte_size);

template <typename T>
class FrameUniformBuffer {
    static_assert(std::is_trivially_copyable_v<T>,
                  "frame uniform buffer data must be trivially copyable");

  public:
    FrameUniformBuffer(const cubey::vulkan::Device& device, std::uint32_t frame_slot_count) {
        if (frame_slot_count == 0) {
            throw std::runtime_error("frame uniform buffer slot count must be positive");
        }

        buffers_.reserve(frame_slot_count);
        const cubey::vulkan::BufferConfig config =
            frame_uniform_buffer_config(uniform_byte_size());
        for (std::uint32_t slot = 0; slot < frame_slot_count; ++slot) {
            buffers_.emplace_back(device, config);
        }
    }

    ~FrameUniformBuffer() = default;

    FrameUniformBuffer(const FrameUniformBuffer&) = delete;
    FrameUniformBuffer& operator=(const FrameUniformBuffer&) = delete;
    FrameUniformBuffer(FrameUniformBuffer&& other) noexcept = default;
    FrameUniformBuffer& operator=(FrameUniformBuffer&& other) noexcept = default;

    [[nodiscard]] static constexpr VkDeviceSize uniform_byte_size() {
        return sizeof(T);
    }

    [[nodiscard]] std::uint32_t slot_count() const {
        return static_cast<std::uint32_t>(buffers_.size());
    }

    [[nodiscard]] VkDeviceSize range() const {
        return uniform_byte_size();
    }

    [[nodiscard]] const cubey::vulkan::Buffer& buffer(FrameSlot slot) const {
        validate_slot(slot);
        return buffers_.at(static_cast<std::size_t>(slot.index));
    }

    void upload(FrameSlot slot, const T& value) const {
        buffer(slot).upload(&value, uniform_byte_size());
    }

  private:
    void validate_slot(FrameSlot slot) const {
        validate_frame_slot(slot);
        if (slot.count != slot_count()) {
            throw std::runtime_error("frame uniform buffer slot count mismatch");
        }
    }

    std::vector<cubey::vulkan::Buffer> buffers_;
};

} // namespace cubey::render
