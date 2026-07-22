#include <cubey/vulkan/buffer.h>

#include <vulkan/vulkan.h>

#include <array>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

cubey::vulkan::Device* fake_device() {
    return reinterpret_cast<cubey::vulkan::Device*>(0x55);
}

cubey::vulkan::SubmissionCoordinator fake_submission() {
    return cubey::vulkan::SubmissionCoordinator(
        reinterpret_cast<VkQueue>(0x56),
        [](VkQueue, const cubey::vulkan::QueueSubmitInfo&, const char*) {},
        [](VkQueue, const char*) {});
}

template <typename Function>
void require_rejected(Function&& function, const char* expected_message) {
    bool rejected = false;
    try {
        function();
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()) == expected_message;
    }
    require(rejected, "device buffer upload validation should reject the request");
}

} // namespace

void test_device_buffer_upload_batch_empty_is_a_noop() {
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });

    cubey::vulkan::DeviceBufferUploadBatch batch =
        cubey::vulkan::upload_device_buffers(runtime, {}, "empty upload batch");

    require(batch.buffers.empty(), "empty upload batch should return no buffers");
    require(batch.uploaded_byte_count == 0, "empty upload batch should report zero bytes");
    require(batch.transfer_submission_count == 0,
            "empty upload batch should report zero transfer submissions");
    require(runtime.empty(), "empty upload batch should not enqueue GPU work");
}

void test_device_buffer_upload_batch_rejects_invalid_requests_before_gpu_work() {
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });
    const std::array<std::byte, 4> bytes{};

    const cubey::vulkan::DeviceBufferUpload missing_data{
        .byte_size = bytes.size(),
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
    };
    require_rejected(
        [&] {
            static_cast<void>(cubey::vulkan::upload_device_buffers(runtime, {&missing_data, 1}));
        },
        "device buffer upload requires data");

    const cubey::vulkan::DeviceBufferUpload empty{
        .data = bytes.data(),
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
    };
    require_rejected(
        [&] { static_cast<void>(cubey::vulkan::upload_device_buffers(runtime, {&empty, 1})); },
        "device buffer upload size must be positive");

    const cubey::vulkan::DeviceBufferUpload missing_usage{
        .data = bytes.data(),
        .byte_size = bytes.size(),
    };
    require_rejected(
        [&] {
            static_cast<void>(cubey::vulkan::upload_device_buffers(runtime, {&missing_usage, 1}));
        },
        "device buffer upload usage must be nonzero");

    const std::array overflow{
        cubey::vulkan::DeviceBufferUpload{
            .data = bytes.data(),
            .byte_size = std::numeric_limits<VkDeviceSize>::max(),
            .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        },
        cubey::vulkan::DeviceBufferUpload{
            .data = bytes.data(),
            .byte_size = 1,
            .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        },
    };
    require_rejected(
        [&] { static_cast<void>(cubey::vulkan::upload_device_buffers(runtime, overflow)); },
        "device buffer upload batch is too large");

    require(runtime.empty(), "invalid upload batches should not enqueue GPU work");
}
