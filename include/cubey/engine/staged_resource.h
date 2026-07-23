#pragma once

#include <cubey/core/jobs.h>
#include <cubey/vulkan/gpu_runtime.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace cubey {

enum class StagedResourcePhase {
    Idle,
    Queued,
    Preparing,
    QueuedForGpu,
    Installing,
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
        };
        const StagedResourceGeneration generation = request.generation;
        ready_.reset();
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
        if (!active_.has_value()) {
            return launch_pending();
        }

        switch (active_->stage) {
        case ActiveStage::Preparing:
            return poll_preparation();
        case ActiveStage::ReadyForGpu:
            return submit_installation(gpu);
        case ActiveStage::Installing:
            return poll_installation();
        }
        return false;
    }

    void finish(vulkan::GpuRuntime& gpu) {
        while (active_.has_value() || pending_.has_value()) {
            wait_for_active_stage(gpu);
            static_cast<void>(poll(gpu));
        }
        if (status_.phase == StagedResourcePhase::Failed) {
            throw std::runtime_error(status_.error);
        }
    }

    void shutdown(vulkan::GpuRuntime& gpu) {
        if (!accepting_ && !active_.has_value()) {
            ready_.reset();
            return;
        }

        accepting_ = false;
        pending_.reset();
        ready_.reset();
        if (active_.has_value()) {
            active_->superseded = true;
            set_status(active_->generation, StagedResourcePhase::Superseded,
                       active_->prepare_milliseconds, active_->install_milliseconds);
        }
        while (active_.has_value()) {
            wait_for_active_stage(gpu);
            static_cast<void>(poll(gpu));
        }
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
        Result result = std::move(ready_.value());
        ready_.reset();
        return result;
    }

  private:
    using Clock = std::chrono::steady_clock;

    enum class ActiveStage {
        Preparing,
        ReadyForGpu,
        Installing,
    };

    struct Request {
        StagedResourceGeneration generation{};
        PrepareFunction prepare{};
        InstallFunction install{};
    };

    struct Active {
        StagedResourceGeneration generation{};
        InstallFunction install{};
        jobs::JobHandle<Prepared> preparation;
        std::optional<Prepared> prepared{};
        std::optional<vulkan::GpuJobHandle<Resident>> installation{};
        Clock::time_point prepare_started{};
        Clock::time_point install_started{};
        double prepare_milliseconds = 0.0;
        double install_milliseconds = 0.0;
        ActiveStage stage = ActiveStage::Preparing;
        bool superseded = false;

        Active(StagedResourceGeneration generation_value, InstallFunction install_value,
               jobs::JobHandle<Prepared> preparation_value, Clock::time_point started)
            : generation(std::move(generation_value)), install(std::move(install_value)),
              preparation(std::move(preparation_value)), prepare_started(started) {}
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
                        std::move(preparation), started);
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
            const std::string label = active_->generation.label + " GPU install";
            active_->install_started = Clock::now();
            active_->installation.emplace(
                gpu.submit(label, [prepared = std::move(prepared), install = std::move(install)](
                                      vulkan::GpuOwnerContext& owner) mutable {
                    return install(owner, std::move(prepared));
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
            Resident resident = active_->installation->get();
            active_->install_milliseconds = elapsed_milliseconds(active_->install_started);
            if (active_->superseded) {
                discard_active();
                return true;
            }
            const StagedResourceGeneration generation = active_->generation;
            const double prepare_milliseconds = active_->prepare_milliseconds;
            const double install_milliseconds = active_->install_milliseconds;
            active_.reset();
            ready_.emplace(Result{
                .generation = generation,
                .resident = std::move(resident),
                .prepare_milliseconds = prepare_milliseconds,
                .install_milliseconds = install_milliseconds,
            });
            set_status(generation, StagedResourcePhase::Ready, prepare_milliseconds,
                       install_milliseconds);
        } catch (const std::exception& error) {
            fail_active(error.what());
        } catch (...) {
            fail_active("unknown staged resource installation failure");
        }
        return true;
    }

    void fail_active(std::string error) {
        const StagedResourceGeneration generation = active_->generation;
        const double prepare_milliseconds = active_->prepare_milliseconds;
        const double install_milliseconds = active_->install_milliseconds;
        const bool superseded = active_->superseded;
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
        active_.reset();
        if (pending_.has_value()) {
            static_cast<void>(launch_pending());
        } else {
            set_status(generation, StagedResourcePhase::Superseded, prepare_milliseconds,
                       install_milliseconds);
        }
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
        }
    }

    jobs::JobSystem* jobs_ = nullptr;
    std::uint64_t next_generation_id_ = 1;
    std::optional<Active> active_{};
    std::optional<Request> pending_{};
    std::optional<Result> ready_{};
    StagedResourceStatus status_{};
    bool accepting_ = true;
};

} // namespace cubey
