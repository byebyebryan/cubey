#pragma once

#include <chrono>
#include <cstdint>

namespace cubey {

struct FrameTiming {
    double delta_seconds = 0.0;
    double elapsed_seconds = 0.0;
    std::uint64_t frame_index = 0;
};

class FrameClock {
  public:
    using Clock = std::chrono::steady_clock;

    FrameClock();
    explicit FrameClock(Clock::time_point start);

    FrameTiming tick();
    FrameTiming tick(Clock::time_point now);

    void reset();
    void reset(Clock::time_point now);

  private:
    Clock::time_point start_;
    Clock::time_point previous_;
    std::uint64_t frame_index_ = 0;
};

} // namespace cubey
