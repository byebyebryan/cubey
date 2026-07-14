#include "terrain_backdrop_camera.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <span>
#include <stdexcept>
#include <vector>

namespace cubey::projects::terrain {
namespace {

constexpr std::array<float, 5> kAnchorCoordinates{-4096.0F, -2048.0F, 0.0F, 2048.0F, 4096.0F};
constexpr std::array<float, 2> kBackdropSampleDistances{3200.0F, 6400.0F};
constexpr std::array<float, 2> kFarFieldSampleDistances{3400.0F, 6600.0F};
constexpr std::array<float, 1> kMidgroundSampleDistances{1600.0F};
constexpr std::array<float, 3> kLateralFactors{-0.18F, 0.0F, 0.18F};
constexpr std::array<float, 3> kLowerFrustumNdcX{-1.0F, 0.0F, 1.0F};
constexpr std::array<float, 5> kNearFrameNdcX{-1.0F, -0.5F, 0.0F, 0.5F, 1.0F};
constexpr std::array<float, 3> kNearFrameNdcY{0.0F, 0.35F, 0.70F};
constexpr std::array<float, 1> kLowerFrameNdcY{-0.35F};
constexpr std::uint32_t kHeadingCount = 24U;
constexpr std::size_t kFramingShortlistCount = 64U;
constexpr std::size_t kFarFieldFramingCandidateCount = 12U;
constexpr std::uint32_t kMaximumNearFrameOccludedRayCount = 2U;
constexpr float kMinimumCameraClearanceM = 150.0F;
constexpr float kForegroundClearDistanceM = 300.0F;
constexpr float kForegroundSampleStepM = 25.0F;
constexpr float kForegroundSafetyMarginM = 10.0F;
constexpr float kNearFrameStartDistanceM = 100.0F;
constexpr float kNearFrameSampleStepM = 50.0F;
constexpr float kFarFieldSafeZoneRadiusM = 200.0F;
constexpr float kFarFieldYawHalfAngleRadians = 30.0F * std::numbers::pi_v<float> / 180.0F;
constexpr float kFarFieldNearFrameTestDistanceM = 2400.0F;
constexpr float kFarFieldLowerFrameStartDistanceM = 300.0F;
constexpr float kFarFieldLowerFrameTestDistanceM = 1200.0F;
constexpr std::uint32_t kFarFieldMaximumLowerFrameOccludedRayCount = 2U;
constexpr float kBackdropVerticalFovRadians = 40.0F * std::numbers::pi_v<float> / 180.0F;
constexpr float kPeakAboveFrameCenterRadians = 5.0F * std::numbers::pi_v<float> / 180.0F;
constexpr float kMinimumPitchRadians = -2.0F * std::numbers::pi_v<float> / 180.0F;
constexpr float kMaximumPitchRadians = 12.0F * std::numbers::pi_v<float> / 180.0F;
constexpr float kHierarchicalMaximumPitchRadians = 18.0F * std::numbers::pi_v<float> / 180.0F;

[[nodiscard]] float saturate(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] float useful_distance_score(float distance_m) {
    const float log_distance = std::log2(distance_m / 2200.0F);
    return std::exp(-1.35F * log_distance * log_distance);
}

[[nodiscard]] std::span<const float> sample_distances(TerrainBackdropCameraProfile profile,
                                                      bool far_field_product) noexcept {
    if (far_field_product) {
        return kFarFieldSampleDistances;
    }
    return profile == TerrainBackdropCameraProfile::Midground
               ? std::span<const float>{kMidgroundSampleDistances}
               : std::span<const float>{kBackdropSampleDistances};
}

struct HeadingEvaluation {
    float score = -std::numeric_limits<float>::infinity();
    float target_distance_m = 0.0F;
    cubey::math::Vec2 target_xz{0.0F, 0.0F};
};

struct BackdropCandidate {
    cubey::math::Vec2 anchor{0.0F, 0.0F};
    float yaw_radians = 0.0F;
    HeadingEvaluation heading{};
};

struct ForegroundClearance {
    float camera_height_m = 0.0F;
    float camera_clearance_m = 0.0F;
    float minimum_margin_m = 0.0F;
};

struct NearFrameOcclusion {
    float test_distance_m = 0.0F;
    std::uint32_t occluded_ray_count = 0U;
    float occupancy_ratio = 0.0F;
    float nearest_hit_distance_m = 0.0F;
};

struct SafeZoneEvaluation {
    float camera_height_m = 0.0F;
    float camera_clearance_m = 0.0F;
    float target_height_m = 0.0F;
    float target_elevation_radians = 0.0F;
    float pitch_radians = 0.0F;
    float minimum_target_distance_m = 0.0F;
    float foreground_minimum_margin_m = 0.0F;
    std::uint32_t near_frame_max_occluded_ray_count = 0U;
    std::uint32_t lower_frame_max_occluded_ray_count = 0U;
    bool contract_satisfied = false;
};

[[nodiscard]] ForegroundClearance foreground_clearance(const TerrainSourceParameters& source,
                                                       cubey::math::Vec2 anchor,
                                                       float anchor_height_m, float yaw_radians,
                                                       float vertical_scale, float aspect_ratio) {
    const cubey::math::Quat conservative_rotation =
        cubey::math::angle_axis_quat(yaw_radians, {0.0F, 1.0F, 0.0F}) *
        cubey::math::angle_axis_quat(kMinimumPitchRadians, {1.0F, 0.0F, 0.0F});
    const float tan_half_fov = std::tan(kBackdropVerticalFovRadians * 0.5F);
    float required_camera_height_m = anchor_height_m + kMinimumCameraClearanceM;

    for (const float ndc_x : kLowerFrustumNdcX) {
        const cubey::math::Vec3 ray =
            conservative_rotation *
            cubey::math::Vec3{ndc_x * tan_half_fov * aspect_ratio, -tan_half_fov, -1.0F};
        const float horizontal_length = std::sqrt(ray.x * ray.x + ray.z * ray.z);
        const cubey::math::Vec2 horizontal_direction{ray.x / horizontal_length,
                                                     ray.z / horizontal_length};
        const float vertical_slope = ray.y / horizontal_length;
        for (float distance_m = kForegroundSampleStepM; distance_m <= kForegroundClearDistanceM;
             distance_m += kForegroundSampleStepM) {
            const cubey::math::Vec2 position = anchor + horizontal_direction * distance_m;
            const float terrain_height_m =
                sample_terrain_height(source, {.world_xz = position}) * vertical_scale;
            required_camera_height_m =
                std::max(required_camera_height_m,
                         terrain_height_m + kForegroundSafetyMarginM - distance_m * vertical_slope);
        }
    }

    float minimum_margin_m = std::numeric_limits<float>::infinity();
    for (const float ndc_x : kLowerFrustumNdcX) {
        const cubey::math::Vec3 ray =
            conservative_rotation *
            cubey::math::Vec3{ndc_x * tan_half_fov * aspect_ratio, -tan_half_fov, -1.0F};
        const float horizontal_length = std::sqrt(ray.x * ray.x + ray.z * ray.z);
        const cubey::math::Vec2 horizontal_direction{ray.x / horizontal_length,
                                                     ray.z / horizontal_length};
        const float vertical_slope = ray.y / horizontal_length;
        for (float distance_m = kForegroundSampleStepM; distance_m <= kForegroundClearDistanceM;
             distance_m += kForegroundSampleStepM) {
            const cubey::math::Vec2 position = anchor + horizontal_direction * distance_m;
            const float terrain_height_m =
                sample_terrain_height(source, {.world_xz = position}) * vertical_scale;
            const float ray_height_m = required_camera_height_m + distance_m * vertical_slope;
            minimum_margin_m = std::min(minimum_margin_m, ray_height_m - terrain_height_m);
        }
    }
    return {
        .camera_height_m = required_camera_height_m,
        .camera_clearance_m = required_camera_height_m - anchor_height_m,
        .minimum_margin_m = minimum_margin_m,
    };
}

[[nodiscard]] HeadingEvaluation evaluate_heading(const TerrainSourceParameters& clean_source,
                                                 cubey::math::Vec2 anchor, float yaw_radians,
                                                 float vertical_scale,
                                                 TerrainBackdropCameraProfile profile) {
    const bool far_field_product = profile == TerrainBackdropCameraProfile::Backdrop &&
                                   clean_source.version == TerrainSourceVersion::V2_1;
    const float heading_sign = clean_source.version == TerrainSourceVersion::V3 ? -1.0F : 1.0F;
    const cubey::math::Vec2 forward{heading_sign * std::sin(yaw_radians), -std::cos(yaw_radians)};
    const cubey::math::Vec2 right{std::cos(yaw_radians), heading_sign * std::sin(yaw_radians)};
    const float anchor_height =
        sample_terrain_height(clean_source, {.world_xz = anchor}) * vertical_scale;
    const float relief_scale = std::max(clean_source.height_scale_m * vertical_scale, 80.0F);

    float best_peak_score = -1.0F;
    const std::span<const float> distances = sample_distances(profile, far_field_product);
    float best_distance = distances.front();
    cubey::math::Vec2 best_target = anchor + forward * best_distance;
    float silhouette_sum = 0.0F;

    for (const float distance : distances) {
        std::array<float, 3> heights{};
        for (std::size_t lateral_index = 0; lateral_index < kLateralFactors.size();
             ++lateral_index) {
            const cubey::math::Vec2 position =
                anchor + forward * distance + right * (distance * kLateralFactors[lateral_index]);
            heights[lateral_index] =
                sample_terrain_height(clean_source, {.world_xz = position}) * vertical_scale;
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
            best_target =
                anchor + forward * distance + right * (distance * kLateralFactors[peak_index]);
        }

        silhouette_sum +=
            saturate((std::abs(heights[0] - heights[1]) + std::abs(heights[2] - heights[1]) +
                      0.5F * std::abs(heights[2] - heights[0])) /
                     (relief_scale * 0.30F));
    }

    const float peak_score = saturate(best_peak_score);
    const float silhouette_score = saturate(silhouette_sum / static_cast<float>(distances.size()));
    const float distance_score = useful_distance_score(best_distance);
    return {
        .score = 0.50F * peak_score + 0.25F * silhouette_score + 0.15F * distance_score,
        .target_distance_m = best_distance,
        .target_xz = best_target,
    };
}

[[nodiscard]] NearFrameOcclusion near_frame_occlusion(const TerrainSourceParameters& source,
                                                      cubey::math::Vec2 anchor,
                                                      float camera_height_m, float yaw_radians,
                                                      float pitch_radians, float test_distance_m,
                                                      float vertical_scale, float aspect_ratio) {
    const cubey::math::Quat rotation =
        cubey::math::angle_axis_quat(yaw_radians, {0.0F, 1.0F, 0.0F}) *
        cubey::math::angle_axis_quat(pitch_radians, {1.0F, 0.0F, 0.0F});
    const float tan_half_fov = std::tan(kBackdropVerticalFovRadians * 0.5F);
    std::uint32_t occluded_ray_count = 0U;
    float nearest_hit_distance_m = std::numeric_limits<float>::infinity();

    for (const float ndc_y : kNearFrameNdcY) {
        for (const float ndc_x : kNearFrameNdcX) {
            const cubey::math::Vec3 ray =
                rotation *
                cubey::math::Vec3{ndc_x * tan_half_fov * aspect_ratio, ndc_y * tan_half_fov, -1.0F};
            const float horizontal_length = std::sqrt(ray.x * ray.x + ray.z * ray.z);
            const cubey::math::Vec2 horizontal_direction{ray.x / horizontal_length,
                                                         ray.z / horizontal_length};
            const float vertical_slope = ray.y / horizontal_length;
            for (float distance_m = kNearFrameStartDistanceM; distance_m <= test_distance_m;
                 distance_m += kNearFrameSampleStepM) {
                const cubey::math::Vec2 position = anchor + horizontal_direction * distance_m;
                const float terrain_height_m =
                    sample_terrain_height(source, {.world_xz = position}) * vertical_scale;
                const float ray_height_m = camera_height_m + distance_m * vertical_slope;
                if (terrain_height_m < ray_height_m) {
                    continue;
                }
                ++occluded_ray_count;
                nearest_hit_distance_m = std::min(nearest_hit_distance_m, distance_m);
                break;
            }
        }
    }

    constexpr float ray_count = static_cast<float>(kNearFrameNdcX.size() * kNearFrameNdcY.size());
    return {
        .test_distance_m = test_distance_m,
        .occluded_ray_count = occluded_ray_count,
        .occupancy_ratio = static_cast<float>(occluded_ray_count) / ray_count,
        .nearest_hit_distance_m =
            std::isfinite(nearest_hit_distance_m) ? nearest_hit_distance_m : 0.0F,
    };
}

[[nodiscard]] float foreground_margin(const TerrainSourceParameters& source,
                                      cubey::math::Vec2 position_xz, float camera_height_m,
                                      float yaw_radians, float vertical_scale, float aspect_ratio) {
    const cubey::math::Quat rotation =
        cubey::math::angle_axis_quat(yaw_radians, {0.0F, 1.0F, 0.0F}) *
        cubey::math::angle_axis_quat(kMinimumPitchRadians, {1.0F, 0.0F, 0.0F});
    const float tan_half_fov = std::tan(kBackdropVerticalFovRadians * 0.5F);
    float minimum_margin_m = std::numeric_limits<float>::infinity();
    for (const float ndc_x : kLowerFrustumNdcX) {
        const cubey::math::Vec3 ray =
            rotation * cubey::math::Vec3{ndc_x * tan_half_fov * aspect_ratio, -tan_half_fov, -1.0F};
        const float horizontal_length = std::sqrt(ray.x * ray.x + ray.z * ray.z);
        const cubey::math::Vec2 horizontal_direction{ray.x / horizontal_length,
                                                     ray.z / horizontal_length};
        const float vertical_slope = ray.y / horizontal_length;
        for (float distance_m = kForegroundSampleStepM; distance_m <= kForegroundClearDistanceM;
             distance_m += kForegroundSampleStepM) {
            const cubey::math::Vec2 sample_position =
                position_xz + horizontal_direction * distance_m;
            const float terrain_height_m =
                sample_terrain_height(source, {.world_xz = sample_position}) * vertical_scale;
            const float ray_height_m = camera_height_m + distance_m * vertical_slope;
            minimum_margin_m = std::min(minimum_margin_m, ray_height_m - terrain_height_m);
        }
    }
    return minimum_margin_m;
}

[[nodiscard]] NearFrameOcclusion lower_frame_occlusion(const TerrainSourceParameters& source,
                                                       cubey::math::Vec2 position_xz,
                                                       float camera_height_m, float yaw_radians,
                                                       float pitch_radians, float vertical_scale,
                                                       float aspect_ratio) {
    const cubey::math::Quat rotation =
        cubey::math::angle_axis_quat(yaw_radians, {0.0F, 1.0F, 0.0F}) *
        cubey::math::angle_axis_quat(pitch_radians, {1.0F, 0.0F, 0.0F});
    const float tan_half_fov = std::tan(kBackdropVerticalFovRadians * 0.5F);
    std::uint32_t occluded_ray_count = 0U;
    float nearest_hit_distance_m = std::numeric_limits<float>::infinity();
    for (const float ndc_y : kLowerFrameNdcY) {
        for (const float ndc_x : kNearFrameNdcX) {
            const cubey::math::Vec3 ray =
                rotation *
                cubey::math::Vec3{ndc_x * tan_half_fov * aspect_ratio, ndc_y * tan_half_fov, -1.0F};
            const float horizontal_length = std::sqrt(ray.x * ray.x + ray.z * ray.z);
            const cubey::math::Vec2 horizontal_direction{ray.x / horizontal_length,
                                                         ray.z / horizontal_length};
            const float vertical_slope = ray.y / horizontal_length;
            for (float distance_m = kFarFieldLowerFrameStartDistanceM;
                 distance_m <= kFarFieldLowerFrameTestDistanceM;
                 distance_m += kNearFrameSampleStepM) {
                const cubey::math::Vec2 sample_position =
                    position_xz + horizontal_direction * distance_m;
                const float terrain_height_m =
                    sample_terrain_height(source, {.world_xz = sample_position}) * vertical_scale;
                const float ray_height_m = camera_height_m + distance_m * vertical_slope;
                if (terrain_height_m < ray_height_m) {
                    continue;
                }
                ++occluded_ray_count;
                nearest_hit_distance_m = std::min(nearest_hit_distance_m, distance_m);
                break;
            }
        }
    }
    constexpr float ray_count = static_cast<float>(kNearFrameNdcX.size());
    return {
        .test_distance_m = kFarFieldLowerFrameTestDistanceM,
        .occluded_ray_count = occluded_ray_count,
        .occupancy_ratio = static_cast<float>(occluded_ray_count) / ray_count,
        .nearest_hit_distance_m =
            std::isfinite(nearest_hit_distance_m) ? nearest_hit_distance_m : 0.0F,
    };
}

[[nodiscard]] SafeZoneEvaluation evaluate_safe_zone(const TerrainSourceParameters& source,
                                                    cubey::math::Vec2 anchor,
                                                    cubey::math::Vec2 target_xz,
                                                    float target_distance_m, float yaw_radians,
                                                    float vertical_scale, float aspect_ratio) {
    std::array<cubey::math::Vec2, 9> positions{};
    positions[0] = anchor;
    for (std::size_t index = 0; index < 8; ++index) {
        const float angle = static_cast<float>(index) * std::numbers::pi_v<float> / 4.0F;
        positions[index + 1] =
            anchor + cubey::math::Vec2{std::cos(angle), std::sin(angle)} * kFarFieldSafeZoneRadiusM;
    }
    constexpr std::array<float, 3> yaw_factors{-1.0F, 0.0F, 1.0F};

    float camera_clearance_m = kMinimumCameraClearanceM;
    float minimum_target_distance_m = std::numeric_limits<float>::infinity();
    for (const cubey::math::Vec2 position : positions) {
        const cubey::math::Vec2 target_offset = target_xz - position;
        minimum_target_distance_m =
            std::min(minimum_target_distance_m, std::sqrt(target_offset.x * target_offset.x +
                                                          target_offset.y * target_offset.y));
        const float local_height_m =
            sample_terrain_height(source, {.world_xz = position}) * vertical_scale;
        for (const float yaw_factor : yaw_factors) {
            const ForegroundClearance clearance =
                foreground_clearance(source, position, local_height_m,
                                     yaw_radians + yaw_factor * kFarFieldYawHalfAngleRadians,
                                     vertical_scale, aspect_ratio);
            camera_clearance_m = std::max(camera_clearance_m, clearance.camera_clearance_m);
        }
    }

    const float anchor_height_m =
        sample_terrain_height(source, {.world_xz = anchor}) * vertical_scale;
    const float camera_height_m = anchor_height_m + camera_clearance_m;
    const float target_height_m =
        sample_terrain_height(source, {.world_xz = target_xz}) * vertical_scale;
    const float target_elevation_radians =
        std::atan2(target_height_m - camera_height_m, target_distance_m);
    const float pitch_radians = std::clamp(target_elevation_radians - kPeakAboveFrameCenterRadians,
                                           kMinimumPitchRadians, kMaximumPitchRadians);

    float minimum_margin_m = std::numeric_limits<float>::infinity();
    std::uint32_t maximum_near_occlusion = 0U;
    std::uint32_t maximum_lower_occlusion = 0U;
    for (const cubey::math::Vec2 position : positions) {
        const float local_height_m =
            sample_terrain_height(source, {.world_xz = position}) * vertical_scale;
        const float local_camera_height_m = local_height_m + camera_clearance_m;
        for (const float yaw_factor : yaw_factors) {
            const float sample_yaw = yaw_radians + yaw_factor * kFarFieldYawHalfAngleRadians;
            minimum_margin_m = std::min(
                minimum_margin_m, foreground_margin(source, position, local_camera_height_m,
                                                    sample_yaw, vertical_scale, aspect_ratio));
            const NearFrameOcclusion near = near_frame_occlusion(
                source, position, local_camera_height_m, sample_yaw, pitch_radians,
                kFarFieldNearFrameTestDistanceM, vertical_scale, aspect_ratio);
            const NearFrameOcclusion lower =
                lower_frame_occlusion(source, position, local_camera_height_m, sample_yaw,
                                      pitch_radians, vertical_scale, aspect_ratio);
            maximum_near_occlusion = std::max(maximum_near_occlusion, near.occluded_ray_count);
            maximum_lower_occlusion = std::max(maximum_lower_occlusion, lower.occluded_ray_count);
        }
    }

    return {
        .camera_height_m = camera_height_m,
        .camera_clearance_m = camera_clearance_m,
        .target_height_m = target_height_m,
        .target_elevation_radians = target_elevation_radians,
        .pitch_radians = pitch_radians,
        .minimum_target_distance_m = minimum_target_distance_m,
        .foreground_minimum_margin_m = minimum_margin_m,
        .near_frame_max_occluded_ray_count = maximum_near_occlusion,
        .lower_frame_max_occluded_ray_count = maximum_lower_occlusion,
        .contract_satisfied = minimum_target_distance_m >= 3200.0F &&
                              minimum_margin_m >= kForegroundSafetyMarginM - 0.01F &&
                              maximum_near_occlusion == 0U &&
                              maximum_lower_occlusion <= kFarFieldMaximumLowerFrameOccludedRayCount,
    };
}

} // namespace

std::string_view
terrain_backdrop_camera_profile_name(TerrainBackdropCameraProfile profile) noexcept {
    return profile == TerrainBackdropCameraProfile::Midground ? "midground" : "backdrop";
}

TerrainBackdropCameraPlan plan_terrain_backdrop_camera(const TerrainSourceParameters& source,
                                                       float vertical_scale, float aspect_ratio,
                                                       TerrainBackdropCameraProfile profile) {
    if (!std::isfinite(vertical_scale) || vertical_scale <= 0.0F) {
        throw std::runtime_error("terrain backdrop camera vertical scale must be positive");
    }
    if (!std::isfinite(aspect_ratio) || aspect_ratio <= 0.0F) {
        throw std::runtime_error("terrain backdrop camera aspect ratio must be positive");
    }

    TerrainSourceParameters clean_source = source;
    clean_source.weathering = TerrainWeatheringMode::Off;
    clean_source.weathering_strength = 0.0F;
    const bool far_field_product = profile == TerrainBackdropCameraProfile::Backdrop &&
                                   source.version == TerrainSourceVersion::V2_1;

    std::vector<BackdropCandidate> candidates;
    candidates.reserve(kAnchorCoordinates.size() * kAnchorCoordinates.size() * kHeadingCount);
    for (const float anchor_z : kAnchorCoordinates) {
        for (const float anchor_x : kAnchorCoordinates) {
            const cubey::math::Vec2 anchor{anchor_x, anchor_z};
            for (std::uint32_t heading = 0U; heading < kHeadingCount; ++heading) {
                const float yaw = static_cast<float>(heading) * 2.0F * std::numbers::pi_v<float> /
                                  static_cast<float>(kHeadingCount);
                const HeadingEvaluation evaluation =
                    evaluate_heading(clean_source, anchor, yaw, vertical_scale, profile);
                candidates.push_back({.anchor = anchor, .yaw_radians = yaw, .heading = evaluation});
            }
        }
    }

    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const BackdropCandidate& left, const BackdropCandidate& right) {
                         return left.heading.score > right.heading.score;
                     });
    candidates.resize(std::min(candidates.size(), kFramingShortlistCount));

    TerrainBackdropCameraPlan best{};
    best.score = -std::numeric_limits<float>::infinity();
    bool best_is_admissible = false;
    std::size_t evaluated_safe_candidates = 0U;
    for (const BackdropCandidate& candidate : candidates) {
        if (far_field_product && evaluated_safe_candidates >= kFarFieldFramingCandidateCount) {
            break;
        }
        if (best_is_admissible && candidate.heading.score + 0.10F <= best.score) {
            break;
        }
        if (far_field_product) {
            ++evaluated_safe_candidates;
            const SafeZoneEvaluation safe_zone =
                evaluate_safe_zone(source, candidate.anchor, candidate.heading.target_xz,
                                   candidate.heading.target_distance_m, candidate.yaw_radians,
                                   vertical_scale, aspect_ratio);
            const float clearance_raise =
                std::max(safe_zone.camera_clearance_m - kMinimumCameraClearanceM, 0.0F);
            const float clearance_efficiency = 1.0F - saturate(clearance_raise / 300.0F);
            const float score = candidate.heading.score + 0.10F * clearance_efficiency;
            const NearFrameOcclusion center_occlusion =
                near_frame_occlusion(source, candidate.anchor, safe_zone.camera_height_m,
                                     candidate.yaw_radians, safe_zone.pitch_radians,
                                     kFarFieldNearFrameTestDistanceM, vertical_scale, aspect_ratio);
            const bool no_best = !std::isfinite(best.score);
            const std::uint32_t lower_excess = safe_zone.lower_frame_max_occluded_ray_count >
                                                       kFarFieldMaximumLowerFrameOccludedRayCount
                                                   ? safe_zone.lower_frame_max_occluded_ray_count -
                                                         kFarFieldMaximumLowerFrameOccludedRayCount
                                                   : 0U;
            const std::uint32_t best_lower_excess =
                best.safe_zone_lower_frame_max_occluded_ray_count >
                        kFarFieldMaximumLowerFrameOccludedRayCount
                    ? best.safe_zone_lower_frame_max_occluded_ray_count -
                          kFarFieldMaximumLowerFrameOccludedRayCount
                    : 0U;
            const std::uint32_t violations =
                safe_zone.near_frame_max_occluded_ray_count * 8U + lower_excess;
            const std::uint32_t best_violations =
                best.safe_zone_near_frame_max_occluded_ray_count * 8U + best_lower_excess;
            const bool should_select =
                safe_zone.contract_satisfied
                    ? !best_is_admissible
                    : (!best_is_admissible &&
                       (no_best || violations < best_violations ||
                        (violations == best_violations && score > best.score)));
            if (!should_select) {
                continue;
            }

            best_is_admissible = safe_zone.contract_satisfied;
            best = {
                .transform =
                    {
                        .translation = {candidate.anchor.x, safe_zone.camera_height_m,
                                        candidate.anchor.y},
                        .rotation = cubey::math::angle_axis_quat(candidate.yaw_radians,
                                                                 {0.0F, 1.0F, 0.0F}) *
                                    cubey::math::angle_axis_quat(safe_zone.pitch_radians,
                                                                 {1.0F, 0.0F, 0.0F}),
                    },
                .anchor_xz = candidate.anchor,
                .target_position = {candidate.heading.target_xz.x, safe_zone.target_height_m,
                                    candidate.heading.target_xz.y},
                .yaw_radians = candidate.yaw_radians,
                .pitch_radians = safe_zone.pitch_radians,
                .target_distance_m = candidate.heading.target_distance_m,
                .minimum_target_distance_m = safe_zone.minimum_target_distance_m,
                .target_elevation_radians = safe_zone.target_elevation_radians,
                .camera_clearance_m = safe_zone.camera_clearance_m,
                .clearance_raise_m = clearance_raise,
                .foreground_clear_distance_m = kForegroundClearDistanceM,
                .foreground_min_margin_m = safe_zone.foreground_minimum_margin_m,
                .near_frame_test_distance_m = center_occlusion.test_distance_m,
                .near_frame_occluded_ray_count = center_occlusion.occluded_ray_count,
                .near_frame_occupancy_ratio = center_occlusion.occupancy_ratio,
                .near_frame_nearest_hit_distance_m = center_occlusion.nearest_hit_distance_m,
                .safe_zone_radius_m =
                    safe_zone.contract_satisfied ? kFarFieldSafeZoneRadiusM : 0.0F,
                .yaw_half_angle_radians =
                    safe_zone.contract_satisfied ? kFarFieldYawHalfAngleRadians : 0.0F,
                .safe_zone_foreground_min_margin_m = safe_zone.foreground_minimum_margin_m,
                .safe_zone_near_frame_test_distance_m = kFarFieldNearFrameTestDistanceM,
                .safe_zone_near_frame_max_occluded_ray_count =
                    safe_zone.near_frame_max_occluded_ray_count,
                .safe_zone_lower_frame_test_distance_m = kFarFieldLowerFrameTestDistanceM,
                .safe_zone_lower_frame_max_occluded_ray_count =
                    safe_zone.lower_frame_max_occluded_ray_count,
                .far_field_contract_satisfied = safe_zone.contract_satisfied,
                .aspect_ratio = aspect_ratio,
                .score = score,
            };
            if (safe_zone.contract_satisfied) {
                break;
            }
            continue;
        }

        const float anchor_height =
            sample_terrain_height(source, {.world_xz = candidate.anchor}) * vertical_scale;
        const ForegroundClearance clearance =
            foreground_clearance(source, candidate.anchor, anchor_height, candidate.yaw_radians,
                                 vertical_scale, aspect_ratio);
        const float clearance_raise =
            std::max(clearance.camera_clearance_m - kMinimumCameraClearanceM, 0.0F);
        const float clearance_efficiency = 1.0F - saturate(clearance_raise / 300.0F);
        const float score = candidate.heading.score + 0.10F * clearance_efficiency;
        const float target_height =
            sample_terrain_height(source, {.world_xz = candidate.heading.target_xz}) *
            vertical_scale;
        const float elevation = std::atan2(target_height - clearance.camera_height_m,
                                           candidate.heading.target_distance_m);
        const float maximum_pitch = source.version == TerrainSourceVersion::V3
                                        ? kHierarchicalMaximumPitchRadians
                                        : kMaximumPitchRadians;
        const float pitch = std::clamp(elevation - kPeakAboveFrameCenterRadians,
                                       kMinimumPitchRadians, maximum_pitch);
        const NearFrameOcclusion occlusion = near_frame_occlusion(
            source, candidate.anchor, clearance.camera_height_m, candidate.yaw_radians, pitch,
            candidate.heading.target_distance_m * 0.75F, vertical_scale, aspect_ratio);
        const bool is_admissible =
            occlusion.occluded_ray_count <= kMaximumNearFrameOccludedRayCount;
        const bool lower_fallback_occupancy =
            !is_admissible && !best_is_admissible &&
            occlusion.occluded_ray_count < best.near_frame_occluded_ray_count;
        const bool equal_fallback_occupancy =
            !is_admissible && !best_is_admissible &&
            occlusion.occluded_ray_count == best.near_frame_occluded_ray_count;
        const bool no_best = !std::isfinite(best.score);
        const bool should_select =
            is_admissible
                ? (!best_is_admissible || score > best.score)
                : (!best_is_admissible && (no_best || lower_fallback_occupancy ||
                                           (equal_fallback_occupancy && score > best.score)));
        if (!should_select) {
            continue;
        }

        best_is_admissible = is_admissible;
        best = {
            .transform =
                {
                    .translation = {candidate.anchor.x, clearance.camera_height_m,
                                    candidate.anchor.y},
                    .rotation =
                        cubey::math::angle_axis_quat(candidate.yaw_radians, {0.0F, 1.0F, 0.0F}) *
                        cubey::math::angle_axis_quat(pitch, {1.0F, 0.0F, 0.0F}),
                },
            .anchor_xz = candidate.anchor,
            .target_position = {candidate.heading.target_xz.x, target_height,
                                candidate.heading.target_xz.y},
            .yaw_radians = candidate.yaw_radians,
            .pitch_radians = pitch,
            .target_distance_m = candidate.heading.target_distance_m,
            .minimum_target_distance_m = candidate.heading.target_distance_m,
            .target_elevation_radians = elevation,
            .camera_clearance_m = clearance.camera_clearance_m,
            .clearance_raise_m = clearance_raise,
            .foreground_clear_distance_m = kForegroundClearDistanceM,
            .foreground_min_margin_m = clearance.minimum_margin_m,
            .near_frame_test_distance_m = occlusion.test_distance_m,
            .near_frame_occluded_ray_count = occlusion.occluded_ray_count,
            .near_frame_occupancy_ratio = occlusion.occupancy_ratio,
            .near_frame_nearest_hit_distance_m = occlusion.nearest_hit_distance_m,
            .aspect_ratio = aspect_ratio,
            .score = score,
        };
    }
    return best;
}

} // namespace cubey::projects::terrain
