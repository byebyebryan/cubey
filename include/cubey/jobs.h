#pragma once

#include <chrono>
#include <cstddef>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace cubey::jobs {

template <typename T>
class JobHandle {
public:
    explicit JobHandle(std::future<T> future) : future_(std::move(future)) {}

    JobHandle(const JobHandle&) = delete;
    JobHandle& operator=(const JobHandle&) = delete;
    JobHandle(JobHandle&&) noexcept = default;
    JobHandle& operator=(JobHandle&&) noexcept = default;

    [[nodiscard]] bool ready() const {
        return future_.wait_for(std::chrono::seconds{0}) == std::future_status::ready;
    }

    void wait() const {
        future_.wait();
    }

    T get() {
        return future_.get();
    }

private:
    mutable std::future<T> future_;
};

class InlineExecutor {
public:
    InlineExecutor() = default;

    InlineExecutor(const InlineExecutor&) = delete;
    InlineExecutor& operator=(const InlineExecutor&) = delete;
    InlineExecutor(InlineExecutor&&) noexcept = default;
    InlineExecutor& operator=(InlineExecutor&&) noexcept = default;

    template <typename Function>
    [[nodiscard]] auto submit(Function&& function)
        -> JobHandle<std::invoke_result_t<std::decay_t<Function>&>> {
        if (!accepting_) {
            throw std::runtime_error("job executor is shut down");
        }

        using Callable = std::decay_t<Function>;
        using Result = std::invoke_result_t<Callable&>;
        Callable callable(std::forward<Function>(function));
        std::promise<Result> promise;
        std::future<Result> future = promise.get_future();

        try {
            if constexpr (std::is_void_v<Result>) {
                std::invoke(callable);
                promise.set_value();
            } else {
                promise.set_value(std::invoke(callable));
            }
        } catch (...) {
            promise.set_exception(std::current_exception());
        }

        return JobHandle<Result>(std::move(future));
    }

    void shutdown() {
        accepting_ = false;
    }

private:
    bool accepting_ = true;
};

class JobSystem {
public:
    explicit JobSystem(std::size_t worker_count = 0);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;
    JobSystem(JobSystem&&) noexcept;
    JobSystem& operator=(JobSystem&&) noexcept;

    template <typename Function>
    [[nodiscard]] auto submit(Function&& function)
        -> JobHandle<std::invoke_result_t<std::decay_t<Function>&>> {
        using Callable = std::decay_t<Function>;
        using Result = std::invoke_result_t<Callable&>;

        Callable callable(std::forward<Function>(function));
        auto task = std::make_shared<std::packaged_task<Result()>>(
            [callable = std::move(callable)]() mutable -> Result {
                if constexpr (std::is_void_v<Result>) {
                    std::invoke(callable);
                } else {
                    return std::invoke(callable);
                }
            });
        std::future<Result> future = task->get_future();
        enqueue_task([task] { (*task)(); });
        return JobHandle<Result>(std::move(future));
    }

    void shutdown();

private:
    struct Impl;

    void enqueue_task(std::function<void()> task);

    std::unique_ptr<Impl> impl_;
};

} // namespace cubey::jobs
