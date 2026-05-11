#include <cubey/vulkan/command_recorder.h>

#include <vulkan/vulkan.h>

#include <array>
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
}
