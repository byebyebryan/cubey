#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cubey::projects::fluid::fluid_25d {

// Render-only cue constants. The lattice spans are deliberately measured in
// cells so every imported terrain receives the same broad, non-authored
// presentation field without changing any physical input.
inline constexpr float kFluid25DPresentationCuePrimaryCellSpan = 23.0F;
inline constexpr float kFluid25DPresentationCueSecondaryCellSpan = 11.0F;
inline constexpr float kFluid25DPresentationCueRelaxationPerSecond = 0.0015F;

struct Fluid25DPresentationCueParity {
    [[nodiscard]] bool source_is_a() const noexcept {
        return source_is_a_;
    }

    void reset() noexcept {
        source_is_a_ = true;
    }

    void advance() noexcept {
        source_is_a_ = !source_is_a_;
    }

  private:
    bool source_is_a_ = true;
};

[[nodiscard]] constexpr std::uint32_t
fluid_25d_presentation_cue_hash(std::uint32_t x, std::uint32_t y, std::uint32_t salt) {
    std::uint32_t value = x * 0x8da6b343U ^ y * 0xd8163841U ^ salt;
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return value;
}

[[nodiscard]] inline float fluid_25d_presentation_cue_random(std::uint32_t x, std::uint32_t y,
                                                             std::uint32_t salt) {
    constexpr float kInvTwentyFourBit = 1.0F / 16777215.0F;
    return static_cast<float>(fluid_25d_presentation_cue_hash(x, y, salt) & 0x00ffffffU) *
           kInvTwentyFourBit;
}

[[nodiscard]] inline float fluid_25d_presentation_cue_smooth(float value) {
    const float clamped = std::clamp(value, 0.0F, 1.0F);
    return clamped * clamped * (3.0F - 2.0F * clamped);
}

[[nodiscard]] inline float fluid_25d_presentation_cue_lattice(std::uint32_t x, std::uint32_t y,
                                                              float cell_span, std::uint32_t salt) {
    const float scaled_x = static_cast<float>(x) / cell_span;
    const float scaled_y = static_cast<float>(y) / cell_span;
    const std::uint32_t x0 = static_cast<std::uint32_t>(std::floor(scaled_x));
    const std::uint32_t y0 = static_cast<std::uint32_t>(std::floor(scaled_y));
    const float tx = fluid_25d_presentation_cue_smooth(scaled_x - static_cast<float>(x0));
    const float ty = fluid_25d_presentation_cue_smooth(scaled_y - static_cast<float>(y0));
    const float lower = std::lerp(fluid_25d_presentation_cue_random(x0, y0, salt),
                                  fluid_25d_presentation_cue_random(x0 + 1U, y0, salt), tx);
    const float upper = std::lerp(fluid_25d_presentation_cue_random(x0, y0 + 1U, salt),
                                  fluid_25d_presentation_cue_random(x0 + 1U, y0 + 1U, salt), tx);
    return std::lerp(lower, upper, ty);
}

[[nodiscard]] inline float fluid_25d_presentation_cue_seed(std::uint32_t x, std::uint32_t y) {
    const float primary = fluid_25d_presentation_cue_lattice(
        x, y, kFluid25DPresentationCuePrimaryCellSpan, 0x68bc21ebU);
    const float secondary = fluid_25d_presentation_cue_lattice(
        x, y, kFluid25DPresentationCueSecondaryCellSpan, 0x02e5be93U);
    return std::clamp(0.5F + (primary - 0.5F) * 0.70F + (secondary - 0.5F) * 0.24F, 0.0F, 1.0F);
}

} // namespace cubey::projects::fluid::fluid_25d
