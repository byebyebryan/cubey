#include <cubey/frame_clock.h>

#include <chrono>
#include <cmath>
#include <stdexcept>

namespace {

void require_close(double actual, double expected, const char* message) {
    if (std::fabs(actual - expected) > 0.000001) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_frame_clock_tracks_delta_elapsed_and_index() {
    using Clock = cubey::FrameClock::Clock;
    const Clock::time_point start{};
    cubey::FrameClock clock(start);

    cubey::FrameTiming first = clock.tick(start + std::chrono::milliseconds(16));
    require_close(first.delta_seconds, 0.016, "first tick should report delta");
    require_close(first.elapsed_seconds, 0.016, "first tick should report elapsed time");
    if (first.frame_index != 1) {
        throw std::runtime_error("first tick should produce frame index 1");
    }

    cubey::FrameTiming second = clock.tick(start + std::chrono::milliseconds(50));
    require_close(second.delta_seconds, 0.034, "second tick should report delta from prior tick");
    require_close(second.elapsed_seconds, 0.05, "second tick should report elapsed time");
    if (second.frame_index != 2) {
        throw std::runtime_error("second tick should produce frame index 2");
    }
}
