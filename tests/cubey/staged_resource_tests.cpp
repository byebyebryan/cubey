#include <cubey/engine/staged_resource.h>

#include <vulkan/vulkan.h>

#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

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
