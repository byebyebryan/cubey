#include "terrain_backdrop_camera.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace cubey::projects::terrain {
namespace {

constexpr std::array<float, 5> kAnchorCoordinates{-4096.0F, -2048.0F, 0.0F, 2048.0F,
                                                   4096.0F};
constexpr std::array<float, 5> kSampleDistances{400.0F, 800.0F, 1600.0F, 3200.0F, 6400.0F};
constexpr std::array<float, 3> kLateralFactors{-0.18F, 0.0F, 0.18F};
constexpr std::uint32_t kHeadingCount = 24U;
constexpr float kCameraClearanceM = 120.0F;
constexpr float kPeakAboveFrameCenterRadians = 5.0F * std::numbers::pi_v<float> / 180.0F;
constexpr float kMinimumPitchRadians = -2.0F * std::numbers::pi_v<float> / 180.0F;
constexpr float kMaximumPitchRadians = 12.0F * std::numbers::pi_v<float> / 180.0F;

[[nodiscard]] float saturate(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] float useful_distance_score(float distance_m) {
    const float log_distance = std::log2(distance_m / 2200.0F);
    return std::exp(-1.35F * log_distance * log_distance);
}

struct HeadingEvaluation {
    float score = -std::numeric_limits<float>::infinity();
    float target_distance_m = 0.0F;
    cubey::math::Vec2 target_xz{0.0F, 0.0F};
};

[[nodiscard]] HeadingEvaluation evaluate_heading(const TerrainSourceParameters& clean_source,
                                                 cubey::math::Vec2 anchor, float yaw_radians,
                                                 float vertical_scale) {
    const cubey::math::Vec2 forward{std::sin(yaw_radians), -std::cos(yaw_radians)};
    const cubey::math::Vec2 right{std::cos(yaw_radians), std::sin(yaw_radians)};
    const float anchor_height =
        sample_terrain(clean_source, {.world_xz = anchor}).height_m * vertical_scale;
    const float relief_scale = std::max(clean_source.height_scale_m * vertical_scale, 80.0F);

    float best_peak_score = -1.0F;
    float best_distance = kSampleDistances.front();
    cubey::math::Vec2 best_target = anchor + forward * best_distance;
    float silhouette_sum = 0.0F;
    float near_obstruction = 0.0F;

    for (std::size_t distance_index = 0; distance_index < kSampleDistances.size();
         ++distance_index) {
        const float distance = kSampleDistances[distance_index];
        std::array<float, 3> heights{};
        for (std::size_t lateral_index = 0; lateral_index < kLateralFactors.size();
             ++lateral_index) {
            const cubey::math::Vec2 position =
                anchor + forward * distance + right * (distance * kLateralFactors[lateral_index]);
            heights[lateral_index] =
                sample_terrain(clean_source, {.world_xz = position}).height_m * vertical_scale;
        }

        const float peak_height = *std::max_element(heights.begin(), heights.end());
        const float prominence = saturate((peak_height - anchor_height) / (relief_scale * 0.45F));
        const float distance_score = useful_distance_score(distance);
        const float candidate_score = prominence * (0.72F + 0.28F * distance_score);
        if (candidate_score > best_peak_score) {
            best_peak_score = candidate_score;
            best_distance = distance;
            const auto peak = std::max_element(heights.begin(), heights.end());
            const std::size_t peak_index = static_cast<std::size_t>(peak - heights.begin());
            best_target = anchor + forward * distance +
                          right * (distance * kLateralFactors[peak_index]);
        }

        silhouette_sum +=
            saturate((std::abs(heights[0] - heights[1]) +
                      std::abs(heights[2] - heights[1]) + 0.5F * std::abs(heights[2] - heights[0])) /
                     (relief_scale * 0.30F));

        if (distance_index < 2U) {
            const float sightline_height = anchor_height + kCameraClearanceM * 0.35F;
            near_obstruction += saturate((peak_height - sightline_height) /
                                        std::max(kCameraClearanceM, relief_scale * 0.08F));
        }
    }

    const float peak_score = saturate(best_peak_score);
    const float silhouette_score = saturate(silhouette_sum / kSampleDistances.size());
    const float distance_score = useful_distance_score(best_distance);
    const float clear_near_field = 1.0F - saturate(near_obstruction * 0.5F);
    return {
        .score = 0.50F * peak_score + 0.25F * silhouette_score + 0.15F * distance_score +
                 0.10F * clear_near_field,
        .target_distance_m = best_distance,
        .target_xz = best_target,
    };
}

} // namespace

TerrainBackdropCameraPlan plan_terrain_backdrop_camera(const TerrainSourceParameters& source,
                                                       float vertical_scale) {
    if (!std::isfinite(vertical_scale) || vertical_scale <= 0.0F) {
        throw std::runtime_error("terrain backdrop camera vertical scale must be positive");
    }

    TerrainSourceParameters clean_source = source;
    clean_source.weathering = TerrainWeatheringMode::Off;
    clean_source.weathering_strength = 0.0F;

    TerrainBackdropCameraPlan best{};
    best.score = -std::numeric_limits<float>::infinity();
    for (const float anchor_z : kAnchorCoordinates) {
        for (const float anchor_x : kAnchorCoordinates) {
            const cubey::math::Vec2 anchor{anchor_x, anchor_z};
            for (std::uint32_t heading = 0U; heading < kHeadingCount; ++heading) {
                const float yaw = static_cast<float>(heading) * 2.0F *
                                  std::numbers::pi_v<float> /
                                  static_cast<float>(kHeadingCount);
                const HeadingEvaluation evaluation =
                    evaluate_heading(clean_source, anchor, yaw, vertical_scale);
                if (evaluation.score <= best.score) {
                    continue;
                }

                const float camera_height =
                    sample_terrain(source, {.world_xz = anchor}).height_m * vertical_scale +
                    kCameraClearanceM;
                const TerrainSample target_sample =
                    sample_terrain(source, {.world_xz = evaluation.target_xz});
                const float target_height = target_sample.height_m * vertical_scale;
                const float elevation =
                    std::atan2(target_height - camera_height, evaluation.target_distance_m);
                const float pitch = std::clamp(elevation - kPeakAboveFrameCenterRadians,
                                               kMinimumPitchRadians, kMaximumPitchRadians);
                best = {
                    .transform =
                        {
                            .translation = {anchor.x, camera_height, anchor.y},
                            .rotation =
                                cubey::math::angle_axis_quat(yaw, {0.0F, 1.0F, 0.0F}) *
                                cubey::math::angle_axis_quat(pitch, {1.0F, 0.0F, 0.0F}),
                        },
                    .anchor_xz = anchor,
                    .target_position = {evaluation.target_xz.x, target_height,
                                        evaluation.target_xz.y},
                    .yaw_radians = yaw,
                    .pitch_radians = pitch,
                    .target_distance_m = evaluation.target_distance_m,
                    .target_elevation_radians = elevation,
                    .score = evaluation.score,
                };
            }
        }
    }
    return best;
}

} // namespace cubey::projects::terrain
