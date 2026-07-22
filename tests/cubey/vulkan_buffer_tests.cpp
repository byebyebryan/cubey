#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/detail/buffer_upload_plan.h>

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

void test_device_buffer_upload_plan_splits_large_unaligned_requests() {
    constexpr VkDeviceSize chunk_size =
        cubey::vulkan::detail::kDeviceBufferUploadStagingChunkByteSize;
    const std::array<VkDeviceSize, 3> upload_sizes{
        chunk_size + 12,
        7,
        chunk_size - 3,
    };

    const cubey::vulkan::detail::DeviceBufferUploadPlan plan =
        cubey::vulkan::detail::plan_device_buffer_uploads(upload_sizes);

    require(plan.uploaded_byte_count == chunk_size * 2 + 16,
            "upload plan should preserve the exact source byte count");
    require(plan.chunks.size() == 3,
            "upload plan should split payloads across the bounded staging capacity");
    require(plan.chunks[0].staging_byte_size == chunk_size &&
                plan.chunks[1].staging_byte_size == chunk_size &&
                plan.chunks[2].staging_byte_size == 17,
            "upload plan should size each staging chunk to its packed copy range");

    std::array<VkDeviceSize, 3> copied_bytes{};
    std::array<VkDeviceSize, 3> next_destination_offsets{};
    for (const cubey::vulkan::detail::DeviceBufferUploadChunk& chunk : plan.chunks) {
        require(!chunk.pieces.empty(), "each upload chunk should make progress");
        require(chunk.staging_byte_size <= chunk_size,
                "upload chunk should stay within the staging cap");
        for (const cubey::vulkan::detail::DeviceBufferUploadCopyPiece& piece : chunk.pieces) {
            require(piece.source_offset % 4 == 0,
                    "upload copy source offsets should satisfy Vulkan alignment");
            require(piece.destination_offset == next_destination_offsets[piece.upload_index],
                    "split upload copies should remain contiguous in destination order");
            require(piece.source_offset + piece.byte_size <= chunk.staging_byte_size,
                    "upload copy should remain inside its staging chunk");
            copied_bytes[piece.upload_index] += piece.byte_size;
            next_destination_offsets[piece.upload_index] += piece.byte_size;
        }
    }
    require(copied_bytes == upload_sizes,
            "upload plan should cover every request exactly once without gaps");
}
