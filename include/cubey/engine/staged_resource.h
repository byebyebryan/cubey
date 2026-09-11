#pragma once

#include <cubey/core/jobs.h>
#include <cubey/vulkan/gpu_runtime.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace cubey {

enum class StagedResourcePhase {
    Idle,
    Queued,
    Preparing,
    QueuedForGpu,
    Installing,
    AwaitingGpu,
    Ready,
    Failed,
    Superseded,
};

[[nodiscard]] constexpr std::string_view
staged_resource_phase_name(StagedResourcePhase phase) noexcept {
    switch (phase) {
    case StagedResourcePhase::Idle:
        return "idle";
    case StagedResourcePhase::Queued:
        return "queued";
    case StagedResourcePhase::Preparing:
        return "preparing";
    case StagedResourcePhase::QueuedForGpu:
        return "queued-for-gpu";
    case StagedResourcePhase::Installing:
        return "installing";
    case StagedResourcePhase::AwaitingGpu:
        return "awaiting-gpu";
    case StagedResourcePhase::Ready:
        return "ready";
    case StagedResourcePhase::Failed:
        return "failed";
    case StagedResourcePhase::Superseded:
        return "superseded";
    }
    return "unknown";
}

struct StagedResourceGeneration {
    std::uint64_t id = 0;
    std::string label{};
};

struct StagedResourceStatus {
    StagedResourceGeneration generation{};
    StagedResourcePhase phase = StagedResourcePhase::Idle;
    double prepare_milliseconds = 0.0;
    double install_milliseconds = 0.0;
    std::string error{};
};

template <typename Resident> struct StagedResourceResult {
    StagedResourceGeneration generation{};
    Resident resident;
    double prepare_milliseconds = 0.0;
    double install_milliseconds = 0.0;
};

template <typename Prepared, typename Resident> class StagedResource {
  public:
    using PrepareFunction = std::function<Prepared()>;
    using InstallFunction = std::function<Resident(vulkan::GpuOwnerContext&, Prepared&&)>;
    using AwaitFunction = std::function<bool(vulkan::GpuRuntime&, Resident&, bool)>;
    using Result = StagedResourceResult<Resident>;

    explicit StagedResource(jobs::JobSystem& jobs) : jobs_(&jobs) {
        static_assert(!std::is_void_v<Prepared>, "staged resource preparation must return a value");
        static_assert(!std::is_void_v<Resident>,
                      "staged resource installation must return a value");
    }

    StagedResource(const StagedResource&) = delete;
    StagedResource& operator=(const StagedResource&) = delete;
    StagedResource(StagedResource&&) = delete;
    StagedResource& operator=(StagedResource&&) = delete;

    template <typename Prepare, typename Install>
    [[nodiscard]] StagedResourceGeneration request(std::string label, Prepare&& prepare,
                                                   Install&& install) {
        return request(std::move(label), std::forward<Prepare>(prepare),
                       std::forward<Install>(install), AwaitFunction{});
    }

    template <typename Prepare, typename Install, typename Await>
    [[nodiscard]] StagedResourceGeneration request(std::string label, Prepare&& prepare,
                                                   Install&& install, Await&& await) {
        if (!accepting_) {
            throw std::runtime_error("staged resource is shut down");
        }
        if (label.empty()) {
            throw std::runtime_error("staged resource request requires a label");
        }

        Request request{
            .generation = {.id = next_generation_id_++, .label = std::move(label)},
            .prepare = PrepareFunction(std::forward<Prepare>(prepare)),
            .install = InstallFunction(std::forward<Install>(install)),
            .await = AwaitFunction(std::forward<Await>(await)),
        };
        const StagedResourceGeneration generation = request.generation;
        queue_ready_for_disposal();
        if (active_.has_value()) {
            active_->superseded = true;
            pending_ = std::move(request);
            set_status(generation, StagedResourcePhase::Queued);
        } else {
            launch(std::move(request));
        }
        return generation;
    }

    [[nodiscard]] bool poll(vulkan::GpuRuntime& gpu) {
        const bool disposed = drain_discarded(gpu);
        if (!active_.has_value()) {
            return disposed || launch_pending();
        }

        bool progressed = false;
        switch (active_->stage) {
        case ActiveStage::Preparing:
            progressed = poll_preparation();
            break;
        case ActiveStage::ReadyForGpu:
            progressed = submit_installation(gpu);
            break;
        case ActiveStage::Installing:
            progressed = poll_installation();
            break;
        case ActiveStage::AwaitingGpu:
            progressed = poll_gpu_completion(gpu);
            break;
        }
        return disposed || progressed;
    }

    void finish(vulkan::GpuRuntime& gpu) {
        while (active_.has_value() || pending_.has_value()) {
            wait_for_active_stage(gpu);
            static_cast<void>(poll(gpu));
        }
        static_cast<void>(drain_discarded(gpu));
        if (status_.phase == StagedResourcePhase::Failed) {
            throw std::runtime_error(status_.error);
        }
    }

    void shutdown(vulkan::GpuRuntime& gpu) {
        accepting_ = false;
        pending_.reset();
        queue_ready_for_disposal();
        if (active_.has_value()) {
            active_->superseded = true;
            set_status(active_->generation, StagedResourcePhase::Superseded,
                       active_->prepare_milliseconds, active_->install_milliseconds);
        }
        while (active_.has_value()) {
            wait_for_active_stage(gpu);
            static_cast<void>(poll(gpu));
        }
        static_cast<void>(drain_discarded(gpu));
        ready_.reset();
    }

    [[nodiscard]] const StagedResourceStatus& status() const noexcept {
        return status_;
    }

    [[nodiscard]] bool busy() const noexcept {
        return active_.has_value() || pending_.has_value();
    }

    [[nodiscard]] bool ready() const noexcept {
        return ready_.has_value();
    }

    [[nodiscard]] bool accepting() const noexcept {
        return accepting_;
    }

    [[nodiscard]] Result take_ready() {
        if (!ready_.has_value()) {
            throw std::runtime_error("staged resource has no ready generation");
        }
        Ready ready = std::move(ready_.value());
        if (ready.resident == nullptr || !ready.resident->resident.has_value()) {
            throw std::runtime_error("staged resource ready generation has no resident");
        }
        Resident resident = std::move(ready.resident->resident.value());
        ready.resident->resident.reset();
        ready_.reset();
        return Result{
            .generation = std::move(ready.generation),
            .resident = std::move(resident),
            .prepare_milliseconds = ready.prepare_milliseconds,
            .install_milliseconds = ready.install_milliseconds,
        };
    }

  private:
    using Clock = std::chrono::steady_clock;

    enum class ActiveStage {
        Preparing,
        ReadyForGpu,
        Installing,
        AwaitingGpu,
    };

    struct Request {
        StagedResourceGeneration generation{};
        PrepareFunction prepare{};
        InstallFunction install{};
        AwaitFunction await{};
    };

    struct ResidentHolder {
        std::optional<Resident> resident{};
    };

    struct Ready {
        StagedResourceGeneration generation{};
        std::shared_ptr<ResidentHolder> resident{};
        double prepare_milliseconds = 0.0;
        double install_milliseconds = 0.0;
    };

    struct Active {
        StagedResourceGeneration generation{};
        InstallFunction install{};
        AwaitFunction await{};
        jobs::JobHandle<Prepared> preparation;
        std::optional<Prepared> prepared{};
        std::optional<vulkan::GpuJobHandle<void>> installation{};
        std::shared_ptr<ResidentHolder> resident{};
        Clock::time_point prepare_started{};
        Clock::time_point install_started{};
        double prepare_milliseconds = 0.0;
        double install_milliseconds = 0.0;
        ActiveStage stage = ActiveStage::Preparing;
        bool superseded = false;

        Active(StagedResourceGeneration generation_value, InstallFunction install_value,
               AwaitFunction await_value, jobs::JobHandle<Prepared> preparation_value,
               Clock::time_point started)
            : generation(std::move(generation_value)), install(std::move(install_value)),
              await(std::move(await_value)), preparation(std::move(preparation_value)),
              prepare_started(started) {}
    };

    [[nodiscard]] static double elapsed_milliseconds(Clock::time_point start) {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }

    void set_status(const StagedResourceGeneration& generation, StagedResourcePhase phase,
                    double prepare_milliseconds = 0.0, double install_milliseconds = 0.0,
                    std::string error = {}) {
        status_ = {
            .generation = generation,
            .phase = phase,
            .prepare_milliseconds = prepare_milliseconds,
            .install_milliseconds = install_milliseconds,
            .error = std::move(error),
        };
    }

    void set_active_status(StagedResourcePhase phase) {
        if (active_.has_value() && status_.generation.id == active_->generation.id) {
            set_status(active_->generation, phase, active_->prepare_milliseconds,
                       active_->install_milliseconds);
        }
    }

    void launch(Request request) {
        const Clock::time_point started = Clock::now();
        jobs::JobHandle<Prepared> preparation =
            jobs_->submit([prepare = std::move(request.prepare)]() mutable { return prepare(); });
        const StagedResourceGeneration generation = request.generation;
        active_.emplace(std::move(request.generation), std::move(request.install),
                        std::move(request.await), std::move(preparation), started);
        set_status(generation, StagedResourcePhase::Preparing);
    }

    [[nodiscard]] bool launch_pending() {
        if (!pending_.has_value()) {
            return false;
        }
        Request request = std::move(pending_.value());
        pending_.reset();
        launch(std::move(request));
        return true;
    }

    [[nodiscard]] bool poll_preparation() {
        if (!active_->preparation.ready()) {
            return false;
        }

        try {
            Prepared prepared = active_->preparation.get();
            active_->prepare_milliseconds = elapsed_milliseconds(active_->prepare_started);
            if (active_->superseded) {
                discard_active();
                return true;
            }
            active_->prepared.emplace(std::move(prepared));
            active_->stage = ActiveStage::ReadyForGpu;
            set_active_status(StagedResourcePhase::QueuedForGpu);
        } catch (const std::exception& error) {
            fail_active(error.what());
        } catch (...) {
            fail_active("unknown staged resource preparation failure");
        }
        return true;
    }

    [[nodiscard]] bool submit_installation(vulkan::GpuRuntime& gpu) {
        if (active_->superseded) {
            discard_active();
            return true;
        }

        try {
            Prepared prepared = std::move(active_->prepared.value());
            active_->prepared.reset();
            InstallFunction install = std::move(active_->install);
            const std::shared_ptr<ResidentHolder> resident = std::make_shared<ResidentHolder>();
            active_->resident = resident;
            const std::string label = active_->generation.label + " GPU install";
            active_->install_started = Clock::now();
            active_->installation.emplace(gpu.submit(
                label, [resident, prepared = std::move(prepared),
                        install = std::move(install)](vulkan::GpuOwnerContext& owner) mutable {
                    resident->resident.emplace(install(owner, std::move(prepared)));
                }));
            active_->stage = ActiveStage::Installing;
            set_active_status(StagedResourcePhase::Installing);
        } catch (const std::exception& error) {
            fail_active(error.what());
        } catch (...) {
            fail_active("unknown staged resource installation enqueue failure");
        }
        return true;
    }

    [[nodiscard]] bool poll_installation() {
        if (!active_->installation->ready()) {
            return false;
        }

        try {
            static_cast<void>(active_->installation->get());
            if (active_->resident == nullptr || !active_->resident->resident.has_value()) {
                throw std::runtime_error("GPU installation produced no resident");
            }
            if (active_->superseded) {
                // The installed product may contain a generation-scoped async
                // upload session. Discard it now instead of continuing to
                // advance an obsolete generation from later app polls.
                discard_active();
                return true;
            }
            if (active_->await) {
                active_->stage = ActiveStage::AwaitingGpu;
                set_active_status(StagedResourcePhase::AwaitingGpu);
                return true;
            }
            complete_active_resident();
        } catch (const std::exception& error) {
            fail_active(error.what());
        } catch (...) {
            fail_active("unknown staged resource installation failure");
        }
        return true;
    }

    [[nodiscard]] bool poll_gpu_completion(vulkan::GpuRuntime& gpu) {
        try {
            if (active_->resident == nullptr || !active_->resident->resident.has_value()) {
                throw std::runtime_error("GPU installation produced no resident");
            }
            if (active_->superseded) {
                discard_active();
                return true;
            }
            if (!active_->await(gpu, active_->resident->resident.value(), false)) {
                return false;
            }
            complete_active_resident();
        } catch (const std::exception& error) {
            fail_active(error.what());
        } catch (...) {
            fail_active("unknown staged resource GPU completion failure");
        }
        return true;
    }

    void complete_active_resident() {
        active_->install_milliseconds = elapsed_milliseconds(active_->install_started);
        if (active_->superseded) {
            discard_active();
            return;
        }
        const StagedResourceGeneration generation = active_->generation;
        const double prepare_milliseconds = active_->prepare_milliseconds;
        const double install_milliseconds = active_->install_milliseconds;
        std::shared_ptr<ResidentHolder> resident = std::move(active_->resident);
        active_.reset();
        ready_.emplace(Ready{
            .generation = generation,
            .resident = std::move(resident),
            .prepare_milliseconds = prepare_milliseconds,
            .install_milliseconds = install_milliseconds,
        });
        set_status(generation, StagedResourcePhase::Ready, prepare_milliseconds,
                   install_milliseconds);
    }

    void fail_active(std::string error) {
        const StagedResourceGeneration generation = active_->generation;
        const double prepare_milliseconds = active_->prepare_milliseconds;
        const double install_milliseconds = active_->install_milliseconds;
        const bool superseded = active_->superseded;
        queue_active_resident_for_disposal();
        active_.reset();
        if (pending_.has_value()) {
            static_cast<void>(launch_pending());
            return;
        }
        set_status(generation,
                   superseded ? StagedResourcePhase::Superseded : StagedResourcePhase::Failed,
                   prepare_milliseconds, install_milliseconds,
                   superseded ? std::string{} : std::move(error));
    }

    void discard_active() {
        const StagedResourceGeneration generation = active_->generation;
        const double prepare_milliseconds = active_->prepare_milliseconds;
        const double install_milliseconds = active_->install_milliseconds;
        queue_active_resident_for_disposal();
        active_.reset();
        if (pending_.has_value()) {
            static_cast<void>(launch_pending());
        } else {
            set_status(generation, StagedResourcePhase::Superseded, prepare_milliseconds,
                       install_milliseconds);
        }
    }

    void queue_ready_for_disposal() {
        if (!ready_.has_value()) {
            return;
        }
        if (ready_->resident != nullptr && ready_->resident->resident.has_value()) {
            discarded_.push_back(std::move(ready_->resident));
        }
        ready_.reset();
    }

    void queue_active_resident_for_disposal() {
        if (!active_.has_value() || active_->resident == nullptr) {
            return;
        }
        if (active_->resident->resident.has_value()) {
            discarded_.push_back(std::move(active_->resident));
        }
        active_->resident.reset();
    }

    [[nodiscard]] bool drain_discarded(vulkan::GpuRuntime& gpu) {
        if (discarded_.empty()) {
            return false;
        }

        std::vector<std::shared_ptr<ResidentHolder>> discarded = discarded_;
        static_cast<void>(gpu.submit_and_wait({
            .label = "discard staged GPU residents",
            .work =
                [discarded = std::move(discarded)](vulkan::GpuOwnerContext& owner) mutable {
                    owner.require_owner_thread("staged resident disposal requires the GPU owner");
                    for (const std::shared_ptr<ResidentHolder>& resident : discarded) {
                        if (resident != nullptr) {
                            resident->resident.reset();
                        }
                    }
                },
        }));
        discarded_.clear();
        return true;
    }

    void wait_for_active_stage(vulkan::GpuRuntime& gpu) {
        if (!active_.has_value()) {
            return;
        }
        switch (active_->stage) {
        case ActiveStage::Preparing:
            active_->preparation.wait();
            break;
        case ActiveStage::ReadyForGpu:
            break;
        case ActiveStage::Installing:
            if (gpu.execution_mode() == vulkan::GpuRuntimeExecutionMode::Inline) {
                if (!active_->installation->ready()) {
                    static_cast<void>(gpu.drain_inline());
                }
            } else {
                active_->installation->wait();
            }
            break;
        case ActiveStage::AwaitingGpu:
            if (active_->resident == nullptr || !active_->resident->resident.has_value()) {
                throw std::runtime_error("GPU completion wait has no resident");
            }
            try {
                if (!active_->await(gpu, active_->resident->resident.value(), true)) {
                    throw std::runtime_error("GPU completion wait returned incomplete");
                }
            } catch (const std::exception& error) {
                fail_active(error.what());
            } catch (...) {
                fail_active("unknown staged resource GPU completion wait failure");
            }
            break;
        }
    }

    jobs::JobSystem* jobs_ = nullptr;
    std::uint64_t next_generation_id_ = 1;
    std::optional<Active> active_{};
    std::optional<Request> pending_{};
    std::optional<Ready> ready_{};
    std::vector<std::shared_ptr<ResidentHolder>> discarded_{};
    StagedResourceStatus status_{};
    bool accepting_ = true;
};

} // namespace cubey
