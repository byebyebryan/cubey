#include <cubey/engine/staged_resource.h>

#include <vulkan/vulkan.h>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
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

struct ResidentTrackerState {
    std::mutex mutex;
    std::condition_variable changed;
    std::thread::id installed_thread{};
    std::thread::id destroyed_thread{};
    bool installed = false;
    bool destroyed = false;
};

class MoveOnlyResidentTracker {
  public:
    explicit MoveOnlyResidentTracker(std::shared_ptr<ResidentTrackerState> state)
        : state_(std::move(state)) {}

    MoveOnlyResidentTracker(const MoveOnlyResidentTracker&) = delete;
    MoveOnlyResidentTracker& operator=(const MoveOnlyResidentTracker&) = delete;

    MoveOnlyResidentTracker(MoveOnlyResidentTracker&& other) noexcept
        : state_(std::move(other.state_)),
          owns_resident_(std::exchange(other.owns_resident_, false)) {}

    MoveOnlyResidentTracker& operator=(MoveOnlyResidentTracker&& other) noexcept {
        if (this != &other) {
            release();
            state_ = std::move(other.state_);
            owns_resident_ = std::exchange(other.owns_resident_, false);
        }
        return *this;
    }

    ~MoveOnlyResidentTracker() {
        release();
    }

  private:
    void release() {
        if (!owns_resident_ || state_ == nullptr) {
            return;
        }
        std::scoped_lock lock(state_->mutex);
        state_->destroyed = true;
        state_->destroyed_thread = std::this_thread::get_id();
        state_->changed.notify_all();
        owns_resident_ = false;
    }

    std::shared_ptr<ResidentTrackerState> state_;
    bool owns_resident_ = true;
};

} // namespace

void test_staged_resource_finishes_owned_cpu_and_gpu_stages() {
    cubey::jobs::JobSystem jobs(1);
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime gpu({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });
    cubey::StagedResource<std::string, std::size_t> resource(jobs);

    const cubey::StagedResourceGeneration generation = resource.request(
        "owned resource", [] { return std::string("prepared"); },
        [](cubey::vulkan::GpuOwnerContext& owner, std::string&& prepared) {
            require(owner.is_owner_thread(), "staged install should execute on the GPU owner");
            return prepared.size();
        });
    require(generation.id == 1 && generation.label == "owned resource",
            "staged resource should issue a labeled generation");

    resource.finish(gpu);
    require(resource.ready(), "finished staged resource should publish one complete result");
    require(resource.status().phase == cubey::StagedResourcePhase::Ready,
            "finished staged resource should report ready");
    cubey::StagedResourceResult<std::size_t> result = resource.take_ready();
    require(result.generation.id == generation.id, "staged result should preserve its generation");
    require(result.resident == std::string("prepared").size(),
            "staged result should carry the installed resident value");
    require(result.prepare_milliseconds >= 0.0 && result.install_milliseconds >= 0.0,
            "staged result should report phase timings");
}

void test_staged_resource_poll_does_not_wait_for_cpu_preparation() {
    cubey::jobs::JobSystem jobs(1);
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime gpu({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });
    cubey::StagedResource<int, int> resource(jobs);
    std::mutex mutex;
    std::condition_variable started_cv;
    std::condition_variable release_cv;
    bool started = false;
    bool released = false;

    static_cast<void>(resource.request(
        "blocked preparation",
        [&] {
            std::unique_lock lock(mutex);
            started = true;
            started_cv.notify_one();
            release_cv.wait(lock, [&] { return released; });
            return 4;
        },
        [](cubey::vulkan::GpuOwnerContext&, int&& prepared) { return prepared * 2; }));
    {
        std::unique_lock lock(mutex);
        started_cv.wait(lock, [&] { return started; });
    }

    require(!resource.poll(gpu), "poll should return while CPU preparation is still pending");
    require(resource.status().phase == cubey::StagedResourcePhase::Preparing,
            "pending CPU work should remain in the preparing phase");
    {
        std::scoped_lock lock(mutex);
        released = true;
    }
    release_cv.notify_one();
    resource.finish(gpu);
    require(resource.take_ready().resident == 8,
            "resource should remain finishable after a nonblocking poll");
}

void test_staged_resource_awaits_gpu_completion_before_activation() {
    cubey::jobs::JobSystem jobs(1);
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime gpu({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });
    cubey::StagedResource<int, int> resource(jobs);
    bool gpu_complete = false;
    std::size_t poll_count = 0U;

    static_cast<void>(resource.request(
        "await completion", [] { return 9; },
        [](cubey::vulkan::GpuOwnerContext& owner, int&& prepared) {
            require(owner.is_owner_thread(), "installation should use the GPU owner");
            return prepared * 2;
        },
        [&gpu_complete, &poll_count](cubey::vulkan::GpuRuntime&, int& resident, bool wait) {
            require(resident == 18, "await callback should observe the installed resident");
            if (wait) {
                gpu_complete = true;
                return true;
            }
            ++poll_count;
            return gpu_complete;
        }));

    while (resource.status().phase != cubey::StagedResourcePhase::AwaitingGpu) {
        static_cast<void>(resource.poll(gpu));
        static_cast<void>(gpu.drain_inline());
    }
    require(!resource.ready(), "owner callback completion must not activate the resident");
    require(!resource.poll(gpu) && poll_count > 0U,
            "nonblocking completion polling must retain an unfinished resident");
    require(resource.status().phase == cubey::StagedResourcePhase::AwaitingGpu,
            "unfinished GPU work should remain in the explicit awaiting state");

    gpu_complete = true;
    static_cast<void>(resource.poll(gpu));
    require(resource.ready(), "resident should activate only after GPU completion reports ready");
    require(resource.take_ready().resident == 18,
            "completed resident should preserve its installed value");
}

void test_staged_resource_supersession_waits_for_gpu_completion_before_disposal() {
    cubey::jobs::JobSystem jobs(1);
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime gpu({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });
    cubey::StagedResource<int, MoveOnlyResidentTracker> resource(jobs);
    const auto original = std::make_shared<ResidentTrackerState>();
    const auto replacement = std::make_shared<ResidentTrackerState>();
    bool gpu_complete = false;

    static_cast<void>(resource.request(
        "first awaiting generation", [] { return 1; },
        [original](cubey::vulkan::GpuOwnerContext&, int&&) {
            return MoveOnlyResidentTracker(original);
        },
        [&gpu_complete](cubey::vulkan::GpuRuntime&, MoveOnlyResidentTracker&, bool wait) {
            if (wait) {
                gpu_complete = true;
                return true;
            }
            return gpu_complete;
        }));
    while (resource.status().phase != cubey::StagedResourcePhase::AwaitingGpu) {
        static_cast<void>(resource.poll(gpu));
        static_cast<void>(gpu.drain_inline());
    }

    static_cast<void>(resource.request(
        "replacement", [] { return 2; },
        [replacement](cubey::vulkan::GpuOwnerContext&, int&&) {
            return MoveOnlyResidentTracker(replacement);
        }));
    static_cast<void>(resource.poll(gpu));
    {
        std::scoped_lock lock(original->mutex);
        require(!original->destroyed,
                "supersession must retain an in-flight resident until its GPU completion");
    }

    gpu_complete = true;
    resource.finish(gpu);
    {
        std::scoped_lock lock(original->mutex);
        require(original->destroyed,
                "completed superseded resident should be retired after its GPU completion");
    }
    require(resource.ready() && resource.take_ready().generation.label == "replacement",
            "the replacement should activate after the superseded upload is safely retired");
}

void test_staged_resource_shutdown_waits_for_gpu_completion_before_disposal() {
    cubey::jobs::JobSystem jobs(1);
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime gpu({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });
    cubey::StagedResource<int, MoveOnlyResidentTracker> resource(jobs);
    const auto state = std::make_shared<ResidentTrackerState>();
    bool wait_called = false;

    static_cast<void>(resource.request(
        "shutdown awaiting generation", [] { return 1; },
        [state](cubey::vulkan::GpuOwnerContext&, int&&) { return MoveOnlyResidentTracker(state); },
        [&wait_called](cubey::vulkan::GpuRuntime&, MoveOnlyResidentTracker&, bool wait) {
            require(!wait_called || wait,
                    "nonblocking completion polling should not report an unfinished upload ready");
            if (!wait) {
                return false;
            }
            wait_called = true;
            return true;
        }));
    while (resource.status().phase != cubey::StagedResourcePhase::AwaitingGpu) {
        static_cast<void>(resource.poll(gpu));
        static_cast<void>(gpu.drain_inline());
    }

    resource.shutdown(gpu);
    {
        std::scoped_lock lock(state->mutex);
        require(wait_called, "shutdown must use the blocking GPU completion path for AwaitingGpu");
        require(state->destroyed,
                "shutdown must dispose an awaiting resident after its completion wait");
    }
    require(!resource.accepting() && !resource.busy() && !resource.ready(),
            "shutdown should leave no awaiting generation available for activation");
    bool rejected = false;
    try {
        static_cast<void>(resource.request(
            "after awaiting shutdown", [] { return 2; },
            [](cubey::vulkan::GpuOwnerContext&, int&&) {
                return MoveOnlyResidentTracker(nullptr);
            }));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "shutdown must reject later requests after awaiting GPU completion");
}

void test_staged_resource_keeps_only_latest_pending_generation() {
    cubey::jobs::JobSystem jobs(1);
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime gpu({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });
    cubey::StagedResource<int, int> resource(jobs);
    std::mutex mutex;
    std::condition_variable started_cv;
    std::condition_variable release_cv;
    bool started = false;
    bool released = false;
    int second_preparations = 0;

    static_cast<void>(resource.request(
        "first",
        [&] {
            std::unique_lock lock(mutex);
            started = true;
            started_cv.notify_one();
            release_cv.wait(lock, [&] { return released; });
            return 1;
        },
        [](cubey::vulkan::GpuOwnerContext&, int&& prepared) { return prepared; }));
    {
        std::unique_lock lock(mutex);
        started_cv.wait(lock, [&] { return started; });
    }
    static_cast<void>(resource.request(
        "second", [&] { return ++second_preparations; },
        [](cubey::vulkan::GpuOwnerContext&, int&& prepared) { return prepared; }));
    const cubey::StagedResourceGeneration latest = resource.request(
        "third", [] { return 3; },
        [](cubey::vulkan::GpuOwnerContext&, int&& prepared) { return prepared * 10; });
    require(resource.status().phase == cubey::StagedResourcePhase::Queued,
            "latest request should wait without launching unbounded work");
    require(resource.status().generation.id == latest.id,
            "queued status should describe the latest request");
    {
        std::scoped_lock lock(mutex);
        released = true;
    }
    release_cv.notify_one();

    resource.finish(gpu);
    cubey::StagedResourceResult<int> result = resource.take_ready();
    require(result.generation.id == latest.id && result.resident == 30,
            "only the latest pending generation should become resident");
    require(second_preparations == 0, "replaced pending generation should never consume a worker");
}

void test_staged_resource_discards_replaced_ready_resident_on_gpu_owner() {
    cubey::jobs::JobSystem jobs(1);
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime gpu({
        .device = fake_device(),
        .submission = &submission,
    });
    cubey::StagedResource<int, MoveOnlyResidentTracker> resource(jobs);
    const auto state = std::make_shared<ResidentTrackerState>();
    const auto replacement_state = std::make_shared<ResidentTrackerState>();

    static_cast<void>(resource.request(
        "tracked ready", [] { return 1; },
        [state](cubey::vulkan::GpuOwnerContext& owner, int&&) {
            std::scoped_lock lock(state->mutex);
            state->installed = true;
            state->installed_thread = std::this_thread::get_id();
            require(owner.is_owner_thread(), "tracked resident should install on the GPU owner");
            return MoveOnlyResidentTracker(state);
        }));
    resource.finish(gpu);
    require(resource.ready(), "tracked generation should become ready before replacement");

    const std::thread::id caller_thread = std::this_thread::get_id();
    static_cast<void>(resource.request(
        "replacement", [] { return 2; },
        [replacement_state](cubey::vulkan::GpuOwnerContext&, int&&) {
            return MoveOnlyResidentTracker(replacement_state);
        }));
    {
        std::scoped_lock lock(state->mutex);
        require(state->installed, "tracked generation should record its install thread");
        require(!state->destroyed,
                "replacing a ready generation should defer resident destruction until poll");
    }

    static_cast<void>(resource.poll(gpu));
    {
        std::scoped_lock lock(state->mutex);
        require(state->destroyed, "poll should dispose a replaced resident");
        require(state->destroyed_thread == state->installed_thread,
                "replaced resident should be destroyed on the GPU owner thread");
        require(state->destroyed_thread != caller_thread,
                "threaded GPU disposal should not run on the polling caller");
    }
    resource.shutdown(gpu);
}

void test_staged_resource_shutdown_discards_ready_resident_on_gpu_owner() {
    cubey::jobs::JobSystem jobs(1);
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime gpu({
        .device = fake_device(),
        .submission = &submission,
    });
    cubey::StagedResource<int, MoveOnlyResidentTracker> resource(jobs);
    const auto state = std::make_shared<ResidentTrackerState>();

    static_cast<void>(resource.request(
        "shutdown tracked", [] { return 3; },
        [state](cubey::vulkan::GpuOwnerContext& owner, int&&) {
            std::scoped_lock lock(state->mutex);
            state->installed = true;
            state->installed_thread = std::this_thread::get_id();
            require(owner.is_owner_thread(), "tracked resident should install on the GPU owner");
            return MoveOnlyResidentTracker(state);
        }));
    resource.finish(gpu);
    require(resource.ready(), "shutdown test generation should become ready");

    resource.shutdown(gpu);
    {
        std::scoped_lock lock(state->mutex);
        require(state->destroyed, "shutdown should dispose a ready resident before returning");
        require(state->destroyed_thread == state->installed_thread,
                "shutdown resident disposal should run on the GPU owner thread");
    }
    require(!resource.ready(), "shutdown should remove the discarded ready generation");
}

void test_staged_resource_shutdown_discards_superseded_install_on_gpu_owner() {
    cubey::jobs::JobSystem jobs(1);
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime gpu({
        .device = fake_device(),
        .submission = &submission,
    });
    cubey::StagedResource<int, MoveOnlyResidentTracker> resource(jobs);
    const auto state = std::make_shared<ResidentTrackerState>();
    const auto replacement_state = std::make_shared<ResidentTrackerState>();
    std::mutex mutex;
    std::condition_variable install_started_cv;
    std::condition_variable release_install_cv;
    bool install_started = false;
    bool release_install = false;

    static_cast<void>(resource.request(
        "shutdown superseded", [] { return 4; },
        [state, &mutex, &install_started_cv, &release_install_cv, &install_started,
         &release_install](cubey::vulkan::GpuOwnerContext& owner, int&&) {
            {
                std::scoped_lock lock(mutex);
                install_started = true;
                state->installed = true;
                state->installed_thread = std::this_thread::get_id();
            }
            install_started_cv.notify_one();
            {
                std::unique_lock lock(mutex);
                release_install_cv.wait(lock, [&] { return release_install; });
            }
            require(owner.is_owner_thread(), "tracked resident should install on the GPU owner");
            return MoveOnlyResidentTracker(state);
        }));

    while (resource.status().phase != cubey::StagedResourcePhase::Installing) {
        static_cast<void>(resource.poll(gpu));
    }
    {
        std::unique_lock lock(mutex);
        install_started_cv.wait(lock, [&] { return install_started; });
    }

    static_cast<void>(resource.request(
        "shutdown replacement", [] { return 5; },
        [replacement_state](cubey::vulkan::GpuOwnerContext&, int&&) {
            return MoveOnlyResidentTracker(replacement_state);
        }));
    {
        std::scoped_lock lock(mutex);
        release_install = true;
    }
    release_install_cv.notify_one();

    resource.shutdown(gpu);
    {
        std::scoped_lock lock(state->mutex);
        require(state->destroyed,
                "shutdown should dispose an installed superseded resident before returning");
        require(state->destroyed_thread == state->installed_thread,
                "superseded resident disposal should run on the GPU owner thread");
    }
}

void test_staged_resource_reports_prepare_and_install_failures() {
    cubey::jobs::JobSystem jobs(1);
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime gpu({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });

    cubey::StagedResource<int, int> prepare_failure(jobs);
    static_cast<void>(prepare_failure.request(
        "prepare failure", []() -> int { throw std::runtime_error("prepare failed"); },
        [](cubey::vulkan::GpuOwnerContext&, int&& prepared) { return prepared; }));
    bool prepare_propagated = false;
    try {
        prepare_failure.finish(gpu);
    } catch (const std::runtime_error& error) {
        prepare_propagated = std::string(error.what()) == "prepare failed";
    }
    require(prepare_propagated &&
                prepare_failure.status().phase == cubey::StagedResourcePhase::Failed,
            "prepare failure should be reported through status and finish");

    cubey::StagedResource<int, int> install_failure(jobs);
    static_cast<void>(install_failure.request(
        "install failure", [] { return 5; },
        [](cubey::vulkan::GpuOwnerContext&, int&&) -> int {
            throw std::runtime_error("install failed");
        }));
    bool install_propagated = false;
    try {
        install_failure.finish(gpu);
    } catch (const std::runtime_error& error) {
        install_propagated = std::string(error.what()) == "install failed";
    }
    require(install_propagated &&
                install_failure.status().phase == cubey::StagedResourcePhase::Failed,
            "install failure should be reported through status and finish");

    cubey::StagedResource<int, int> completion_failure(jobs);
    static_cast<void>(completion_failure.request(
        "completion failure", [] { return 7; },
        [](cubey::vulkan::GpuOwnerContext&, int&& prepared) { return prepared; },
        [](cubey::vulkan::GpuRuntime&, int&, bool) -> bool {
            throw std::runtime_error("completion failed");
        }));
    bool completion_propagated = false;
    try {
        completion_failure.finish(gpu);
    } catch (const std::runtime_error& error) {
        completion_propagated = std::string(error.what()) == "completion failed";
    }
    require(completion_propagated &&
                completion_failure.status().phase == cubey::StagedResourcePhase::Failed,
            "GPU completion failure should preserve the staged failure contract");
}

void test_staged_resource_shutdown_discards_work_and_rejects_requests() {
    cubey::jobs::JobSystem jobs(1);
    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime gpu({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });
    cubey::StagedResource<int, int> resource(jobs);
    static_cast<void>(resource.request(
        "shutdown", [] { return 9; },
        [](cubey::vulkan::GpuOwnerContext&, int&& prepared) { return prepared; }));

    resource.shutdown(gpu);
    require(!resource.accepting() && !resource.busy() && !resource.ready(),
            "shutdown should leave no accepted or published staged work");
    require(resource.status().phase == cubey::StagedResourcePhase::Superseded,
            "shutdown should identify discarded active work as superseded");
    bool rejected = false;
    try {
        static_cast<void>(resource.request(
            "after shutdown", [] { return 1; },
            [](cubey::vulkan::GpuOwnerContext&, int&& prepared) { return prepared; }));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "shutdown staged resource should reject new requests");
}
