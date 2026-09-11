#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/detail/buffer_upload_plan.h>
#include <cubey/vulkan/detail/staging_pool_plan.h>
#include <cubey/vulkan/staging_pool.h>

#include <vulkan/vulkan.h>

#include <array>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

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

void test_staging_pool_plan_aligns_reclaims_and_wraps_ranges() {
    cubey::vulkan::detail::StagingPoolPlanner planner({
        .initial_block_byte_size = 32,
        .growth_block_byte_size = 32,
        .max_total_byte_size = 64,
    });

    const auto first = planner.try_reserve(5, 4);
    const auto second = planner.try_reserve(5, 8);
    require(first.has_value() && first->block_index == 0 && first->offset == 0,
            "first staging reservation should begin at the initial block origin");
    require(second.has_value() && second->block_index == 0 && second->offset == 8,
            "staging reservations should honor requested alignment within a block");

    planner.commit(first->id, {.value = 1});
    planner.commit(second->id, {.value = 2});
    require(planner.reclaim({.value = 1}) == 1,
            "only allocations whose ticket completed should be reclaimed");

    const auto wrapped = planner.try_reserve(4, 4);
    require(wrapped.has_value() && wrapped->block_index == 0 && wrapped->offset == 0,
            "reclaimed leading space should be reused before allocating a new block");
    planner.cancel(wrapped->id);
    require(planner.reclaim({.value = 2}) == 1,
            "later ticket completion should reclaim its remaining aligned range");

    const auto merged = planner.try_reserve(32, 4);
    require(merged.has_value() && merged->block_index == 0 && merged->offset == 0,
            "adjacent reclaimed ranges should merge into a full reusable block");
}

void test_staging_pool_plan_grows_to_cap_then_reports_backpressure() {
    cubey::vulkan::detail::StagingPoolPlanner planner({
        .initial_block_byte_size = 16,
        .growth_block_byte_size = 16,
        .max_total_byte_size = 32,
    });

    const auto first = planner.try_reserve(16, 4);
    const auto second = planner.try_reserve(16, 4);
    require(first.has_value() && second.has_value() && second->block_index == 1,
            "a full initial block should grow by one bounded staging block");
    require(planner.block_count() == 2 && planner.total_capacity_byte_size() == 32,
            "planner growth should remain within the configured total cap");

    planner.commit(first->id, {.value = 1});
    planner.commit(second->id, {.value = 2});
    require(!planner.try_reserve(4, 4).has_value(),
            "in-flight ranges at the cap should apply explicit allocation backpressure");
    require(planner.reclaim({.value = 1}) == 1,
            "completion should reclaim only the first graphics submission range");
    const auto retried = planner.try_reserve(8, 8);
    require(retried.has_value() && retried->block_index == 0 && retried->offset == 0,
            "a retry after reclaim should reuse capacity without further growth");
}

void test_staging_pool_plan_commit_many_validates_before_mutating() {
    cubey::vulkan::detail::StagingPoolPlanner planner({
        .initial_block_byte_size = 32,
        .growth_block_byte_size = 32,
        .max_total_byte_size = 32,
    });
    const auto first = planner.try_reserve(8, 4);
    const auto second = planner.try_reserve(8, 4);
    require(first.has_value() && second.has_value(),
            "commit-many validation requires two active reservations");

    const std::array invalid_ids{first->id, second->id, std::uint64_t{999}};
    bool rejected = false;
    try {
        planner.commit_many(invalid_ids, {.value = 1});
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()) == "staging pool reservation is unknown";
    }
    require(rejected, "commit-many should reject an unknown reservation before mutation");

    planner.cancel(first->id);
    planner.cancel(second->id);
    require(planner.reserved_byte_size() == 0,
            "failed commit-many must leave every earlier reservation cancellable");

    const auto retry_first = planner.try_reserve(8, 4);
    const auto retry_second = planner.try_reserve(8, 4);
    require(retry_first.has_value() && retry_second.has_value(),
            "planner should accept a fresh all-or-none commit set");
    const std::array valid_ids{retry_first->id, retry_second->id};
    planner.commit_many(valid_ids, {.value = 2});
    require(planner.reclaim({.value = 2}) == 2,
            "successful commit-many should retire every reservation on one ticket");
}

void test_gpu_staging_reservation_is_move_only_and_inerts_the_source() {
    static_assert(!std::is_copy_constructible_v<cubey::vulkan::GpuStagingReservation>);
    static_assert(!std::is_copy_assignable_v<cubey::vulkan::GpuStagingReservation>);
    static_assert(std::is_move_constructible_v<cubey::vulkan::GpuStagingReservation>);
    static_assert(!std::is_move_assignable_v<cubey::vulkan::GpuStagingReservation>);

    cubey::vulkan::GpuStagingReservation original;
    original.buffer = reinterpret_cast<VkBuffer>(0x57);
    original.mapped = reinterpret_cast<std::byte*>(0x58);
    original.offset = 12;
    original.byte_size = 20;
    cubey::vulkan::GpuStagingReservation moved(std::move(original));

    require(moved.buffer == reinterpret_cast<VkBuffer>(0x57) &&
                moved.mapped == reinterpret_cast<std::byte*>(0x58) && moved.offset == 12 &&
                moved.byte_size == 20,
            "moved staging reservation should retain the visible lease metadata");
    require(original.buffer == VK_NULL_HANDLE && original.mapped == nullptr &&
                original.offset == 0 && original.byte_size == 0,
            "moved-from staging reservation must be inert");
}
