#include <cubey/render/uniform_buffer.h>

#include <vulkan/vulkan.h>

#include <array>
#include <stdexcept>
#include <type_traits>

namespace {

struct TestUniforms {
    std::array<float, 16> transform{};
};

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_throws(void (*fn)(), const char* message) {
    try {
        fn();
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error(message);
}

} // namespace

void test_frame_uniform_buffer_config_describes_host_visible_uniform_storage() {
    const cubey::vulkan::BufferConfig config =
        cubey::render::frame_uniform_buffer_config(sizeof(TestUniforms));

    require(config.size == sizeof(TestUniforms),
            "frame uniform buffer config should preserve byte size");
    require(config.usage == VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            "frame uniform buffer config should use uniform-buffer usage");
    require(config.memory_properties ==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
            "frame uniform buffer config should use host-visible coherent memory");
}

void test_frame_uniform_buffer_contract_is_slot_based_and_move_only() {
    using UniformBuffer = cubey::render::FrameUniformBuffer<TestUniforms>;

    static_assert(std::is_trivially_copyable_v<TestUniforms>);
    static_assert(!std::is_copy_constructible_v<UniformBuffer>);
    static_assert(!std::is_copy_assignable_v<UniformBuffer>);
    static_assert(std::is_move_constructible_v<UniformBuffer>);
    static_assert(std::is_move_assignable_v<UniformBuffer>);
    static_assert(
        std::is_same_v<decltype(&UniformBuffer::upload),
                       void (UniformBuffer::*)(cubey::render::FrameSlot,
                                               const TestUniforms&) const>);
    static_assert(std::is_same_v<decltype(&UniformBuffer::buffer),
                                 const cubey::vulkan::Buffer& (UniformBuffer::*)(
                                     cubey::render::FrameSlot) const>);

    require(UniformBuffer::uniform_byte_size() == sizeof(TestUniforms),
            "frame uniform buffer should expose its uniform byte size");
    require_throws([] { (void)cubey::render::frame_uniform_buffer_config(0); },
                   "frame uniform buffer config should reject zero byte size");
}
