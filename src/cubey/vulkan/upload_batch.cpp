#include <cubey/vulkan/upload_batch.h>

#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/command_pool.h>
#include <cubey/vulkan/vk_check.h>

#include <atomic>
#include <chrono>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cubey::vulkan {

class GpuUploadBatchState : public std::enable_shared_from_this<GpuUploadBatchState> {
  public:
    explicit GpuUploadBatchState(GpuOwnerContext& context)
        : device_(&context.device()), submission_(&context.submission()), started_(Clock::now()) {
        context.require_owner_thread("GPU upload batch creation requires the GPU owner thread");
        command_pool_.emplace(context.device(),
                              CommandPoolConfig{.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT});
        command_buffer_ = command_pool_->allocate_primary();
        begin_command_buffer(command_buffer_, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    }

    ~GpuUploadBatchState() = default;

    void submit(GpuOwnerContext& context, std::string label) {
        context.require_owner_thread("GPU upload batch submission requires the GPU owner thread");
        if (submitted_) {
            throw std::runtime_error("GPU upload batch was already submitted");
        }
        if (label.empty()) {
            label = "vkQueueSubmit upload batch";
        }
        end_command_buffer(command_buffer_, "vkEndCommandBuffer upload batch");
        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        check(vkCreateFence(device_->handle(), &fence_info, nullptr, &fence_),
              "vkCreateFence upload batch");
        try {
            submission_ticket_ = submission_->submit(
                {.command_buffers = {command_buffer_}, .fence = fence_}, label.c_str());
        } catch (...) {
            destroy_fence();
            throw;
        }
        submitted_ = true;
        submitted_at_ = Clock::now();
        metrics_.submission_count = 1U;
        metrics_.owner_submit_milliseconds = elapsed_milliseconds(started_);
        try {
            context.defer_destruction_after(submission_ticket_, [state = shared_from_this()] {
                state->release_after_completion_on_owner_thread();
            });
        } catch (...) {
            static_cast<void>(vkWaitForFences(device_->handle(), 1, &fence_, VK_TRUE, UINT64_MAX));
            release_after_completion_on_owner_thread();
            throw;
        }
    }

    [[nodiscard]] bool complete() const noexcept {
        return complete_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool failed() const noexcept {
        return failed_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::string failure_message() const {
        std::scoped_lock lock(mutex_);
        return failure_message_;
    }

    [[nodiscard]] GpuUploadBatchMetrics metrics() const {
        std::scoped_lock lock(mutex_);
        return metrics_;
    }

    void query_completion_on_owner(GpuOwnerContext& context, bool wait) {
        context.require_owner_thread("GPU upload completion query requires the GPU owner thread");
        if (complete() || failed()) {
            query_in_flight_.store(false, std::memory_order_release);
            return;
        }
        const VkResult result =
            wait ? vkWaitForFences(device_->handle(), 1, &fence_, VK_TRUE, UINT64_MAX)
                 : vkGetFenceStatus(device_->handle(), fence_);
        if (result == VK_NOT_READY) {
            query_in_flight_.store(false, std::memory_order_release);
            return;
        }
        if (result != VK_SUCCESS) {
            {
                std::scoped_lock lock(mutex_);
                failure_message_ = "Vulkan upload completion query failed (VkResult " +
                                   std::to_string(static_cast<int>(result)) + ")";
            }
            failed_.store(true, std::memory_order_release);
            // vkGetFenceStatus can only fail here when the device is lost.  At
            // that point no later graphics submission can establish safe fence
            // ordering, so retire the batch immediately while we are still on
            // the GPU owner.  Do not advance SubmissionCoordinator: doing so
            // would falsely complete this or later tickets.
            release_after_completion_on_owner_thread();
            query_in_flight_.store(false, std::memory_order_release);
            return;
        }
        context.complete_submission(submission_ticket_);
    }

    [[nodiscard]] bool begin_query() {
        bool expected = false;
        return query_in_flight_.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
    }

    void cancel_query() noexcept {
        query_in_flight_.store(false, std::memory_order_release);
    }

    const Device& device() const {
        return *device_;
    }

    VkCommandBuffer command_buffer() const {
        return command_buffer_;
    }

    void retain_staging(Buffer&& staging) {
        staging_.push_back(std::move(staging));
    }

    void discard_unsubmitted(GpuOwnerContext& context) {
        context.require_owner_thread("GPU upload batch discard requires the GPU owner thread");
        if (submitted_) {
            return;
        }
        staging_.clear();
        command_pool_.reset();
    }

    void add_uploaded_bytes(VkDeviceSize byte_count) {
        std::scoped_lock lock(mutex_);
        if (byte_count > std::numeric_limits<std::uint64_t>::max() - metrics_.uploaded_byte_count) {
            throw std::runtime_error("GPU upload batch byte count overflows");
        }
        metrics_.uploaded_byte_count += static_cast<std::uint64_t>(byte_count);
    }

    void add_copy_count(std::uint32_t copy_count) {
        std::scoped_lock lock(mutex_);
        if (copy_count > std::numeric_limits<std::uint32_t>::max() - metrics_.copy_count) {
            throw std::runtime_error("GPU upload batch copy count overflows");
        }
        metrics_.copy_count += copy_count;
    }

    GpuSubmissionTicket submission_ticket() const noexcept {
        return submission_ticket_;
    }

  private:
    using Clock = std::chrono::steady_clock;

    [[nodiscard]] static double elapsed_milliseconds(Clock::time_point started) {
        return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    }

    void release_after_completion_on_owner_thread() {
        if (complete()) {
            return;
        }
        {
            std::scoped_lock lock(mutex_);
            metrics_.completion_latency_milliseconds = elapsed_milliseconds(submitted_at_);
        }
        staging_.clear();
        command_pool_.reset();
        destroy_fence();
        complete_.store(true, std::memory_order_release);
        query_in_flight_.store(false, std::memory_order_release);
    }

    void destroy_fence() noexcept {
        if (fence_ != VK_NULL_HANDLE) {
            vkDestroyFence(device_->handle(), fence_, nullptr);
            fence_ = VK_NULL_HANDLE;
        }
    }

    const Device* device_ = nullptr;
    SubmissionCoordinator* submission_ = nullptr;
    std::optional<CommandPool> command_pool_{};
    VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;
    std::vector<Buffer> staging_{};
    VkFence fence_ = VK_NULL_HANDLE;
    GpuSubmissionTicket submission_ticket_{};
    mutable std::mutex mutex_;
    GpuUploadBatchMetrics metrics_{};
    std::string failure_message_{};
    Clock::time_point started_{};
    Clock::time_point submitted_at_{};
    std::atomic<bool> submitted_{false};
    std::atomic<bool> complete_{false};
    std::atomic<bool> failed_{false};
    std::atomic<bool> query_in_flight_{false};
};

GpuUploadTicket::GpuUploadTicket(std::shared_ptr<GpuUploadBatchState> state)
    : state_(std::move(state)) {}

bool GpuUploadTicket::valid() const noexcept {
    return state_ != nullptr;
}

bool GpuUploadTicket::complete() const noexcept {
    return state_ != nullptr && state_->complete();
}

bool GpuUploadTicket::failed() const noexcept {
    return state_ != nullptr && state_->failed();
}

std::string GpuUploadTicket::failure_message() const {
    return state_ == nullptr ? std::string{} : state_->failure_message();
}

GpuSubmissionTicket GpuUploadTicket::submission_ticket() const noexcept {
    return state_ == nullptr ? GpuSubmissionTicket{} : state_->submission_ticket();
}

GpuUploadBatchMetrics GpuUploadTicket::metrics() const {
    return state_ == nullptr ? GpuUploadBatchMetrics{} : state_->metrics();
}

bool GpuUploadTicket::poll(GpuRuntime& gpu) const {
    if (state_ == nullptr) {
        return true;
    }
    if (state_->failed()) {
        throw std::runtime_error(state_->failure_message());
    }
    if (state_->complete()) {
        return true;
    }
    if (!state_->begin_query()) {
        return false;
    }
    try {
        static_cast<void>(
            gpu.submit("poll GPU upload completion", [state = state_](GpuOwnerContext& context) {
                state->query_completion_on_owner(context, false);
            }));
    } catch (...) {
        state_->cancel_query();
        throw;
    }
    return state_->complete();
}

void GpuUploadTicket::wait(GpuRuntime& gpu) const {
    if (state_ == nullptr) {
        return;
    }
    if (state_->failed()) {
        throw std::runtime_error(state_->failure_message());
    }
    if (state_->complete()) {
        return;
    }
    static_cast<void>(gpu.submit_and_wait({
        .label = "wait for GPU upload completion",
        .work = [state = state_](
                    GpuOwnerContext& context) { state->query_completion_on_owner(context, true); },
    }));
    if (state_->failed()) {
        throw std::runtime_error(state_->failure_message());
    }
}

GpuUploadBatch::GpuUploadBatch(GpuOwnerContext& context)
    : owner_context_(&context), state_(std::make_shared<GpuUploadBatchState>(context)) {}

GpuUploadBatch::~GpuUploadBatch() {
    if (state_ != nullptr) {
        state_->discard_unsubmitted(*owner_context_);
    }
}

const Device& GpuUploadBatch::device() const {
    if (state_ == nullptr) {
        throw std::runtime_error("GPU upload batch has no state");
    }
    return state_->device();
}

VkCommandBuffer GpuUploadBatch::command_buffer() const {
    if (state_ == nullptr) {
        throw std::runtime_error("GPU upload batch has no state");
    }
    return state_->command_buffer();
}

void GpuUploadBatch::retain_staging(Buffer&& staging) {
    if (state_ == nullptr) {
        throw std::runtime_error("GPU upload batch has no state");
    }
    state_->retain_staging(std::move(staging));
}

void GpuUploadBatch::add_uploaded_bytes(VkDeviceSize byte_count) {
    if (state_ == nullptr) {
        throw std::runtime_error("GPU upload batch has no state");
    }
    state_->add_uploaded_bytes(byte_count);
}

void GpuUploadBatch::add_copy_count(std::uint32_t copy_count) {
    if (state_ == nullptr) {
        throw std::runtime_error("GPU upload batch has no state");
    }
    state_->add_copy_count(copy_count);
}

GpuUploadTicket GpuUploadBatch::submit(GpuOwnerContext& context, std::string label) {
    if (state_ == nullptr) {
        throw std::runtime_error("GPU upload batch has no state");
    }
    state_->submit(context, std::move(label));
    return GpuUploadTicket(state_);
}

} // namespace cubey::vulkan
