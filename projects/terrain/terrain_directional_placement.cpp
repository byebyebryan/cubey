#include "terrain_directional_placement.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace cubey::projects::terrain {
namespace {

constexpr float kTwoPi = 2.0F * std::numbers::pi_v<float>;
constexpr float kMediumRefinementOffsetM = 1'000.0F;
constexpr float kFineRefinementOffsetM = 500.0F;
constexpr std::size_t kCoarseShortlistCount = 24U;
constexpr std::size_t kFineShortlistCount = 8U;
constexpr std::size_t kFullCandidateCount = 16U;
static_assert(kMediumRefinementOffsetM + kFineRefinementOffsetM ==
              terrain_directional_placement_maximum_refinement_offset_m());

struct Candidate {
    cubey::math::Vec2 focus{0.0F, 0.0F};
    bool contract_satisfied = false;
    float score = -std::numeric_limits<float>::infinity();
};

[[nodiscard]] cubey::math::Vec2 direction_from_yaw(float yaw) {
    return {std::sin(yaw), -std::cos(yaw)};
}

[[nodiscard]] float length(cubey::math::Vec2 value) {
    return std::sqrt(value.x * value.x + value.y * value.y);
}

[[nodiscard]] float sample_height(const TerrainHeightSource& source, cubey::math::Vec2 position,
                                  float footprint_m, float vertical_scale) {
    return source.sample_height({.world_xz = position, .footprint_m = footprint_m}) *
           vertical_scale;
}

[[nodiscard]] std::uint32_t largest_circular_arc(const std::vector<bool>& sectors) {
    if (sectors.empty() || std::none_of(sectors.begin(), sectors.end(), [](bool value) {
            return value;
        })) {
        return 0U;
    }
    if (std::all_of(sectors.begin(), sectors.end(), [](bool value) { return value; })) {
        return static_cast<std::uint32_t>(sectors.size());
    }
    std::uint32_t best = 0U;
    std::uint32_t run = 0U;
    for (std::size_t index = 0U; index < sectors.size() * 2U; ++index) {
        if (sectors[index % sectors.size()]) {
            run = std::min(run + 1U, static_cast<std::uint32_t>(sectors.size()));
            best = std::max(best, run);
        } else {
            run = 0U;
        }
    }
    return best;
}

[[nodiscard]] float quantile(std::vector<float> values, float q) {
    std::sort(values.begin(), values.end());
    const float position = q * static_cast<float>(values.size() - 1U);
    const std::size_t lower = static_cast<std::size_t>(std::floor(position));
    const std::size_t upper = static_cast<std::size_t>(std::ceil(position));
    return std::lerp(values[lower], values[upper], position - static_cast<float>(lower));
}

[[nodiscard]] TerrainDirectionalPlacementPlan evaluate(
    const TerrainHeightSource& source, const TerrainDirectionalPlacementRequest& request,
    cubey::math::Vec2 focus, bool detailed_local) {
    TerrainDirectionalPlacementPlan result;
    result.source_focus_xz = focus;
    result.local_radius_m = request.local_radius_m;
    result.maximum_local_relief_m = request.maximum_local_relief_m;
    result.maximum_local_p95_slope = request.maximum_local_p95_slope;
    result.sector_count = request.sector_count;
    result.sectors.resize(request.sector_count);
    result.center_height_m = sample_height(source, focus, 32.0F, request.vertical_scale);

    float local_minimum = result.center_height_m;
    float local_maximum = result.center_height_m;
    std::vector<float> local_slopes;
    const int local_half_steps = detailed_local ? 4 : 1;
    const float local_step = request.local_radius_m / static_cast<float>(local_half_steps);
    for (int z = -local_half_steps; z <= local_half_steps; ++z) {
        for (int x = -local_half_steps; x <= local_half_steps; ++x) {
            const cubey::math::Vec2 offset{static_cast<float>(x) * local_step,
                                           static_cast<float>(z) * local_step};
            if (length(offset) > request.local_radius_m) {
                continue;
            }
            const TerrainSample sample = source.sample(
                {.world_xz = focus + offset, .footprint_m = detailed_local ? 32.0F : 96.0F});
            const float height = sample.height_m * request.vertical_scale;
            local_minimum = std::min(local_minimum, height);
            local_maximum = std::max(local_maximum, height);
            local_slopes.push_back(length(sample.gradient_xz) * request.vertical_scale);
        }
    }
    result.local_relief_m = local_maximum - local_minimum;
    result.local_p95_slope = quantile(local_slopes, 0.95F);

    std::vector<bool> mountain_sectors(request.sector_count, false);
    std::vector<bool> open_sectors(request.sector_count, false);
    float prominence_sum = 0.0F;
    float direction_x = 0.0F;
    float direction_z = 0.0F;
    for (std::uint32_t sector = 0U; sector < request.sector_count; ++sector) {
        const float yaw = static_cast<float>(sector) * kTwoPi /
                          static_cast<float>(request.sector_count);
        const cubey::math::Vec2 direction = direction_from_yaw(yaw);
        const float near = sample_height(source, focus + direction * request.near_distance_m,
                                         96.0F, request.vertical_scale);
        const float middle = sample_height(source, focus + direction * request.middle_distance_m,
                                           128.0F, request.vertical_scale);
        const float far = sample_height(source, focus + direction * request.far_distance_m, 160.0F,
                                        request.vertical_scale);
        const float remote = sample_height(source, focus + direction * request.remote_distance_m,
                                           192.0F, request.vertical_scale);
        const float peak = std::max(far, remote);
        const float prominence = peak - near;
        const bool mountain = prominence >= request.mountain_prominence_m;
        const bool open = std::max(middle, peak) - near <= request.open_prominence_m;
        const bool gradual = mountain && middle >= near - 150.0F && peak >= middle - 150.0F;
        result.sectors[sector] = {
            .yaw_radians = yaw,
            .near_height_m = near,
            .middle_height_m = middle,
            .far_height_m = far,
            .remote_height_m = remote,
            .prominence_m = prominence,
            .mountain = mountain,
            .open = open,
            .gradual_rise = gradual,
        };
        mountain_sectors[sector] = mountain;
        open_sectors[sector] = open;
        if (mountain) {
            ++result.mountain_sector_count;
            prominence_sum += prominence;
            const float weight = std::max(prominence, 0.0F);
            direction_x += direction.x * weight;
            direction_z += direction.y * weight;
            if (gradual) {
                ++result.gradual_rise_sector_count;
            }
        }
        result.open_sector_count += open ? 1U : 0U;
    }
    result.largest_mountain_arc_sectors = largest_circular_arc(mountain_sectors);
    result.largest_open_arc_sectors = largest_circular_arc(open_sectors);
    result.mean_mountain_prominence_m =
        result.mountain_sector_count > 0U
            ? prominence_sum / static_cast<float>(result.mountain_sector_count)
            : 0.0F;
    if (direction_x != 0.0F || direction_z != 0.0F) {
        result.mountain_yaw_radians = std::atan2(direction_x, -direction_z);
    }

    const bool local_contract = result.local_relief_m <= request.maximum_local_relief_m &&
                                result.local_p95_slope <= request.maximum_local_p95_slope;
    const bool coverage_contract =
        result.mountain_sector_count >= request.minimum_mountain_sectors &&
        result.mountain_sector_count <= request.maximum_mountain_sectors &&
        result.largest_mountain_arc_sectors >= request.minimum_mountain_arc_sectors &&
        result.largest_open_arc_sectors >= request.minimum_open_arc_sectors;
    const bool rise_contract = result.gradual_rise_sector_count * 2U >=
                               std::max(result.mountain_sector_count, 1U);
    result.contract_satisfied = local_contract && coverage_contract && rise_contract;

    const float local_score =
        2.0F * (1.0F - std::clamp(result.local_relief_m / request.maximum_local_relief_m, 0.0F,
                                  2.0F)) +
        2.0F * (1.0F - std::clamp(result.local_p95_slope /
                                      request.maximum_local_p95_slope,
                                  0.0F, 2.0F));
    const float desired_coverage = 8.0F;
    const float coverage_score =
        1.5F * (1.0F - std::clamp(std::abs(static_cast<float>(result.mountain_sector_count) -
                                           desired_coverage) /
                                      desired_coverage,
                                  0.0F, 1.0F));
    const float arc_score =
        static_cast<float>(result.largest_mountain_arc_sectors +
                           result.largest_open_arc_sectors) /
        static_cast<float>(request.sector_count);
    const float rise_score =
        result.mountain_sector_count > 0U
            ? static_cast<float>(result.gradual_rise_sector_count) /
                  static_cast<float>(result.mountain_sector_count)
            : 0.0F;
    const float prominence_score =
        std::clamp(result.mean_mountain_prominence_m / 1'500.0F, 0.0F, 2.0F);
    result.score = local_score + coverage_score + arc_score + rise_score + prominence_score;
    return result;
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

[[nodiscard]] std::vector<Candidate> score_candidates(
    const TerrainHeightSource& source, const TerrainDirectionalPlacementRequest& request,
    const std::vector<cubey::math::Vec2>& focuses) {
    std::vector<Candidate> result;
    result.reserve(focuses.size());
    for (const cubey::math::Vec2 focus : focuses) {
        const TerrainDirectionalPlacementPlan plan =
            evaluate(source, request, focus, request.detailed_search);
        result.push_back({
            .focus = focus,
            .contract_satisfied = request.detailed_search && plan.contract_satisfied,
            .score = plan.score,
        });
    }
    std::sort(result.begin(), result.end(), [](const Candidate& lhs, const Candidate& rhs) {
        if (lhs.contract_satisfied != rhs.contract_satisfied) {
            return lhs.contract_satisfied;
        }
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

[[nodiscard]] bool plan_is_better(const TerrainDirectionalPlacementPlan& lhs,
                                  const TerrainDirectionalPlacementPlan& rhs) {
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

} // namespace

void validate_terrain_directional_placement_request(
    const TerrainDirectionalPlacementRequest& request) {
    const bool finite = std::isfinite(request.search_extent_m) &&
                        std::isfinite(request.search_step_m) &&
                        std::isfinite(request.local_radius_m) &&
                        std::isfinite(request.near_distance_m) &&
                        std::isfinite(request.middle_distance_m) &&
                        std::isfinite(request.far_distance_m) &&
                        std::isfinite(request.remote_distance_m) &&
                        std::isfinite(request.mountain_prominence_m) &&
                        std::isfinite(request.open_prominence_m) &&
                        std::isfinite(request.maximum_local_relief_m) &&
                        std::isfinite(request.maximum_local_p95_slope) &&
                        std::isfinite(request.vertical_scale);
    if (!finite || request.search_extent_m < 0.0F || request.search_step_m <= 0.0F ||
        request.local_radius_m <= 0.0F || request.near_distance_m <= 0.0F ||
        request.middle_distance_m <= request.near_distance_m ||
        request.far_distance_m <= request.middle_distance_m ||
        request.remote_distance_m <= request.far_distance_m ||
        request.mountain_prominence_m <= request.open_prominence_m ||
        request.open_prominence_m < 0.0F || request.maximum_local_relief_m <= 0.0F ||
        request.maximum_local_p95_slope <= 0.0F || request.sector_count < 8U ||
        request.minimum_mountain_sectors == 0U ||
        request.minimum_mountain_sectors > request.maximum_mountain_sectors ||
        request.maximum_mountain_sectors >= request.sector_count ||
        request.minimum_mountain_arc_sectors > request.sector_count ||
        request.minimum_open_arc_sectors > request.sector_count || request.vertical_scale <= 0.0F) {
        throw std::runtime_error("invalid terrain directional placement request");
    }
}

TerrainDirectionalPlacementPlan evaluate_terrain_directional_placement(
    const TerrainHeightSource& source, const TerrainDirectionalPlacementRequest& request,
    cubey::math::Vec2 focus) {
    validate_terrain_directional_placement_request(request);
    return evaluate(source, request, focus, true);
}

TerrainDirectionalPlacementPlan plan_terrain_directional_placement(
    const TerrainHeightSource& source, const TerrainDirectionalPlacementRequest& request) {
    validate_terrain_directional_placement_request(request);
    std::vector<cubey::math::Vec2> coarse_focuses;
    for (float z = -request.search_extent_m; z <= request.search_extent_m;
         z += request.search_step_m) {
        for (float x = -request.search_extent_m; x <= request.search_extent_m;
             x += request.search_step_m) {
            coarse_focuses.push_back({x, z});
        }
    }
    const std::vector<Candidate> coarse = score_candidates(source, request, coarse_focuses);

    std::vector<cubey::math::Vec2> medium_focuses;
    for (std::size_t index = 0U; index < std::min(kCoarseShortlistCount, coarse.size()); ++index) {
        for (float z : {-kMediumRefinementOffsetM, 0.0F, kMediumRefinementOffsetM}) {
            for (float x : {-kMediumRefinementOffsetM, 0.0F, kMediumRefinementOffsetM}) {
                append_unique(medium_focuses, coarse[index].focus + cubey::math::Vec2{x, z});
            }
        }
    }
    const std::vector<Candidate> medium = score_candidates(source, request, medium_focuses);

    std::vector<cubey::math::Vec2> fine_focuses;
    for (std::size_t index = 0U; index < std::min(kFineShortlistCount, medium.size()); ++index) {
        for (float z : {-kFineRefinementOffsetM, -kFineRefinementOffsetM * 0.5F, 0.0F,
                        kFineRefinementOffsetM * 0.5F, kFineRefinementOffsetM}) {
            for (float x : {-kFineRefinementOffsetM, -kFineRefinementOffsetM * 0.5F, 0.0F,
                            kFineRefinementOffsetM * 0.5F, kFineRefinementOffsetM}) {
                append_unique(fine_focuses, medium[index].focus + cubey::math::Vec2{x, z});
            }
        }
    }
    const std::vector<Candidate> fine = score_candidates(source, request, fine_focuses);
    if (fine.empty()) {
        throw std::runtime_error("terrain directional placement search produced no candidates");
    }

    std::vector<TerrainDirectionalPlacementPlan> plans;
    const std::size_t full_count = std::min(kFullCandidateCount, fine.size());
    plans.reserve(full_count);
    for (std::size_t index = 0U; index < full_count; ++index) {
        TerrainDirectionalPlacementPlan plan = evaluate(source, request, fine[index].focus, true);
        plan.coarse_candidate_count = static_cast<std::uint32_t>(coarse.size());
        plan.refined_candidate_count = static_cast<std::uint32_t>(medium.size() + fine.size());
        plan.full_candidate_count = static_cast<std::uint32_t>(full_count);
        plans.push_back(plan);
    }
    std::sort(plans.begin(), plans.end(), plan_is_better);
    return plans.front();
}

} // namespace cubey::projects::terrain
