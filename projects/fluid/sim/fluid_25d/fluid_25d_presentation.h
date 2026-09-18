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

// Flow Inspection owns a deliberately small, render-only streamlet field.
// These are not solver particles: the fixed state exists only to make the
// velocity field readable over time without a dense screen-space texture.
// Forty-eight instances leave the active mountain-sheet audition with roughly
// thirty visible marks: enough to read the field, but not a screen-space
// texture. Eight locally draped cells make the taper readable at overview
// resolution without turning the streamlets into a surface overlay.
inline constexpr std::uint32_t kFluid25DStreamletCount = 48U;
inline constexpr std::uint32_t kFluid25DStreamletSegmentCount = 8U;
inline constexpr std::uint32_t kFluid25DStreamletVertexCount =
    kFluid25DStreamletSegmentCount * 6U;
// Streamlets deliberately use a presentation-only hysteresis band. An
// inactive seed must see decisively moving water before it can appear, while
// an established mark is allowed to remain through the quieter parts of the
// same coherent run. Both thresholds stay above/below the retained initial
// mountain-sheet maximum (~0.019828 m/s) respectively, so the initial state
// remains an empty, honest inspection surface.
inline constexpr float kFluid25DStreamletActivationSpeedMPerS = 0.025F;
inline constexpr float kFluid25DStreamletSustainSpeedMPerS = 0.013F;
// Fade, cooldown, and heading response are all simulation-time quantities.
// They are deliberately long enough to span several 2 s terrain steps while
// still behaving proportionally on the smaller fixed-step fixtures.
inline constexpr float kFluid25DStreamletFadeSeconds = 8.0F;
inline constexpr float kFluid25DStreamletDirectionSmoothingSeconds = 6.0F;
inline constexpr std::uint32_t kFluid25DStreamletCooldownAcceptedSteps = 8U;
// Lifetime is expressed in accepted outer fixed steps rather than wall time.
// That keeps a 30 m / 2 s terrain audition coherent for roughly one hundred
// samples while remaining responsive on the 1 m product fixtures.
inline constexpr std::uint32_t kFluid25DStreamletLifetimeAcceptedSteps = 96U;
inline constexpr std::uint32_t kFluid25DStreamletWarmupAcceptedSteps = 10U;

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

// A seed is expressed in normalized interior cell coordinates. Keeping this
// helper CPU-testable makes reset/respawn deterministic while the GPU keeps
// the persistent positions and never needs a host readback or upload.
struct Fluid25DStreamletSeed {
    float normalized_x = 0.5F;
    float normalized_y = 0.5F;
    float initial_age_fraction = 0.0F;
    float activation_delay_steps = 0.0F;
};

[[nodiscard]] inline float fluid_25d_streamlet_lifetime_multiplier(
    std::uint32_t streamlet_index, std::uint32_t generation) {
    return 0.75F + fluid_25d_presentation_cue_random(streamlet_index, generation, 0x3c6ef372U) *
                        0.50F;
}

[[nodiscard]] inline float fluid_25d_streamlet_lifetime_seconds(
    float fixed_delta_seconds, std::uint32_t streamlet_index, std::uint32_t generation) {
    return fixed_delta_seconds * static_cast<float>(kFluid25DStreamletLifetimeAcceptedSteps) *
           fluid_25d_streamlet_lifetime_multiplier(streamlet_index, generation);
}

[[nodiscard]] inline float fluid_25d_streamlet_warmup_seconds(float fixed_delta_seconds,
                                                                std::uint32_t streamlet_index,
                                                                std::uint32_t generation) {
    return fixed_delta_seconds * static_cast<float>(kFluid25DStreamletWarmupAcceptedSteps) *
           fluid_25d_presentation_cue_random(streamlet_index, generation, 0xbb67ae85U);
}

[[nodiscard]] inline float fluid_25d_streamlet_cooldown_seconds(
    float fixed_delta_seconds, std::uint32_t streamlet_index, std::uint32_t generation) {
    // The fixed base prevents an all-at-once retry, while the deterministic
    // tail prevents an artificial pulse when an entire patch becomes active.
    return fixed_delta_seconds *
           (static_cast<float>(kFluid25DStreamletCooldownAcceptedSteps) +
            fluid_25d_presentation_cue_random(streamlet_index, generation, 0x510e527fU) *
                static_cast<float>(kFluid25DStreamletWarmupAcceptedSteps));
}

[[nodiscard]] inline float fluid_25d_streamlet_fade_fraction(float delta_seconds) {
    if (!std::isfinite(delta_seconds) || delta_seconds <= 0.0F) {
        return 0.0F;
    }
    return std::clamp(delta_seconds / kFluid25DStreamletFadeSeconds, 0.0F, 1.0F);
}

[[nodiscard]] inline float fluid_25d_streamlet_direction_blend(float delta_seconds) {
    if (!std::isfinite(delta_seconds) || delta_seconds <= 0.0F) {
        return 0.0F;
    }
    return 1.0F - std::exp(-delta_seconds / kFluid25DStreamletDirectionSmoothingSeconds);
}

[[nodiscard]] inline Fluid25DStreamletSeed
fluid_25d_streamlet_seed(std::uint32_t streamlet_index, std::uint32_t generation) {
    return {
        .normalized_x = fluid_25d_presentation_cue_random(streamlet_index, generation,
                                                           0x7f4a7c15U),
        .normalized_y = fluid_25d_presentation_cue_random(streamlet_index, generation,
                                                           0x6a09e667U),
        .initial_age_fraction = fluid_25d_presentation_cue_random(streamlet_index, generation,
                                                                   0xa54ff53aU),
        .activation_delay_steps =
            fluid_25d_presentation_cue_random(streamlet_index, generation, 0xbb67ae85U) *
            static_cast<float>(kFluid25DStreamletWarmupAcceptedSteps),
    };
}

[[nodiscard]] inline bool fluid_25d_streamlet_has_safe_water(float water_depth_m,
                                                              float speed_m_per_s,
                                                              float minimum_wet_depth_m) {
    return std::isfinite(water_depth_m) && std::isfinite(speed_m_per_s) &&
           std::isfinite(minimum_wet_depth_m) && water_depth_m > minimum_wet_depth_m;
}

[[nodiscard]] inline bool fluid_25d_streamlet_can_activate(float water_depth_m,
                                                            float speed_m_per_s,
                                                            float minimum_wet_depth_m) {
    return fluid_25d_streamlet_has_safe_water(water_depth_m, speed_m_per_s, minimum_wet_depth_m) &&
           speed_m_per_s >= kFluid25DStreamletActivationSpeedMPerS;
}

[[nodiscard]] inline bool fluid_25d_streamlet_can_sustain(float water_depth_m,
                                                           float speed_m_per_s,
                                                           float minimum_wet_depth_m) {
    return fluid_25d_streamlet_has_safe_water(water_depth_m, speed_m_per_s, minimum_wet_depth_m) &&
           speed_m_per_s >= kFluid25DStreamletSustainSpeedMPerS;
}

// A speed-driven retirement can be cancelled when the same finite, wet mark
// recovers to the sustain threshold. Lifetime retirement is terminal, so it
// does not oscillate with noisy speed samples near the lower threshold.
[[nodiscard]] inline bool fluid_25d_streamlet_lifetime_expired(float age_seconds,
                                                                float lifetime_seconds) {
    return std::isfinite(age_seconds) && std::isfinite(lifetime_seconds) &&
           age_seconds > lifetime_seconds;
}

[[nodiscard]] inline bool fluid_25d_streamlet_should_retire(float speed_m_per_s,
                                                             float age_seconds,
                                                             float lifetime_seconds) {
    return !std::isfinite(speed_m_per_s) ||
           fluid_25d_streamlet_lifetime_expired(age_seconds, lifetime_seconds) ||
           speed_m_per_s < kFluid25DStreamletSustainSpeedMPerS;
}

[[nodiscard]] inline bool fluid_25d_streamlet_can_cancel_speed_retirement(
    float speed_m_per_s, float age_seconds, float lifetime_seconds) {
    return std::isfinite(speed_m_per_s) &&
           !fluid_25d_streamlet_lifetime_expired(age_seconds, lifetime_seconds) &&
           speed_m_per_s >= kFluid25DStreamletSustainSpeedMPerS;
}

} // namespace cubey::projects::fluid::fluid_25d
