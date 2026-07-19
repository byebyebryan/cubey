#include "terrain_backdrop_stage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/geometric.hpp>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace cubey::projects::terrain {
namespace {

constexpr float kDegreesToRadians = std::numbers::pi_v<float> / 180.0F;
constexpr std::uint32_t kPanoramaSectorCount = 24U;
constexpr std::size_t kCoarseShortlistCount = 24U;
constexpr std::size_t kFineShortlistCount = 8U;
constexpr std::size_t kFullCandidateCount = 16U;
constexpr float kLowerFrameTraceStartM = 100.0F;
constexpr float kLowerFrameTraceStepM = 50.0F;
constexpr float kLowerFrameTraceMaximumM = 6'600.0F;
constexpr float kLowerFrameHeightMarginM = 20.0F;
constexpr std::array<float, 5> kLowerFrameNdcX{-1.0F, -0.5F, 0.0F, 0.5F, 1.0F};
constexpr float kReliefNearDistanceM = 3'200.0F;
constexpr float kReliefMiddleDistanceM = 4'800.0F;
constexpr float kReliefFarDistanceM = 6'600.0F;
constexpr float kRequiredReliefM = 250.0F;
constexpr std::uint32_t kRequiredReliefSectorCount = 14U;
constexpr float kGroundedMaximumReliefM = 40.0F;
constexpr float kGroundedMaximumP95Slope = 0.15F;
constexpr float kGroundedMinimumCameraClearanceM = 10.0F;

struct SearchCandidate {
    cubey::math::Vec2 focus{0.0F, 0.0F};
    float score = -std::numeric_limits<float>::infinity();
};

struct LocalSummary {
    float center_height_m = 0.0F;
    float minimum_height_m = 0.0F;
    float maximum_height_m = 0.0F;
    float guard_maximum_height_m = 0.0F;
    float relief_m = 0.0F;
    float p95_slope = 0.0F;
};

[[nodiscard]] cubey::math::Vec2 direction_from_yaw(float yaw_radians) {
    return {std::sin(yaw_radians), -std::cos(yaw_radians)};
}

[[nodiscard]] float length(cubey::math::Vec2 value) {
    return std::sqrt(value.x * value.x + value.y * value.y);
}

[[nodiscard]] float sample_height(const TerrainHeightSource& source, cubey::math::Vec2 source_xz,
                                  float footprint_m, float vertical_scale) {
    return source.sample_height({.world_xz = source_xz, .footprint_m = footprint_m}) *
           vertical_scale;
}

[[nodiscard]] bool same_focus(cubey::math::Vec2 lhs, cubey::math::Vec2 rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

void append_unique(std::vector<cubey::math::Vec2>& values, cubey::math::Vec2 value) {
    if (std::none_of(values.begin(), values.end(),
                     [value](cubey::math::Vec2 existing) { return same_focus(existing, value); })) {
        values.push_back(value);
    }
}

[[nodiscard]] float cheap_candidate_score(const TerrainHeightSource& source,
                                          const TerrainBackdropStageRequest& request,
                                          cubey::math::Vec2 focus) {
    constexpr std::uint32_t ring_sample_count = 8U;
    const float center = sample_height(source, focus, 64.0F, request.vertical_scale);
    float minimum = center;
    float maximum = center;
    float panorama_score = 0.0F;
    std::uint32_t relief_count = 0U;
    for (std::uint32_t index = 0U; index < ring_sample_count; ++index) {
        const float yaw = static_cast<float>(index) * 2.0F * std::numbers::pi_v<float> /
                          static_cast<float>(ring_sample_count);
        const cubey::math::Vec2 direction = direction_from_yaw(yaw);
        const float local_height = sample_height(source, focus + direction * request.stage_radius_m,
                                                 64.0F, request.vertical_scale);
        minimum = std::min(minimum, local_height);
        maximum = std::max(maximum, local_height);

        const float middle = sample_height(source, focus + direction * kReliefMiddleDistanceM,
                                           128.0F, request.vertical_scale);
        const float far = sample_height(source, focus + direction * kReliefFarDistanceM, 128.0F,
                                        request.vertical_scale);
        const float prominence = std::max(middle, far) - local_height;
        panorama_score += std::clamp(prominence / 600.0F, -1.0F, 2.0F);
        relief_count += prominence >= kRequiredReliefM ? 1U : 0U;
    }
    const float local_relief = maximum - minimum;
    const float flatness = 1.0F - std::clamp(local_relief / 160.0F, 0.0F, 1.0F);
    const float panorama = panorama_score / static_cast<float>(ring_sample_count);
    const float coverage = static_cast<float>(relief_count) / static_cast<float>(ring_sample_count);
    return panorama + 1.5F * coverage +
           (request.mode == TerrainBackdropStageMode::Grounded ? 2.5F * flatness
                                                               : 0.25F * flatness);
}

[[nodiscard]] std::vector<SearchCandidate>
score_candidates(const TerrainHeightSource& source, const TerrainBackdropStageRequest& request,
                 const std::vector<cubey::math::Vec2>& focuses) {
    std::vector<SearchCandidate> result;
    result.reserve(focuses.size());
    for (const cubey::math::Vec2 focus : focuses) {
        result.push_back({.focus = focus, .score = cheap_candidate_score(source, request, focus)});
    }
    std::sort(result.begin(), result.end(),
              [](const SearchCandidate& lhs, const SearchCandidate& rhs) {
                  if (lhs.score != rhs.score) {
                      return lhs.score > rhs.score;
                  }
                  if (lhs.focus.x != rhs.focus.x) {
                      return lhs.focus.x < rhs.focus.x;
                  }
                  return lhs.focus.y < rhs.focus.y;
              });
    return result;
}

[[nodiscard]] LocalSummary local_summary(const TerrainHeightSource& source,
                                         const TerrainBackdropStageRequest& request,
                                         cubey::math::Vec2 focus) {
    LocalSummary result{};
    result.center_height_m = sample_height(source, focus, 16.0F, request.vertical_scale);
    result.minimum_height_m = result.center_height_m;
    result.maximum_height_m = result.center_height_m;
    std::vector<float> slopes;
    for (int z = -3; z <= 3; ++z) {
        for (int x = -3; x <= 3; ++x) {
            const cubey::math::Vec2 offset{static_cast<float>(x) * 100.0F,
                                           static_cast<float>(z) * 100.0F};
            if (length(offset) > request.stage_radius_m) {
                continue;
            }
            const TerrainSample sample =
                source.sample({.world_xz = focus + offset, .footprint_m = 16.0F});
            const float height = sample.height_m * request.vertical_scale;
            result.minimum_height_m = std::min(result.minimum_height_m, height);
            result.maximum_height_m = std::max(result.maximum_height_m, height);
            slopes.push_back(length(sample.gradient_xz) * request.vertical_scale);
        }
    }
    result.relief_m = result.maximum_height_m - result.minimum_height_m;
    std::sort(slopes.begin(), slopes.end());
    const std::size_t p95_index = std::min(
        slopes.size() - 1U,
        static_cast<std::size_t>(std::ceil(static_cast<float>(slopes.size()) * 0.95F)) - 1U);
    result.p95_slope = slopes[p95_index];
    result.guard_maximum_height_m = result.maximum_height_m;
    for (std::uint32_t index = 0U; index < 16U; ++index) {
        const float yaw = static_cast<float>(index) * 2.0F * std::numbers::pi_v<float> / 16.0F;
        result.guard_maximum_height_m =
            std::max(result.guard_maximum_height_m,
                     sample_height(source, focus + direction_from_yaw(yaw) * request.guard_radius_m,
                                   32.0F, request.vertical_scale));
    }
    return result;
}

struct OrbitCameraSample {
    cubey::math::Vec2 local_xz{0.0F, 0.0F};
    float height_m = 0.0F;
    cubey::math::Vec3 forward{0.0F, 0.0F, -1.0F};
};

struct LowerFrameEnvelope {
    float required_target_height_m = -std::numeric_limits<float>::infinity();
    float minimum_terrain_distance_m = kLowerFrameTraceMaximumM;
    std::uint32_t clear_sector_count = 0U;
};

[[nodiscard]] OrbitCameraSample orbit_camera_sample(float yaw_radians, float radius_m,
                                                    float elevation_radians,
                                                    float target_height_m) {
    const cubey::math::Vec2 background_direction = direction_from_yaw(yaw_radians);
    const float horizontal_radius = std::cos(elevation_radians) * radius_m;
    const cubey::math::Vec2 camera_xz = background_direction * -horizontal_radius;
    const float camera_height = target_height_m + std::sin(elevation_radians) * radius_m;
    const cubey::math::Vec3 to_target{-camera_xz.x, target_height_m - camera_height, -camera_xz.y};
    const float forward_length = std::sqrt(to_target.x * to_target.x + to_target.y * to_target.y +
                                           to_target.z * to_target.z);
    return {
        .local_xz = camera_xz,
        .height_m = camera_height,
        .forward = to_target / forward_length,
    };
}

[[nodiscard]] cubey::math::Vec3 lower_frame_ray(const OrbitCameraSample& camera,
                                                const TerrainBackdropStageRequest& request,
                                                float ndc_x) {
    const cubey::math::Vec3 right =
        glm::normalize(glm::cross(camera.forward, cubey::math::Vec3{0.0F, 1.0F, 0.0F}));
    const cubey::math::Vec3 up = glm::normalize(glm::cross(right, camera.forward));
    const float tan_half_fovy = std::tan(request.vertical_fov_radians * 0.5F);
    return glm::normalize(camera.forward + right * (ndc_x * request.aspect_ratio * tan_half_fovy) -
                          up * tan_half_fovy);
}

[[nodiscard]] float required_detached_target_height(const TerrainHeightSource& source,
                                                    const TerrainBackdropStageRequest& request,
                                                    cubey::math::Vec2 focus) {
    float required_height = -std::numeric_limits<float>::infinity();
    const std::array<float, 3> radii{request.orbit_min_radius_m, request.orbit_default_radius_m,
                                     request.orbit_max_radius_m};
    const std::array<float, 3> elevations{request.orbit_min_elevation_radians,
                                          request.orbit_default_elevation_radians,
                                          request.orbit_max_elevation_radians};
    for (std::uint32_t sector = 0U; sector < kPanoramaSectorCount; ++sector) {
        const float yaw = static_cast<float>(sector) * 2.0F * std::numbers::pi_v<float> /
                          static_cast<float>(kPanoramaSectorCount);
        for (const float radius : radii) {
            for (const float elevation : elevations) {
                const OrbitCameraSample camera = orbit_camera_sample(yaw, radius, elevation, 0.0F);
                for (const float ndc_x : kLowerFrameNdcX) {
                    const cubey::math::Vec3 ray = lower_frame_ray(camera, request, ndc_x);
                    for (float distance_m = kLowerFrameTraceStartM;
                         distance_m <= request.minimum_visible_terrain_distance_m;
                         distance_m += kLowerFrameTraceStepM) {
                        const cubey::math::Vec2 local_position =
                            camera.local_xz + cubey::math::Vec2{ray.x, ray.z} * distance_m;
                        if (length(local_position) < request.stage_radius_m) {
                            continue;
                        }
                        const float terrain_height = sample_height(source, focus + local_position,
                                                                   32.0F, request.vertical_scale);
                        required_height =
                            std::max(required_height, terrain_height + kLowerFrameHeightMarginM -
                                                          camera.height_m - ray.y * distance_m);
                    }
                }
            }
        }
    }
    if (!std::isfinite(required_height)) {
        throw std::runtime_error("terrain backdrop lower-frame solver sampled no terrain");
    }
    return required_height;
}

[[nodiscard]] float lower_frame_terrain_distance(const TerrainHeightSource& source,
                                                 const TerrainBackdropStageRequest& request,
                                                 cubey::math::Vec2 focus, float target_height_m,
                                                 float yaw_radians, float radius_m,
                                                 float elevation_radians) {
    const OrbitCameraSample camera =
        orbit_camera_sample(yaw_radians, radius_m, elevation_radians, target_height_m);
    float first_hit = kLowerFrameTraceMaximumM;
    for (const float ndc_x : kLowerFrameNdcX) {
        const cubey::math::Vec3 ray = lower_frame_ray(camera, request, ndc_x);
        for (float distance_m = kLowerFrameTraceStartM; distance_m <= kLowerFrameTraceMaximumM;
             distance_m += kLowerFrameTraceStepM) {
            const cubey::math::Vec2 local_position =
                camera.local_xz + cubey::math::Vec2{ray.x, ray.z} * distance_m;
            if (request.mode == TerrainBackdropStageMode::Detached &&
                length(local_position) < request.stage_radius_m) {
                continue;
            }
            const float terrain_height =
                sample_height(source, focus + local_position, 32.0F, request.vertical_scale);
            const float ray_height = camera.height_m + ray.y * distance_m;
            if (terrain_height >= ray_height) {
                first_hit = std::min(first_hit, distance_m);
                break;
            }
        }
    }
    return first_hit;
}

[[nodiscard]] LowerFrameEnvelope
evaluate_lower_frame_envelope(const TerrainHeightSource& source,
                              const TerrainBackdropStageRequest& request, cubey::math::Vec2 focus,
                              float target_height_m) {
    LowerFrameEnvelope result;
    result.required_target_height_m = target_height_m;
    const std::array<float, 3> radii{request.orbit_min_radius_m, request.orbit_default_radius_m,
                                     request.orbit_max_radius_m};
    const std::array<float, 3> elevations{request.orbit_min_elevation_radians,
                                          request.orbit_default_elevation_radians,
                                          request.orbit_max_elevation_radians};
    for (std::uint32_t sector = 0U; sector < kPanoramaSectorCount; ++sector) {
        const float yaw = static_cast<float>(sector) * 2.0F * std::numbers::pi_v<float> /
                          static_cast<float>(kPanoramaSectorCount);
        const float default_distance = lower_frame_terrain_distance(
            source, request, focus, target_height_m, yaw, request.orbit_default_radius_m,
            request.orbit_default_elevation_radians);
        result.clear_sector_count +=
            default_distance >= request.minimum_visible_terrain_distance_m ? 1U : 0U;
        result.minimum_terrain_distance_m =
            std::min(result.minimum_terrain_distance_m, default_distance);
        for (const float radius : radii) {
            for (const float elevation : elevations) {
                result.minimum_terrain_distance_m =
                    std::min(result.minimum_terrain_distance_m,
                             lower_frame_terrain_distance(source, request, focus, target_height_m,
                                                          yaw, radius, elevation));
            }
        }
    }
    return result;
}

[[nodiscard]] TerrainBackdropStagePlan
evaluate_full_candidate(const TerrainHeightSource& source,
                        const TerrainBackdropStageRequest& request, cubey::math::Vec2 focus) {
    const LocalSummary local = local_summary(source, request, focus);
    const float target_height = request.mode == TerrainBackdropStageMode::Detached
                                    ? required_detached_target_height(source, request, focus)
                                    : local.center_height_m + request.subject_center_height_m;
    const float stage_plane = target_height - request.subject_center_height_m;
    const LowerFrameEnvelope lower_frame =
        evaluate_lower_frame_envelope(source, request, focus, target_height);

    std::uint32_t relief_sector_count = 0U;
    float best_sector_score = -std::numeric_limits<float>::infinity();
    float showcase_yaw = 0.0F;
    float prominence_sum = 0.0F;
    for (std::uint32_t sector = 0U; sector < kPanoramaSectorCount; ++sector) {
        const float yaw = static_cast<float>(sector) * 2.0F * std::numbers::pi_v<float> /
                          static_cast<float>(kPanoramaSectorCount);
        const cubey::math::Vec2 direction = direction_from_yaw(yaw);
        const float near_height = sample_height(source, focus + direction * kReliefNearDistanceM,
                                                96.0F, request.vertical_scale);
        const float middle_height = sample_height(
            source, focus + direction * kReliefMiddleDistanceM, 128.0F, request.vertical_scale);
        const float far_height = sample_height(source, focus + direction * kReliefFarDistanceM,
                                               160.0F, request.vertical_scale);
        const float prominence = std::max(middle_height, far_height) - near_height;
        prominence_sum += prominence;
        relief_sector_count += prominence >= kRequiredReliefM ? 1U : 0U;
        if (prominence > best_sector_score) {
            best_sector_score = prominence;
            showcase_yaw = terrain_backdrop_camera_yaw_for_source_direction(yaw);
        }
    }

    float minimum_camera_clearance = std::numeric_limits<float>::infinity();
    constexpr std::array<float, 3> endpoints{0.0F, 0.5F, 1.0F};
    for (std::uint32_t azimuth = 0U; azimuth < kPanoramaSectorCount; ++azimuth) {
        const float yaw = static_cast<float>(azimuth) * 2.0F * std::numbers::pi_v<float> /
                          static_cast<float>(kPanoramaSectorCount);
        for (const float radius_endpoint : endpoints) {
            const float radius =
                std::lerp(request.orbit_min_radius_m, request.orbit_max_radius_m, radius_endpoint);
            for (const float elevation_endpoint : endpoints) {
                const float elevation =
                    std::lerp(request.orbit_min_elevation_radians,
                              request.orbit_max_elevation_radians, elevation_endpoint);
                const OrbitCameraSample camera =
                    orbit_camera_sample(yaw, radius, elevation, target_height);
                const float terrain_height =
                    sample_height(source, focus + camera.local_xz, 16.0F, request.vertical_scale);
                minimum_camera_clearance =
                    std::min(minimum_camera_clearance, camera.height_m - terrain_height);
            }
        }
    }

    const bool panoramic_contract =
        lower_frame.clear_sector_count == kPanoramaSectorCount &&
        lower_frame.minimum_terrain_distance_m >= request.minimum_visible_terrain_distance_m &&
        relief_sector_count >= kRequiredReliefSectorCount;
    const bool grounded_contract = local.relief_m <= kGroundedMaximumReliefM &&
                                   local.p95_slope <= kGroundedMaximumP95Slope &&
                                   minimum_camera_clearance >= kGroundedMinimumCameraClearanceM;
    const bool contract =
        request.mode == TerrainBackdropStageMode::Detached ? panoramic_contract : grounded_contract;
    const float panorama_score =
        static_cast<float>(lower_frame.clear_sector_count) /
            static_cast<float>(kPanoramaSectorCount) +
        1.5F * static_cast<float>(relief_sector_count) / static_cast<float>(kPanoramaSectorCount) +
        std::clamp(prominence_sum / (static_cast<float>(kPanoramaSectorCount) * 600.0F), -1.0F,
                   2.0F) -
        0.5F * std::clamp((target_height - local.guard_maximum_height_m) / 1'500.0F, 0.0F, 2.0F);
    const float grounded_score = 2.0F * (1.0F - std::clamp(local.relief_m / 160.0F, 0.0F, 1.0F)) +
                                 2.0F * (1.0F - std::clamp(local.p95_slope / 0.6F, 0.0F, 1.0F));
    return {
        .mode = request.mode,
        .source_focus_xz = focus,
        .source_center_height_m = local.center_height_m,
        .stage_plane_height_m = stage_plane,
        .target_height_m = target_height,
        .terrain_vertical_offset_m = -target_height,
        .local_relief_m = local.relief_m,
        .local_p95_slope = local.p95_slope,
        .minimum_camera_clearance_m = minimum_camera_clearance,
        .showcase_yaw_radians = showcase_yaw,
        .stage_radius_m = request.stage_radius_m,
        .orbit_min_radius_m = request.orbit_min_radius_m,
        .orbit_default_radius_m = request.orbit_default_radius_m,
        .orbit_max_radius_m = request.orbit_max_radius_m,
        .orbit_min_elevation_radians = request.orbit_min_elevation_radians,
        .orbit_default_elevation_radians = request.orbit_default_elevation_radians,
        .orbit_max_elevation_radians = request.orbit_max_elevation_radians,
        .panorama_sector_count = kPanoramaSectorCount,
        .lower_frame_clear_sector_count = lower_frame.clear_sector_count,
        .relief_sector_count = relief_sector_count,
        .minimum_lower_frame_terrain_distance_m = lower_frame.minimum_terrain_distance_m,
        .contract_satisfied = contract,
        .score = panorama_score +
                 (request.mode == TerrainBackdropStageMode::Grounded ? grounded_score : 0.0F),
    };
}

[[nodiscard]] bool plan_is_better(const TerrainBackdropStagePlan& lhs,
                                  const TerrainBackdropStagePlan& rhs) {
    if (lhs.contract_satisfied != rhs.contract_satisfied) {
        return lhs.contract_satisfied;
    }
    if (lhs.score != rhs.score) {
        return lhs.score > rhs.score;
    }
    if (lhs.source_focus_xz.x != rhs.source_focus_xz.x) {
        return lhs.source_focus_xz.x < rhs.source_focus_xz.x;
    }
    return lhs.source_focus_xz.y < rhs.source_focus_xz.y;
}

void validate_request(const TerrainBackdropStageRequest& request) {
    if (!std::isfinite(request.stage_radius_m) || !std::isfinite(request.guard_radius_m) ||
        !std::isfinite(request.orbit_min_radius_m) ||
        !std::isfinite(request.orbit_default_radius_m) ||
        !std::isfinite(request.orbit_max_radius_m) ||
        !std::isfinite(request.orbit_min_elevation_radians) ||
        !std::isfinite(request.orbit_default_elevation_radians) ||
        !std::isfinite(request.orbit_max_elevation_radians) ||
        !std::isfinite(request.subject_center_height_m) ||
        !std::isfinite(request.minimum_visible_terrain_distance_m) ||
        !std::isfinite(request.search_extent_m) || !std::isfinite(request.search_step_m) ||
        !std::isfinite(request.vertical_fov_radians) || !std::isfinite(request.aspect_ratio) ||
        !std::isfinite(request.vertical_scale) || request.stage_radius_m <= 0.0F ||
        request.guard_radius_m < request.stage_radius_m || request.orbit_min_radius_m <= 0.0F ||
        request.orbit_default_radius_m < request.orbit_min_radius_m ||
        request.orbit_default_radius_m > request.orbit_max_radius_m ||
        request.orbit_min_elevation_radians < 0.0F ||
        request.orbit_default_elevation_radians < request.orbit_min_elevation_radians ||
        request.orbit_default_elevation_radians > request.orbit_max_elevation_radians ||
        request.orbit_max_elevation_radians >= std::numbers::pi_v<float> * 0.5F ||
        request.subject_center_height_m < 0.0F ||
        request.minimum_visible_terrain_distance_m <= request.stage_radius_m ||
        request.search_extent_m < 0.0F || request.search_step_m <= 0.0F ||
        request.vertical_fov_radians <= 0.0F ||
        request.vertical_fov_radians >= std::numbers::pi_v<float> || request.aspect_ratio <= 0.0F ||
        request.vertical_scale <= 0.0F) {
        throw std::runtime_error("invalid terrain backdrop stage request");
    }
}

} // namespace

std::string_view terrain_backdrop_stage_mode_name(TerrainBackdropStageMode mode) noexcept {
    return mode == TerrainBackdropStageMode::Detached ? "detached" : "grounded";
}

TerrainBackdropStageMode terrain_backdrop_stage_mode_from_name(std::string_view name) {
    if (name.empty() || name == "detached") {
        return TerrainBackdropStageMode::Detached;
    }
    if (name == "grounded") {
        return TerrainBackdropStageMode::Grounded;
    }
    throw std::runtime_error("unknown terrain backdrop stage mode: " + std::string(name));
}

TerrainBackdropStageRequest terrain_backdrop_stage_request(TerrainBackdropStageMode mode,
                                                           float aspect_ratio,
                                                           float vertical_scale) {
    TerrainBackdropStageRequest result{
        .mode = mode,
        .orbit_min_elevation_radians =
            (mode == TerrainBackdropStageMode::Detached ? 0.0F : 12.0F) * kDegreesToRadians,
        .orbit_default_elevation_radians =
            (mode == TerrainBackdropStageMode::Detached ? 8.0F : 20.0F) * kDegreesToRadians,
        .orbit_max_elevation_radians =
            (mode == TerrainBackdropStageMode::Detached ? 30.0F : 32.0F) * kDegreesToRadians,
        .vertical_fov_radians = 40.0F * kDegreesToRadians,
        .aspect_ratio = aspect_ratio,
        .vertical_scale = vertical_scale,
    };
    validate_request(result);
    return result;
}

TerrainBackdropStagePlan plan_terrain_backdrop_stage(const TerrainHeightSource& source,
                                                     const TerrainBackdropStageRequest& request) {
    validate_request(request);
    std::vector<cubey::math::Vec2> coarse_focuses;
    for (float z = -request.search_extent_m; z <= request.search_extent_m;
         z += request.search_step_m) {
        for (float x = -request.search_extent_m; x <= request.search_extent_m;
             x += request.search_step_m) {
            coarse_focuses.push_back({x, z});
        }
    }
    const std::vector<SearchCandidate> coarse = score_candidates(source, request, coarse_focuses);

    std::vector<cubey::math::Vec2> medium_focuses;
    const std::size_t coarse_count = std::min(kCoarseShortlistCount, coarse.size());
    for (std::size_t candidate = 0U; candidate < coarse_count; ++candidate) {
        for (float z : {-1'000.0F, 0.0F, 1'000.0F}) {
            for (float x : {-1'000.0F, 0.0F, 1'000.0F}) {
                append_unique(medium_focuses, coarse[candidate].focus + cubey::math::Vec2{x, z});
            }
        }
    }
    const std::vector<SearchCandidate> medium = score_candidates(source, request, medium_focuses);

    std::vector<cubey::math::Vec2> fine_focuses;
    const std::size_t medium_count = std::min(kFineShortlistCount, medium.size());
    for (std::size_t candidate = 0U; candidate < medium_count; ++candidate) {
        for (float z : {-500.0F, -250.0F, 0.0F, 250.0F, 500.0F}) {
            for (float x : {-500.0F, -250.0F, 0.0F, 250.0F, 500.0F}) {
                append_unique(fine_focuses, medium[candidate].focus + cubey::math::Vec2{x, z});
            }
        }
    }
    const std::vector<SearchCandidate> fine = score_candidates(source, request, fine_focuses);
    if (fine.empty()) {
        throw std::runtime_error("terrain backdrop stage search produced no candidates");
    }

    std::vector<TerrainBackdropStagePlan> full_plans;
    const std::size_t full_count = std::min(kFullCandidateCount, fine.size());
    full_plans.reserve(full_count);
    for (std::size_t candidate = 0U; candidate < full_count; ++candidate) {
        TerrainBackdropStagePlan plan =
            evaluate_full_candidate(source, request, fine[candidate].focus);
        plan.coarse_candidate_count = static_cast<std::uint32_t>(coarse.size());
        plan.refined_candidate_count = static_cast<std::uint32_t>(medium.size() + fine.size());
        plan.full_candidate_count = static_cast<std::uint32_t>(full_count);
        full_plans.push_back(plan);
    }
    std::sort(full_plans.begin(), full_plans.end(), plan_is_better);
    return full_plans.front();
}

TerrainBackdropStagePlan plan_terrain_backdrop_stage(const TerrainSourceParameters& source,
                                                     const TerrainBackdropStageRequest& request,
                                                     std::uint64_t seed) {
    const ParameterTerrainHeightSource adapter(source, seed);
    return plan_terrain_backdrop_stage(adapter, request);
}

} // namespace cubey::projects::terrain
