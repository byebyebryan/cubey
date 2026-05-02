#include <cubey/frame_clock.h>

namespace cubey {
namespace {

double seconds_between(FrameClock::Clock::time_point begin, FrameClock::Clock::time_point end) {
    return std::chrono::duration<double>(end - begin).count();
}

} // namespace

FrameClock::FrameClock() : FrameClock(Clock::now()) {}

FrameClock::FrameClock(Clock::time_point start) : start_(start), previous_(start) {}

FrameTiming FrameClock::tick() {
    return tick(Clock::now());
}

FrameTiming FrameClock::tick(Clock::time_point now) {
    ++frame_index_;
    FrameTiming timing{
        seconds_between(previous_, now),
        seconds_between(start_, now),
        frame_index_,
    };
    previous_ = now;
    return timing;
}

void FrameClock::reset() {
    reset(Clock::now());
}

void FrameClock::reset(Clock::time_point now) {
    start_ = now;
    previous_ = now;
    frame_index_ = 0;
}

} // namespace cubey
