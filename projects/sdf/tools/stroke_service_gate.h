#pragma once
#include "../sdf_cached_commit.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <nlohmann/json.hpp>

namespace cubey::projects::sdf {
struct StopService {};
// One caller waits for permission; all GPU work remains on the GPU owner.
struct StrokeServiceGate {
    using Clock = std::chrono::steady_clock;
    std::mutex mutex;
    std::condition_variable cv;
    bool stop = false, waiting = false, active = false, permit = false;
    std::string kind;
    unsigned frame = 0;
    Clock::time_point received;
    double ready_ms = 0, grant_ms = 0, latest_available_ms = 0;
    nlohmann::json units = nlohmann::json::array();
    double elapsed() const {
        return std::chrono::duration<double, std::milli>(Clock::now() - received).count();
    }
    void before(const CommitUnit& next) {
        std::unique_lock lock(mutex);
        kind = next.kind;
        ready_ms = elapsed();
        waiting = true;
        cv.notify_all();
        cv.wait(lock, [&] { return stop || permit; });
        if (stop)
            throw StopService{};
        permit = false;
        waiting = false;
        active = true;
    }
    void after(const CommitUnit& u) {
        std::lock_guard lock(mutex);
        const auto origin =
            std::chrono::duration_cast<std::chrono::nanoseconds>(received.time_since_epoch())
                .count();
        units.push_back({{"frame", frame},
                         {"kind", u.kind},
                         {"groups", u.groups},
                         {"level", u.level},
                         {"gpu_ms", u.gpu_ms},
                         {"wall_ms", u.wall_ms},
                         {"ready_ms", ready_ms},
                         {"grant_ms", grant_ms},
                         {"submit_ms", (u.submit_ns - origin) / 1e6},
                         {"gpu_complete_ms", (u.complete_ns - origin) / 1e6}});
        latest_available_ms = elapsed();
        units.back()["available_ms"] = latest_available_ms;
        active = false;
        cv.notify_all();
    }
    void request_stop() {
        {
            std::lock_guard lock(mutex);
            stop = true;
        }
        cv.notify_all();
    }
};
} // namespace cubey::projects::sdf
