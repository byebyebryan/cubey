#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace cubey::projects::fluid::fluid_25d {

// Optional catchment presentation overrides. These values are consumed only
// by camera setup and graphics push constants; the solver configuration and
// its terrain/water buffers never contain them.
struct Fluid25DCatchmentRenderOptions {
    std::optional<float> terrain_palette_low_m{};
    std::optional<float> terrain_palette_high_m{};
    std::optional<float> terrain_height_scale{};
    std::optional<float> home_camera_distance_m{};
    bool terrain_thin_water_composite = false;
    bool hillside_depth_cues = false;
    // Native recording presentation only; historical scenario defaults remain unchanged.
    bool native_recording = false;
    float quiver_speed_upper_m_per_s = 0.80F;
};

inline constexpr float kFluid25DMinTerrainCaseRenderHeightScale = 0.001F;
inline constexpr float kFluid25DMaxTerrainCaseRenderHeightScale = 2.0F;

// Render-only cue constants. The lattice spans are deliberately measured in
// cells so every imported terrain receives the same broad, non-authored
// presentation field without changing any physical input.
inline constexpr float kFluid25DPresentationCuePrimaryCellSpan = 23.0F;
inline constexpr float kFluid25DPresentationCueSecondaryCellSpan = 11.0F;
inline constexpr float kFluid25DPresentationCueRelaxationPerSecond = 0.0015F;

// Flow Inspection is a conventional, fixed-grid velocity field rather than a
// particle/tracer display. The compact 128 by 64 source-to-outlet scene gets a
// 96 by 48 lattice (about 1.3 cells between anchors); the default 256 by 128
// product grid retains that lattice count with about 2.7 cells between
// anchors. Smaller numerical fixtures reduce the count instead of placing
// duplicate or out-of-domain anchors.
inline constexpr std::uint32_t kFluid25DQuiverMaxColumns = 96U;
inline constexpr std::uint32_t kFluid25DQuiverMaxRows = 48U;
inline constexpr std::uint32_t kFluid25DQuiverMinimumPitchCells = 1U;
inline constexpr std::uint32_t kFluid25DQuiverVertexCount = 9U;
// This lies above the retained initial terrain-sheet maximum (~0.019828 m/s),
// keeping initial, dry, and lake-at-rest inspection frames honestly empty.
inline constexpr float kFluid25DQuiverMinimumSpeedMPerS = 0.025F;
// A fixed physical upper range keeps color and length comparable between
// frames and scenes. The source-outlet demo's mature flow reaches roughly
// 0.79 m/s, so 0.80 m/s preserves variation through its fastest reach without
// making the mapping depend on the current frame.
inline constexpr float kFluid25DQuiverSpeedUpperMPerS = 0.80F;
// The complete arrow silhouette is 1.26 local units long. Scaling it to this
// fraction of the local lattice pitch leaves visible gaps on the compact
// grid while preserving enough overview pixels to distinguish the head.
inline constexpr float kFluid25DQuiverMinimumSilhouettePitchFraction = 0.58F;
inline constexpr float kFluid25DQuiverMaximumSilhouettePitchFraction = 0.72F;
inline constexpr float kFluid25DQuiverDirectionSmoothingSeconds = 1.5F;
inline constexpr float kFluid25DQuiverStrengthSmoothingSeconds = 1.5F;
inline constexpr float kFluid25DQuiverOpacitySmoothingSeconds = 0.75F;
inline constexpr std::uint32_t kFluid25DQuiverNeighborhoodRadiusCells = 1U;

struct Fluid25DQuiverLattice {
    std::uint32_t columns = 1U;
    std::uint32_t rows = 1U;
};

[[nodiscard]] constexpr std::uint32_t fluid_25d_quiver_axis_count(std::uint32_t cell_count,
                                                                  std::uint32_t maximum_count) {
    // Config validation requires at least two cells on each axis. The max(1)
    // keeps this helper safe for focused callers before that validation, and
    // the anchor helper clamps the corresponding degenerate coordinate.
    const std::uint32_t reduced = cell_count / kFluid25DQuiverMinimumPitchCells;
    return std::min(maximum_count, std::max(1U, reduced));
}

[[nodiscard]] constexpr Fluid25DQuiverLattice fluid_25d_quiver_lattice(std::uint32_t grid_width,
                                                                       std::uint32_t grid_height) {
    return {
        .columns = fluid_25d_quiver_axis_count(grid_width, kFluid25DQuiverMaxColumns),
        .rows = fluid_25d_quiver_axis_count(grid_height, kFluid25DQuiverMaxRows),
    };
}

[[nodiscard]] constexpr std::uint32_t fluid_25d_quiver_count(std::uint32_t grid_width,
                                                             std::uint32_t grid_height) {
    const Fluid25DQuiverLattice lattice = fluid_25d_quiver_lattice(grid_width, grid_height);
    return lattice.columns * lattice.rows;
}

struct Fluid25DQuiverAnchor {
    float cell_x = 0.0F;
    float cell_y = 0.0F;
};

[[nodiscard]] inline float fluid_25d_quiver_anchor_axis(std::uint32_t coordinate,
                                                        std::uint32_t count,
                                                        std::uint32_t cell_count) {
    if (cell_count == 0U) {
        return 0.0F;
    }
    const float centre = 0.5F * (static_cast<float>(cell_count) - 1.0F);
    if (count <= 1U || cell_count <= 2U) {
        return centre;
    }
    const float fraction = static_cast<float>(coordinate) / static_cast<float>(count - 1U);
    return std::lerp(0.5F, static_cast<float>(cell_count) - 1.5F, fraction);
}

[[nodiscard]] inline Fluid25DQuiverAnchor
fluid_25d_quiver_anchor(std::uint32_t index, std::uint32_t grid_width, std::uint32_t grid_height) {
    const Fluid25DQuiverLattice lattice = fluid_25d_quiver_lattice(grid_width, grid_height);
    const std::uint32_t bounded_index = std::min(index, lattice.columns * lattice.rows - 1U);
    return {
        .cell_x = fluid_25d_quiver_anchor_axis(bounded_index % lattice.columns, lattice.columns,
                                               grid_width),
        .cell_y = fluid_25d_quiver_anchor_axis(bounded_index / lattice.columns, lattice.rows,
                                               grid_height),
    };
}

[[nodiscard]] inline float fluid_25d_quiver_smoothing_blend(float delta_seconds,
                                                            float smoothing_seconds) {
    if (!std::isfinite(delta_seconds) || !std::isfinite(smoothing_seconds) ||
        delta_seconds <= 0.0F || smoothing_seconds <= 0.0F) {
        return 0.0F;
    }
    return 1.0F - std::exp(-delta_seconds / smoothing_seconds);
}

[[nodiscard]] inline bool fluid_25d_quiver_sample_is_visible(float water_depth_m,
                                                             float speed_m_per_s,
                                                             float minimum_wet_depth_m) {
    return std::isfinite(water_depth_m) && std::isfinite(speed_m_per_s) &&
           std::isfinite(minimum_wet_depth_m) && water_depth_m > minimum_wet_depth_m &&
           speed_m_per_s >= kFluid25DQuiverMinimumSpeedMPerS;
}

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
