#include <cubey/vulkan/upload_step.h>

#include <cubey/vulkan/detail/upload_step_resources.h>
#include <cubey/vulkan/staging_pool.h>
#include <cubey/vulkan/vk_check.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cubey::vulkan {

class GpuUploadStepState : public std::enable_shared_from_this<GpuUploadStepState> {
  public:
    GpuUploadStepState(GpuOwnerContext& context, GpuUploadStepConfig config)
        : device_(&context.device()), runtime_(&context.runtime()),
          submission_(&context.submission()), pool_(&context.staging_pool()), config_(config),
          started_(Clock::now()) {
        context.require_owner_thread("GPU upload step creation requires the GPU owner thread");
        if (config_.max_staged_byte_size == 0 || config_.owner_cpu_target_milliseconds <= 0.0) {
            throw std::runtime_error("GPU upload step configuration is invalid");
        }
        resources_ = context.runtime().acquire_upload_step_resources_on_owner_thread();
    }

    ~GpuUploadStepState() = default;

    [[nodiscard]] VkCommandBuffer command_buffer() const noexcept {
        return resources_ == nullptr ? VK_NULL_HANDLE : resources_->command_buffer();
    }

    [[nodiscard]] std::optional<GpuUploadStagingSlice>
    try_stage_copy(GpuOwnerContext& context, std::span<const std::byte> bytes,
                   VkDeviceSize alignment) {
        context.require_owner_thread("GPU upload step staging requires the GPU owner thread");
        if (submitted_) {
            throw std::runtime_error("GPU upload step was already submitted");
        }
        if (bytes.empty()) {
            throw std::runtime_error("GPU upload step staging bytes must be nonempty");
        }
        if (bytes.size() > std::numeric_limits<VkDeviceSize>::max()) {
            return std::nullopt;
        }
        const VkDeviceSize byte_size = static_cast<VkDeviceSize>(bytes.size());
        {
            std::scoped_lock lock(mutex_);
            if (byte_size > config_.max_staged_byte_size - metrics_.staged_byte_size) {
                return std::nullopt;
            }
        }
        std::optional<GpuStagingReservation> reservation =
            pool_->try_reserve(context, byte_size, alignment);
        if (!reservation.has_value()) {
            return std::nullopt;
        }
        std::memcpy(reservation->mapped, bytes.data(), bytes.size());
        const GpuUploadStagingSlice slice{
            .buffer = reservation->buffer,
            .offset = reservation->offset,
            .byte_size = reservation->byte_size,
        };
        reservations_.push_back(std::move(reservation.value()));
        {
            std::scoped_lock lock(mutex_);
            metrics_.staged_byte_size += slice.byte_size;
        }
        return slice;
    }

    void add_copy_count(std::uint32_t copy_count) {
        std::scoped_lock lock(mutex_);
        if (copy_count > std::numeric_limits<std::uint32_t>::max() - metrics_.copy_count) {
            throw std::runtime_error("GPU upload step copy count overflows");
        }
        metrics_.copy_count += copy_count;
    }

    void submit(GpuOwnerContext& context, std::string label) {
        context.require_owner_thread("GPU upload step submission requires the GPU owner thread");
        if (submitted_) {
            throw std::runtime_error("GPU upload step was already submitted");
        }
        if (reservations_.empty()) {
            throw std::runtime_error("GPU upload step requires staged bytes before submission");
        }
        if (label.empty()) {
            label = "vkQueueSubmit upload step";
        }
        end_command_buffer(command_buffer(), "vkEndCommandBuffer upload step");
        try {
            submission_ticket_ = submission_->submit(
                {.command_buffers = {command_buffer()}, .fence = resources_->fence()},
                label.c_str());
        } catch (...) {
            recycle_unsubmitted_resources(context);
            throw;
        }
        submitted_ = true;
        submitted_at_ = Clock::now();
        try {
            pool_->commit_many(context, reservations_, submission_ticket_);
            {
                std::scoped_lock lock(mutex_);
                metrics_.owner_submit_milliseconds = elapsed_milliseconds(started_);
            }
            context.defer_destruction_after(submission_ticket_, [state = shared_from_this()] {
                state->release_after_completion_on_owner_thread();
            });
        } catch (...) {
            retire_submitted_after_failure(context);
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

    [[nodiscard]] GpuSubmissionTicket submission_ticket() const noexcept {
        return submission_ticket_;
    }

    [[nodiscard]] GpuUploadStepMetrics metrics() const {
        std::scoped_lock lock(mutex_);
        return metrics_;
    }

    void query_completion_on_owner(GpuOwnerContext& context, bool wait) {
        context.require_owner_thread(
            "GPU upload step completion query requires the GPU owner thread");
        if (&context.device() != device_ || &context.submission() != submission_) {
            throw std::runtime_error(
                "GPU upload step completion must use its creating GPU runtime");
        }
        if (complete() || failed()) {
            query_in_flight_.store(false, std::memory_order_release);
            return;
        }
        const VkFence fence = resources_->fence();
        const VkResult result =
            wait ? vkWaitForFences(device_->handle(), 1, &fence, VK_TRUE, UINT64_MAX)
                 : vkGetFenceStatus(device_->handle(), fence);
        if (result == VK_NOT_READY) {
            query_in_flight_.store(false, std::memory_order_release);
            return;
        }
        if (result != VK_SUCCESS) {
            {
                std::scoped_lock lock(mutex_);
                failure_message_ = "Vulkan upload step completion query failed (VkResult " +
                                   std::to_string(static_cast<int>(result)) + ")";
            }
            failed_.store(true, std::memory_order_release);
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

    void discard_unsubmitted(GpuOwnerContext& context) {
        context.require_owner_thread("GPU upload step discard requires the GPU owner thread");
        if (submitted_) {
            return;
        }
        for (GpuStagingReservation& reservation : reservations_) {
            pool_->cancel(context, reservation);
        }
        reservations_.clear();
        recycle_unsubmitted_resources(context);
    }

  private:
    using Clock = std::chrono::steady_clock;

    [[nodiscard]] static double elapsed_milliseconds(Clock::time_point started) {
        return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    }

    void retire_submitted_after_failure(GpuOwnerContext& context) noexcept {
        // commit_many() validates before it mutates. Therefore either every
        // reservation is attached to submission_ticket_, or every reservation
        // remains cancellable after this fence has signaled.
        const VkFence fence = resources_->fence();
        const VkResult wait_result =
            vkWaitForFences(device_->handle(), 1, &fence, VK_TRUE, UINT64_MAX);
        if (wait_result == VK_SUCCESS) {
            try {
                for (GpuStagingReservation& reservation : reservations_) {
                    pool_->cancel(context, reservation);
                }
                context.complete_submission(submission_ticket_);
            } catch (...) {
                record_retirement_failure("GPU upload step cleanup failed after submission");
            }
        } else {
            // A non-success fence wait means the Vulkan device can no longer
            // provide a safe completion observation. Treat it as terminal and
            // retire owner-held handles here, matching the device-loss policy
            // used by GpuUploadBatch completion queries.
            record_retirement_failure("Vulkan upload step cleanup fence wait failed (VkResult " +
                                      std::to_string(static_cast<int>(wait_result)) + ")");
        }
        release_after_completion_on_owner_thread();
    }

    void record_retirement_failure(std::string message) noexcept {
        try {
            std::scoped_lock lock(mutex_);
            failure_message_ = std::move(message);
        } catch (...) {
        }
        failed_.store(true, std::memory_order_release);
    }

    void release_after_completion_on_owner_thread() {
        if (complete()) {
            return;
        }
        {
            std::scoped_lock lock(mutex_);
            metrics_.completion_latency_milliseconds = elapsed_milliseconds(submitted_at_);
        }
        reservations_.clear();
        recycle_submitted_resources();
        complete_.store(true, std::memory_order_release);
        query_in_flight_.store(false, std::memory_order_release);
    }

    void recycle_unsubmitted_resources(GpuOwnerContext& context) {
        if (resources_ != nullptr) {
            context.runtime().recycle_upload_step_resources_on_owner_thread(std::move(resources_));
        }
    }

    void recycle_submitted_resources() {
        if (resources_ != nullptr) {
            runtime_->recycle_upload_step_resources_on_owner_thread(std::move(resources_));
        }
    }

    const Device* device_ = nullptr;
    GpuRuntime* runtime_ = nullptr;
    SubmissionCoordinator* submission_ = nullptr;
    GpuStagingPool* pool_ = nullptr;
    GpuUploadStepConfig config_{};
    std::unique_ptr<GpuUploadStepResources> resources_{};
    std::vector<GpuStagingReservation> reservations_{};
    GpuSubmissionTicket submission_ticket_{};
    mutable std::mutex mutex_;
    GpuUploadStepMetrics metrics_{};
    std::string failure_message_{};
    Clock::time_point started_{};
    Clock::time_point submitted_at_{};
    std::atomic<bool> submitted_{false};
    std::atomic<bool> complete_{false};
    std::atomic<bool> failed_{false};
    std::atomic<bool> query_in_flight_{false};
};

GpuUploadStepTicket::GpuUploadStepTicket(std::shared_ptr<GpuUploadStepState> state)
    : state_(std::move(state)) {}

bool GpuUploadStepTicket::valid() const noexcept {
    return state_ != nullptr;
}

bool GpuUploadStepTicket::complete() const noexcept {
    return state_ != nullptr && state_->complete();
}

bool GpuUploadStepTicket::failed() const noexcept {
    return state_ != nullptr && state_->failed();
}

std::string GpuUploadStepTicket::failure_message() const {
    return state_ == nullptr ? std::string{} : state_->failure_message();
}

GpuSubmissionTicket GpuUploadStepTicket::submission_ticket() const noexcept {
    return state_ == nullptr ? GpuSubmissionTicket{} : state_->submission_ticket();
}

GpuUploadStepMetrics GpuUploadStepTicket::metrics() const {
    return state_ == nullptr ? GpuUploadStepMetrics{} : state_->metrics();
}

bool GpuUploadStepTicket::poll(GpuRuntime& gpu) const {
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
        static_cast<void>(gpu.submit("poll GPU upload step completion",
                                     [state = state_](GpuOwnerContext& context) {
                                         state->query_completion_on_owner(context, false);
                                     }));
    } catch (...) {
        state_->cancel_query();
        throw;
    }
    return state_->complete();
}

void GpuUploadStepTicket::wait(GpuRuntime& gpu) const {
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
        .label = "wait for GPU upload step completion",
        .work = [state = state_](
                    GpuOwnerContext& context) { state->query_completion_on_owner(context, true); },
    }));
    if (state_->failed()) {
        throw std::runtime_error(state_->failure_message());
    }
}

GpuUploadStep::GpuUploadStep(GpuOwnerContext& context, GpuUploadStepConfig config)
    : owner_context_(&context), state_(std::make_shared<GpuUploadStepState>(context, config)) {}

GpuUploadStep::~GpuUploadStep() {
    if (state_ != nullptr) {
        state_->discard_unsubmitted(*owner_context_);
    }
}

VkCommandBuffer GpuUploadStep::command_buffer() const {
    if (state_ == nullptr) {
        throw std::runtime_error("GPU upload step has no state");
    }
    return state_->command_buffer();
}

std::optional<GpuUploadStagingSlice> GpuUploadStep::try_stage_copy(std::span<const std::byte> bytes,
                                                                   VkDeviceSize alignment) {
    if (state_ == nullptr) {
        throw std::runtime_error("GPU upload step has no state");
    }
    return state_->try_stage_copy(*owner_context_, bytes, alignment);
}

void GpuUploadStep::add_copy_count(std::uint32_t copy_count) {
    if (state_ == nullptr) {
        throw std::runtime_error("GPU upload step has no state");
    }
    owner_context_->require_owner_thread(
        "GPU upload step copy count requires the GPU owner thread");
    state_->add_copy_count(copy_count);
}

GpuUploadStepTicket GpuUploadStep::submit(GpuOwnerContext& context, std::string label) {
    if (state_ == nullptr) {
        throw std::runtime_error("GPU upload step has no state");
    }
    if (&context != owner_context_) {
        throw std::runtime_error("GPU upload step must submit through its creating owner context");
    }
    state_->submit(context, std::move(label));
    return GpuUploadStepTicket(state_);
}

} // namespace cubey::vulkan
