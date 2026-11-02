#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace cubey::projects::sdf {
struct StreamPose {
    double t = 0, radius = 0;
    std::array<double, 3> point{};
    unsigned index = 0;
    bool begin = false, end = false, corner = false;
};
struct StreamSegment {
    StreamPose first, last;
    double error = 0;
};
// Bounded recorded-input prototype. Decisions use only received poses; an
// accepted segment never changes. Release/restart always breaks the path.
class StreamSampler {
  public:
    explicit StreamSampler(double tolerance) : tolerance_(tolerance) {
        if (!std::isfinite(tolerance) || tolerance <= 0)
            throw std::invalid_argument("invalid stream tolerance");
    }
    std::vector<StreamSegment> feed(const StreamPose& pose) {
        if (!std::isfinite(pose.t) || pose.t <= last_t_ || !std::isfinite(pose.radius) ||
            pose.radius < 0 ||
            std::any_of(
                pose.point.begin(), pose.point.end(), [](double x) { return !std::isfinite(x); }))
            throw std::invalid_argument("invalid or unordered stream pose");
        last_t_ = pose.t;
        if (pose.begin) {
            if (down_)
                throw std::invalid_argument("stream restart without release");
            down_ = true;
            emitted_ = false;
            span_.clear();
        }
        if (!down_)
            throw std::invalid_argument("stream pose without begin");
        std::vector<StreamSegment> accepted;
        auto freeze = [&] {
            if (span_.size() > 1 || !emitted_) {
                accepted.push_back({span_.front(), span_.back(), error(span_)});
                emitted_ = true;
            }
            const auto last = span_.back();
            span_ = {last};
        };
        if (span_.size() >= 2) {
            const double before = span_.back().radius - span_[span_.size() - 2].radius;
            const double after = pose.radius - span_.back().radius;
            if (before * after < 0 || ((before == 0) != (after == 0)))
                freeze();
        }
        span_.push_back(pose);
        if (span_.size() > 1 && error(span_) > tolerance_) {
            span_.pop_back();
            freeze();
            span_.push_back(pose);
        }
        if (pose.corner || pose.end)
            freeze();
        if (pose.end) {
            down_ = false;
            span_.clear();
        }
        return accepted;
    }
    [[nodiscard]] std::optional<StreamSegment> tip() const {
        if (!down_ || (span_.size() == 1 && emitted_))
            return {};
        return StreamSegment{span_.front(), span_.back(), error(span_)};
    }
    [[nodiscard]] bool down() const {
        return down_;
    }

  private:
    double tolerance_, last_t_ = -std::numeric_limits<double>::infinity();
    bool down_ = false, emitted_ = false;
    std::vector<StreamPose> span_;
    static double error(const std::vector<StreamPose>& poses) {
        if (poses.size() == 1)
            return 0;
        const auto& first = poses.front();
        const auto& last = poses.back();
        double maximum = 0;
        for (const auto& p : poses) {
            const double t = (p.t - first.t) / (last.t - first.t);
            std::array<double, 3> d{};
            for (unsigned k = 0; k < 3; ++k)
                d[k] = p.point[k] - (first.point[k] + t * (last.point[k] - first.point[k]));
            const double r = std::abs(p.radius - (first.radius + t * (last.radius - first.radius)));
            maximum = std::max(maximum, std::hypot(d[0], d[1], d[2]) + r);
        }
        return maximum;
    }
};
} // namespace cubey::projects::sdf
