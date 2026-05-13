#include <cubey/vulkan/command_recorder.h>

#include <vulkan/vulkan.h>

#include <array>
#include <span>
#include <stdexcept>
#include <type_traits>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Fn> void require_runtime_error(Fn&& fn, const char* message) {
    bool threw = false;
    try {
        fn();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    require(threw, message);
}

} // namespace

void test_command_recorder_exposes_non_owning_command_buffer_contract() {
    const VkCommandBuffer command_buffer = reinterpret_cast<VkCommandBuffer>(0x01);
    const cubey::vulkan::CommandRecorder recorder(command_buffer);

    require(recorder.handle() == command_buffer, "command recorder should preserve command buffer");
    static_assert(!std::is_default_constructible_v<cubey::vulkan::CommandRecorder>);
    static_assert(std::is_copy_constructible_v<cubey::vulkan::CommandRecorder>);
    static_assert(std::is_trivially_copyable_v<cubey::vulkan::CommandRecorder>);
}

void test_command_recorder_rejects_invalid_recording_inputs_before_vulkan_calls() {
    require_runtime_error([] { (void)cubey::vulkan::CommandRecorder(VK_NULL_HANDLE); },
                          "command recorder should reject a null command buffer");

    const cubey::vulkan::CommandRecorder recorder(reinterpret_cast<VkCommandBuffer>(0x01));
    require_runtime_error([&] { recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, {}); },
                          "command recorder should reject a null pipeline");
    require_runtime_error([&] { recorder.bind_vertex_buffer(0, VK_NULL_HANDLE); },
                          "command recorder should reject a null vertex buffer");
    const VkBuffer valid_buffer = reinterpret_cast<VkBuffer>(0x02);
    require_runtime_error(
        [&] { recorder.bind_vertex_buffers(0, std::span<const VkBuffer>(&valid_buffer, 1), {}); },
        "command recorder should reject mismatched vertex buffer offsets");
    require_runtime_error(
        [&] {
            recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, {}, 0,
                                         reinterpret_cast<VkDescriptorSet>(0x02));
        },
        "command recorder should reject a null descriptor pipeline layout");
    require_runtime_error(
        [&] {
            recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                         reinterpret_cast<VkPipelineLayout>(0x03), 0, {});
        },
        "command recorder should reject a null descriptor set");

    const std::array<VkDescriptorSet, 1> null_sets{{VK_NULL_HANDLE}};
    require_runtime_error(
        [&] {
            recorder.bind_descriptor_sets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                          reinterpret_cast<VkPipelineLayout>(0x03), 0, null_sets);
        },
        "command recorder should reject null descriptor sets inside spans");

    int value = 1;
    require_runtime_error(
        [&] {
            recorder.push_constants_bytes({}, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(value), &value);
        },
        "command recorder should reject a null push-constant pipeline layout");
    require_runtime_error(
        [&] {
            recorder.push_constants_bytes(reinterpret_cast<VkPipelineLayout>(0x03), 0, 0,
                                          sizeof(value), &value);
        },
        "command recorder should reject empty push-constant stage flags");
    require_runtime_error(
        [&] {
            recorder.push_constants_bytes(reinterpret_cast<VkPipelineLayout>(0x03),
                                          VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(value), nullptr);
        },
        "command recorder should reject null push-constant data");
    require_runtime_error(
        [&] {
            recorder.push_constants_bytes(reinterpret_cast<VkPipelineLayout>(0x03),
                                          VK_SHADER_STAGE_VERTEX_BIT, 0, 0, &value);
        },
        "command recorder should reject zero-size push constants");

    cubey::vulkan::ImageLayoutTransition transition;
    require_runtime_error([&] { recorder.transition_image_layout(transition); },
                          "command recorder should reject layout transitions without an image");

    VkRenderingInfo rendering{};
    require_runtime_error([&] { recorder.begin_rendering(rendering); },
                          "command recorder should reject rendering info without an sType");

    VkMemoryBarrier memory_barrier{};
    memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    require_runtime_error(
        [&] {
            recorder.pipeline_barrier(0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                      std::span<const VkMemoryBarrier>(&memory_barrier, 1), {}, {});
        },
        "command recorder should reject empty source stage masks");
    require_runtime_error(
        [&] {
            recorder.pipeline_barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                                      std::span<const VkMemoryBarrier>(&memory_barrier, 1), {}, {});
        },
        "command recorder should reject empty destination stage masks");
    require_runtime_error(
        [&] {
            recorder.pipeline_barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, {}, {}, {});
        },
        "command recorder should reject empty barrier batches");

    VkBufferMemoryBarrier buffer_barrier{};
    buffer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    buffer_barrier.size = 4;
    require_runtime_error(
        [&] {
            recorder.pipeline_barrier(
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, {},
                std::span<const VkBufferMemoryBarrier>(&buffer_barrier, 1), {});
        },
        "command recorder should reject buffer barriers without buffers");

    VkImageMemoryBarrier image_barrier{};
    image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    require_runtime_error(
        [&] {
            recorder.pipeline_barrier(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, {}, {},
                                      std::span<const VkImageMemoryBarrier>(&image_barrier, 1));
        },
        "command recorder should reject image barriers without images");
}
