#include <cubey/engine/gltf_scene_importer.h>

#include "gltf_scene_resident_builder.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace cubey {

struct GltfSceneUploadSession::Impl : public std::enable_shared_from_this<Impl> {
    enum class Phase {
        Recording,
        FinishedRecording,
        Complete,
        Failed,
        Abandoned,
    };

    explicit Impl(std::shared_ptr<const GltfPreparedScene> prepared, GltfSceneImportConfig config,
                  vulkan::GpuRuntime& runtime)
        : builder(
              std::make_unique<GltfSceneResidentBuilder>(std::move(prepared), std::move(config))) {
        if (!runtime.has_staging_pool()) {
            throw std::runtime_error("glTF upload session requires a configured GPU staging pool");
        }
        metrics_snapshot = builder->metrics();
    }

    void arm_owner_cleanup(vulkan::GpuRuntime& runtime) {
        const std::shared_ptr<Impl> self = shared_from_this();
        owner_cleanup = runtime.register_owner_cleanup(
            "retire abandoned glTF upload session", [self](vulkan::GpuOwnerContext& owner) {
                self->retire_abandoned_resident_on_owner_thread(owner);
            });
    }

    struct OwnerStepInFlightReset {
        explicit OwnerStepInFlightReset(std::atomic<bool>& state) : state_(&state) {}

        ~OwnerStepInFlightReset() {
            state_->store(false, std::memory_order_release);
        }

      private:
        std::atomic<bool>* state_ = nullptr;
    };

    void record_one_owner_step(vulkan::GpuOwnerContext& owner) {
        // Declare this before the lock so it publishes completion only after
        // the lock is released during normal returns and exception unwinding.
        OwnerStepInFlightReset reset_owner_step(owner_step_in_flight);
        owner.require_owner_thread("glTF upload session requires the GPU owner thread");
        std::unique_lock lock(mutex);
        if (abandoned || phase == Phase::Failed || phase == Phase::Complete ||
            phase == Phase::Abandoned || phase == Phase::FinishedRecording) {
            return;
        }
        backpressure_pending = false;
        try {
            if (builder == nullptr) {
                throw std::runtime_error("glTF upload session resident builder is missing");
            }
            GltfSceneResidentBuilder::AdvanceResult result = builder->advance(owner);
            metrics_snapshot = builder->metrics();
            if (result.submitted_ticket.has_value()) {
                // The builder records the ticket in its resident; the session
                // retains its own copy for polling and abandonment lifetime.
                final_ticket = std::move(result.submitted_ticket);
            }
            backpressure_pending = result.backpressured;
            if (result.finished_recording) {
                phase = Phase::FinishedRecording;
            }
        } catch (const std::exception& error) {
            if (builder != nullptr) {
                metrics_snapshot = builder->metrics();
            }
            fail(error.what());
        } catch (...) {
            if (builder != nullptr) {
                metrics_snapshot = builder->metrics();
            }
            fail("unknown glTF upload session owner-step failure");
        }
    }

    void fail(std::string message) {
        phase = Phase::Failed;
        failure = std::move(message);
    }

    void record_terminal_failure(const char* message) noexcept {
        try {
            std::scoped_lock lock(mutex);
            if (phase != Phase::Complete && phase != Phase::Abandoned) {
                fail(message == nullptr ? "unknown glTF upload session failure" : message);
            }
        } catch (...) {
            // The caller is already propagating a terminal error. Do not hide
            // it if recording the diagnostic itself runs out of memory.
        }
    }

    void mark_complete() {
        if (phase == Phase::Complete) {
            return;
        }
        if (builder == nullptr) {
            throw std::runtime_error("glTF upload session resident builder is missing");
        }
        builder->mark_complete();
        metrics_snapshot = builder->metrics();
        phase = Phase::Complete;
    }

    // A runtime-owned registration retains this implementation until an owner
    // callback can retire it. This lets a dropped session outlive its wrapper
    // without an arbitrary thread touching Vulkan or a raw runtime pointer.
    void abandon_from_any_thread() noexcept {
        {
            std::scoped_lock lock(mutex);
            if (taken || resident_released || retirement_scheduled) {
                return;
            }
            abandoned = true;
            retirement_scheduled = true;
        }
        owner_cleanup.request();
    }

    void retire_abandoned_resident_on_owner_thread(vulkan::GpuOwnerContext& owner) {
        owner.require_owner_thread("glTF abandoned resident disposal requires the GPU owner");
        std::optional<vulkan::GpuUploadStepTicket> ticket;
        {
            std::scoped_lock lock(mutex);
            if (taken || resident_released) {
                return;
            }
            abandoned = true;
            retirement_scheduled = true;
            ticket = final_ticket;
        }
        if (ticket.has_value() && !ticket->complete() && !ticket->failed()) {
            const std::shared_ptr<Impl> self = shared_from_this();
            owner.defer_destruction_after(ticket->submission_ticket(), [self] {
                self->release_abandoned_resident_on_owner_thread();
            });
            return;
        }
        release_abandoned_resident_on_owner_thread();
    }

    void release_abandoned_resident_on_owner_thread() {
        std::unique_ptr<GltfSceneResidentBuilder> released_builder;
        {
            std::scoped_lock lock(mutex);
            if (taken || resident_released) {
                return;
            }
            resident_released = true;
            phase = Phase::Abandoned;
            if (builder != nullptr) {
                metrics_snapshot = builder->metrics();
            }
            released_builder = std::move(builder);
        }
        owner_cleanup.reset();
        // This local deliberately destructs on the GPU owner after the lock
        // is released. It owns the resident plus any partly created images or
        // buffers which have not yet been adopted into it.
    }

    std::mutex mutex{};
    vulkan::GpuRuntimeOwnerCleanup owner_cleanup{};
    std::unique_ptr<GltfSceneResidentBuilder> builder{};
    GltfSceneUploadSessionMetrics metrics_snapshot{};
    Phase phase = Phase::Recording;
    std::string failure{};
    std::optional<vulkan::GpuUploadStepTicket> final_ticket{};
    bool backpressure_pending = false;
    struct QueuedOwnerStep {
        std::optional<vulkan::GpuJobHandle<void>> job{};
    };
    std::shared_ptr<QueuedOwnerStep> queued_owner_step{};
    std::atomic<bool> owner_step_in_flight{false};
    bool taken = false;
    bool abandoned = false;
    bool retirement_scheduled = false;
    bool resident_released = false;
};

GltfSceneUploadSession::GltfSceneUploadSession(std::shared_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

GltfSceneUploadSession::~GltfSceneUploadSession() {
    if (impl_ != nullptr) {
        impl_->abandon_from_any_thread();
    }
}

std::shared_ptr<GltfSceneUploadSession>
begin_gltf_scene_upload_session(std::shared_ptr<const GltfPreparedScene> prepared,
                                GltfSceneImportConfig config, vulkan::GpuRuntime& gpu) {
    const std::shared_ptr<GltfSceneUploadSession::Impl> impl =
        std::make_shared<GltfSceneUploadSession::Impl>(std::move(prepared), std::move(config), gpu);
    impl->arm_owner_cleanup(gpu);
    return std::shared_ptr<GltfSceneUploadSession>(new GltfSceneUploadSession(impl));
}

bool GltfSceneUploadSession::poll(vulkan::GpuRuntime& gpu, bool wait) {
    if (impl_ == nullptr) {
        throw std::runtime_error("glTF upload session has no implementation");
    }
    if (!impl_->owner_cleanup.belongs_to(gpu)) {
        throw std::runtime_error("glTF upload session must use its creating GPU runtime");
    }
    // The GPU owner holds Impl::mutex while it creates/records/submits one
    // indivisible builder advance. A windowed caller must never wait behind
    // that work in host.update: it can observe this acquire-load and retry
    // next frame.
    if (!wait && impl_->owner_step_in_flight.load(std::memory_order_acquire)) {
        return false;
    }
    try {
        for (;;) {
            std::unique_lock lock(impl_->mutex);
            if (impl_->phase == Impl::Phase::Failed) {
                // The session destructor queues owner-only deferred disposal.
                // A windowed poll reports the failure immediately; only a
                // headless finish waits for the final submitted ticket first.
                const std::string failure = impl_->failure;
                const std::optional<vulkan::GpuUploadStepTicket> final_ticket = impl_->final_ticket;
                lock.unlock();
                if (wait && final_ticket.has_value()) {
                    final_ticket->wait(gpu);
                }
                throw std::runtime_error(failure);
            }
            if (impl_->phase == Impl::Phase::Abandoned) {
                throw std::runtime_error("glTF upload session was abandoned");
            }
            if (impl_->phase == Impl::Phase::Complete) {
                return true;
            }
            if (impl_->queued_owner_step != nullptr) {
                // Retain a stable shared handle before dropping Impl::mutex to
                // wait. A concurrent poll may harvest/reset the session field,
                // but cannot invalidate this job object.
                const std::shared_ptr<Impl::QueuedOwnerStep> queued = impl_->queued_owner_step;
                if (!queued->job.has_value()) {
                    throw std::runtime_error("glTF upload session owner job is invalid");
                }
                if (!queued->job->ready()) {
                    if (!wait) {
                        return false;
                    }
                    lock.unlock();
                    queued->job->wait();
                    continue;
                }
                try {
                    queued->job->get();
                } catch (const std::exception& error) {
                    impl_->fail(error.what());
                } catch (...) {
                    impl_->fail("unknown glTF owner-step failure");
                }
                if (impl_->queued_owner_step == queued) {
                    impl_->queued_owner_step.reset();
                }
                const bool backpressured = impl_->backpressure_pending;
                const std::optional<vulkan::GpuUploadStepTicket> backpressure_ticket =
                    impl_->final_ticket;
                impl_->backpressure_pending = false;
                if (backpressured) {
                    if (!wait) {
                        // The graphics queue advances on the later app frame.
                        // Consume this edge and retry on exactly the next poll.
                        return false;
                    }
                    lock.unlock();
                    if (!backpressure_ticket.has_value()) {
                        throw std::runtime_error(
                            "glTF upload session exhausted staging before its first submission");
                    }
                    backpressure_ticket->wait(gpu);
                    continue;
                }
                if (impl_->phase == Impl::Phase::Failed) {
                    const std::string failure = impl_->failure;
                    lock.unlock();
                    throw std::runtime_error(failure);
                }
                // A completed owner job is not an enqueue performed by this
                // poll. Continue so a non-wait poll can submit at most one
                // successor advance without waiting for the GPU owner.
                continue;
            }
            if (impl_->phase == Impl::Phase::FinishedRecording) {
                if (!impl_->final_ticket.has_value()) {
                    impl_->mark_complete();
                    return true;
                }
                vulkan::GpuUploadStepTicket ticket = *impl_->final_ticket;
                lock.unlock();
                const bool complete = wait ? (ticket.wait(gpu), true) : ticket.poll(gpu);
                if (!complete) {
                    return false;
                }
                lock.lock();
                impl_->mark_complete();
                return true;
            }
            if (wait) {
                const std::shared_ptr<Impl> impl = impl_;
                impl_->owner_step_in_flight.store(true, std::memory_order_release);
                lock.unlock();
                try {
                    static_cast<void>(gpu.submit_and_wait({
                        .label = "advance glTF upload session",
                        .work =
                            [impl](vulkan::GpuOwnerContext& owner) {
                                impl->record_one_owner_step(owner);
                            },
                    }));
                } catch (...) {
                    impl_->owner_step_in_flight.store(false, std::memory_order_release);
                    throw;
                }
                lock.lock();
                if (impl_->backpressure_pending) {
                    const std::optional<vulkan::GpuUploadStepTicket> ticket = impl_->final_ticket;
                    impl_->backpressure_pending = false;
                    lock.unlock();
                    if (!ticket.has_value()) {
                        throw std::runtime_error(
                            "glTF upload session exhausted staging before its first submission");
                    }
                    // Headless startup has no later frame submission to
                    // advance the graphics-queue completion watermark. Retire
                    // the latest same-queue step before retrying the bounded
                    // pool.
                    ticket->wait(gpu);
                } else {
                    lock.unlock();
                }
                continue;
            }
            const std::shared_ptr<Impl> impl = impl_;
            const std::shared_ptr<Impl::QueuedOwnerStep> queued =
                std::make_shared<Impl::QueuedOwnerStep>();
            impl_->owner_step_in_flight.store(true, std::memory_order_release);
            try {
                queued->job.emplace(gpu.submit("advance glTF upload session",
                                               [impl](vulkan::GpuOwnerContext& owner) {
                                                   impl->record_one_owner_step(owner);
                                               }));
                impl_->queued_owner_step = queued;
            } catch (...) {
                impl_->owner_step_in_flight.store(false, std::memory_order_release);
                throw;
            }
            return false;
        }
    } catch (const std::exception& error) {
        impl_->record_terminal_failure(error.what());
        throw;
    } catch (...) {
        impl_->record_terminal_failure("unknown glTF upload session failure");
        throw;
    }
}

bool GltfSceneUploadSession::complete() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->phase == Impl::Phase::Complete;
}

bool GltfSceneUploadSession::failed() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->phase == Impl::Phase::Failed;
}

std::string GltfSceneUploadSession::failure_message() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->failure;
}

GltfSceneUploadSessionMetrics GltfSceneUploadSession::metrics() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->metrics_snapshot;
}

GltfSceneResident GltfSceneUploadSession::take_resident() {
    GltfSceneResident resident;
    {
        std::scoped_lock lock(impl_->mutex);
        if (impl_->phase != Impl::Phase::Complete) {
            throw std::runtime_error("glTF upload session resident is not GPU complete");
        }
        if (impl_->taken) {
            throw std::runtime_error("glTF upload session resident was already taken");
        }
        if (impl_->builder == nullptr) {
            throw std::runtime_error("glTF upload session resident builder is missing");
        }
        impl_->taken = true;
        resident = impl_->builder->take_resident();
    }
    // The resident is now owned by the caller; release the runtime's strong
    // session-retention action so the successful path cannot form a cycle.
    impl_->owner_cleanup.reset();
    return resident;
}

} // namespace cubey
