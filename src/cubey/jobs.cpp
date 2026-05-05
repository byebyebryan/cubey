#include <cubey/jobs.h>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace cubey::jobs {
namespace {

[[nodiscard]] std::size_t normalize_worker_count(std::size_t requested) {
    if (requested != 0) {
        return requested;
    }

    const unsigned int hardware_threads = std::thread::hardware_concurrency();
    return std::max<std::size_t>(1, static_cast<std::size_t>(hardware_threads));
}

} // namespace

struct JobSystem::Impl {
    explicit Impl(std::size_t worker_count) {
        workers.reserve(worker_count);
        for (std::size_t index = 0; index < worker_count; ++index) {
            workers.emplace_back([this] { run_worker(); });
        }
    }

    void run_worker() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock lock(mutex);
                cv.wait(lock, [this] { return stopping || !tasks.empty(); });
                if (tasks.empty()) {
                    return;
                }

                task = std::move(tasks.front());
                tasks.pop_front();
            }

            task();
        }
    }

    std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::function<void()>> tasks;
    std::vector<std::jthread> workers;
    bool accepting = true;
    bool stopping = false;
};

JobSystem::JobSystem(std::size_t worker_count)
    : impl_(std::make_unique<Impl>(normalize_worker_count(worker_count))) {}

JobSystem::~JobSystem() {
    shutdown();
}

JobSystem::JobSystem(JobSystem&& other) noexcept = default;

JobSystem& JobSystem::operator=(JobSystem&& other) noexcept {
    if (this != &other) {
        shutdown();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

void JobSystem::shutdown() {
    if (impl_ == nullptr) {
        return;
    }

    {
        std::scoped_lock lock(impl_->mutex);
        impl_->accepting = false;
        impl_->stopping = true;
    }
    impl_->cv.notify_all();
    impl_->workers.clear();
}

void JobSystem::enqueue_task(std::function<void()> task) {
    if (impl_ == nullptr) {
        throw std::runtime_error("job system is shut down");
    }

    {
        std::scoped_lock lock(impl_->mutex);
        if (!impl_->accepting) {
            throw std::runtime_error("job system is shut down");
        }
        impl_->tasks.push_back(std::move(task));
    }
    impl_->cv.notify_one();
}

} // namespace cubey::jobs
