#include <cubey/jobs.h>

#include <atomic>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_inline_executor_runs_jobs_immediately() {
    cubey::jobs::InlineExecutor executor;

    cubey::jobs::JobHandle<int> job = executor.submit([] { return 42; });

    require(job.ready(), "inline executor should complete jobs before returning");
    require(job.get() == 42, "job handle should return callable result");
}

void test_job_system_runs_jobs_and_propagates_errors() {
    cubey::jobs::JobSystem jobs(2);
    std::atomic<int> counter = 0;

    auto first = jobs.submit([&counter] {
        counter.fetch_add(1);
        return 7;
    });
    auto second = jobs.submit([&counter] {
        counter.fetch_add(1);
        throw std::runtime_error("job failed");
    });

    require(first.get() == 7, "job system should return worker result");

    bool propagated = false;
    try {
        second.get();
    } catch (const std::runtime_error& error) {
        propagated = std::string{error.what()} == "job failed";
    }

    require(propagated, "job handle should propagate worker exceptions");
    require(counter.load() == 2, "job system should run accepted jobs");
}

void test_job_system_shutdown_rejects_new_jobs() {
    cubey::jobs::JobSystem jobs(1);

    auto accepted = jobs.submit([] { return 3; });
    jobs.shutdown();

    require(accepted.get() == 3, "shutdown should let accepted jobs complete");

    bool rejected = false;
    try {
        static_cast<void>(jobs.submit([] { return 4; }));
    } catch (const std::runtime_error&) {
        rejected = true;
    }

    require(rejected, "shutdown should reject new jobs");
}
