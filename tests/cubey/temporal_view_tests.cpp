#include <cubey/render/temporal_aa.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace {

using namespace cubey;
using namespace cubey::render;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void near(float actual, double expected, double tolerance, const char* message) {
    require(std::isfinite(actual) && std::abs(static_cast<double>(actual) - expected) <= tolerance,
            message);
}

void throws(auto&& action, const char* message) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

void projection_and_motion() {
    constexpr VkExtent2D extent{1920, 1080};
    const auto projection = math::perspective(std::acos(-1.0F) * 0.5F, 16.0F / 9.0F, 0.01F, 100.0F);
    const math::Vec3 world{0.3F, -0.2F, -2.0F};
    const auto original = temporal_screen_position(projection * math::Vec4{world, 1}, extent);
    // Independent pinhole camera arithmetic, including Vulkan's flipped Y.
    near(original.x, (1.0 + .3 / (2.0 * (16.0 / 9.0))) * 960.0, .0003,
         "perspective screen X differs from pinhole oracle");
    near(original.y, (1.0 + .2 / 2.0) * 540.0, .0003,
         "perspective screen Y differs from pinhole oracle");
    std::set<std::pair<float, float>> offsets;
    for (unsigned phase = 0; phase < 16; ++phase) {
        const auto offset = temporal_sample_offset(phase);
        require(std::abs(offset.x) <= .5F && std::abs(offset.y) <= .5F,
                "sample offset escaped its pixel");
        offsets.emplace(offset.x, offset.y);
        const auto shifted = temporal_screen_position(
            temporal_jitter_projection(projection, offset, extent) * math::Vec4{world, 1}, extent);
        near(shifted.x - original.x, -offset.x, .0003, "jitter X has wrong pixel scale/sign");
        near(shifted.y - original.y, -offset.y, .0003, "jitter Y has wrong pixel scale/sign");
        require(offset == temporal_sample_offset(phase + 16), "jitter pattern did not repeat");
    }
    require(offsets.size() == 16, "jitter phases are not unique");
    near(temporal_sample_offset(0).x, 0, 1e-7, "Halton first X");
    near(temporal_sample_offset(0).y, -1.0 / 6.0, 1e-7, "Halton first Y");

    const auto camera_right = math::translation(-.1F, 0, 0);
    const auto camera_up = math::translation(0, -.1F, 0);
    const auto right_motion = temporal_motion_pixels({0, 0, -2}, {0, 0, -2},
                                                     projection * camera_right, projection, extent);
    near(right_motion.x, 27, .0003, "camera translation X motion sign/units");
    near(right_motion.y, 0, .0003, "camera X motion contaminated Y");
    const auto up_motion =
        temporal_motion_pixels({0, 0, -2}, {0, 0, -2}, projection * camera_up, projection, extent);
    near(up_motion.y, -27, .0003, "camera translation Y motion sign/units");
    const auto object_motion =
        temporal_motion_pixels({.1F, 0, -2}, {0, 0, -2}, projection, projection, extent);
    near(object_motion.x, -27, .0003, "object motion must map current to previous");
    const auto rest = temporal_motion_pixels(world, world, projection, projection, extent);
    require(rest == math::Vec2{0}, "static geometry has geometric jitter motion");

    const auto ortho = math::orthographic(-2, 2, -1, 1, .01F, 100);
    const math::Vec2 offset{.25F, -.375F};
    const auto ortho_before = temporal_screen_position(ortho * math::Vec4{world, 1}, extent);
    const auto ortho_after = temporal_screen_position(
        temporal_jitter_projection(ortho, offset, extent) * math::Vec4{world, 1}, extent);
    near(ortho_after.x - ortho_before.x, -.25, .0003, "orthographic jitter X");
    near(ortho_after.y - ortho_before.y, .375, .0003, "orthographic jitter Y");
    throws([&] { (void)temporal_jitter_projection(projection, {0, 0}, {0, 1}); },
           "zero extent accepted");
    throws([&] { (void)temporal_jitter_projection(projection, {1, 0}, extent); },
           "non-subpixel jitter accepted");
    throws([&] { (void)temporal_screen_position({0, 0, 0, -1}, extent); },
           "point behind the eye accepted");
}

void submission_history() {
    TemporalViewState state;
    TemporalView view;
    constexpr VkExtent2D extent{640, 480};
    const auto first = state.prepare(view, extent);
    require(!first.history_valid && first.history_age == 0 && first.sequence == 0,
            "first frame incorrectly has history");
    require(state.submitted_frames() == 0, "recording advanced submitted history");
    throws([&] { (void)state.prepare(view, extent); }, "overlapping prepare accepted");
    throws([&] { state.commit(first.token, {}); }, "zero submission ticket accepted");
    throws([&] { state.commit(first.token + 1, {1}); }, "foreign token accepted");
    state.commit(first.token, {7});
    require(state.submitted_frames() == 1 && state.last_submission().value == 7,
            "accepted submission not published");

    view.view = math::translation(-.1F, 0, 0);
    const auto moved = state.prepare(view, extent);
    require(moved.history_valid && moved.history_age == 1 && moved.sequence == 1,
            "ordinary camera movement reset history");
    require(moved.history_read_index == first.history_write_index &&
                moved.history_write_index != moved.history_read_index,
            "history ping-pong does not follow accepted sequence");
    throws([&] { state.commit(moved.token, {7}); }, "stale submission ticket accepted");
    state.discard();
    require(state.submitted_frames() == 1, "discarded frame advanced history");
    const auto retry = state.prepare(view, extent);
    require(!retry.history_valid && retry.reset_reason == TemporalResetReason::DiscardedFrame &&
                retry.sequence == 1 && retry.sample_offset_pixels == first.sample_offset_pixels,
            "discarded work was reused or advanced jitter");
    state.commit(retry.token, {9});
    throws([&] { state.commit(retry.token, {10}); }, "duplicate commit accepted");
    const auto following = state.prepare(view, extent);
    require(following.history_valid && following.history_age == 1,
            "retry did not seed fresh history");
    state.commit(following.token, {10});
}

void discontinuities_and_independence() {
    constexpr VkExtent2D extent{640, 480};
    const auto reset_case = [&](auto change, TemporalResetReason expected) {
        TemporalViewState state;
        TemporalView view;
        auto dimensions = extent;
        const auto initial = state.prepare(view, dimensions);
        state.commit(initial.token, {1});
        change(view, dimensions);
        const auto changed = state.prepare(view, dimensions);
        require(!changed.history_valid && changed.reset_reason == expected,
                "discontinuity has wrong history policy");
    };
    reset_case([](auto& view, auto&) { view.content_generation++; },
               TemporalResetReason::ContentChanged);
    reset_case([](auto& view, auto&) { view.view_id++; }, TemporalResetReason::ViewChanged);
    reset_case([](auto&, auto& dimensions) { dimensions.width++; },
               TemporalResetReason::TargetChanged);
    reset_case([](auto& view, auto&) { view.projection[0][0] *= 2; },
               TemporalResetReason::ProjectionChanged);
    reset_case([](auto& view, auto&) { view.reset = true; }, TemporalResetReason::Explicit);

    TemporalViewState left, right;
    TemporalView view;
    const auto a = left.prepare(view, extent);
    left.commit(a.token, {1});
    const auto b = right.prepare(view, extent);
    require(!b.history_valid && b.sequence == 0, "independent views shared history");
    right.commit(b.token, {2});
    const auto a2 = left.prepare(view, extent);
    require(a2.history_valid && a2.sequence == 1, "other view changed first view history");
    left.discard();
    TemporalView bad;
    bad.projection[0][0] = std::numeric_limits<float>::quiet_NaN();
    throws([&] { (void)left.prepare(bad, extent); }, "NaN camera accepted");
    bad.projection = math::Mat4{0};
    throws([&] { (void)left.prepare(bad, extent); }, "singular camera accepted");
    require(!left.pending(), "invalid inputs left a pending frame");
}

} // namespace

int main() {
    try {
        projection_and_motion();
        submission_history();
        discontinuities_and_independence();
        std::cout << "PASS: temporal projection, motion and accepted history\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
