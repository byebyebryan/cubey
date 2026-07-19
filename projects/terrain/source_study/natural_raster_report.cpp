#include "terrain_raster_height_source.h"

#include "terrain_backdrop_stage.h"
#include "terrain_natural_backdrop_stage.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

struct Options {
    std::filesystem::path field{};
    float focus_height_m = 500.0F;
};

[[nodiscard]] Options parse_options(int argc, char** argv) {
    Options result;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option = argv[index];
        if (option == "--terrain-study-field" && index + 1 < argc) {
            result.field = argv[++index];
        } else if (option == "--natural-focus-height" && index + 1 < argc) {
            result.focus_height_m = std::stof(argv[++index]);
            if (!std::isfinite(result.focus_height_m) || result.focus_height_m < 100.0F ||
                result.focus_height_m > 1'000.0F) {
                throw std::runtime_error("natural focus height must be 100..1000 m");
            }
        } else {
            throw std::runtime_error(
                "usage: terrain_natural_raster_report --terrain-study-field path "
                "[--natural-focus-height meters]");
        }
    }
    if (result.field.empty()) {
        throw std::runtime_error("terrain natural raster report requires a field path");
    }
    return result;
}

[[nodiscard]] nlohmann::json stage_json(
    const cubey::projects::terrain::TerrainBackdropStagePlan& stage) {
    return {
        {"source_focus_xz_m", {stage.source_focus_xz.x, stage.source_focus_xz.y}},
        {"source_center_height_m", stage.source_center_height_m},
        {"target_height_m", stage.target_height_m},
        {"focus_height_m", stage.target_height_m - stage.source_center_height_m},
        {"minimum_camera_clearance_m", stage.minimum_camera_clearance_m},
        {"showcase_yaw_radians", stage.showcase_yaw_radians},
        {"local_relief_m", stage.local_relief_m},
        {"local_p95_slope", stage.local_p95_slope},
        {"relief_sector_count", stage.relief_sector_count},
        {"open_sector_count", stage.lower_frame_clear_sector_count},
        {"minimum_lower_frame_terrain_distance_m",
         stage.minimum_lower_frame_terrain_distance_m},
        {"orbit_radius_m",
         {stage.orbit_min_radius_m, stage.orbit_default_radius_m, stage.orbit_max_radius_m}},
        {"orbit_elevation_radians",
         {stage.orbit_min_elevation_radians, stage.orbit_default_elevation_radians,
          stage.orbit_max_elevation_radians}},
        {"candidate_counts",
         {stage.coarse_candidate_count, stage.refined_candidate_count,
          stage.full_candidate_count}},
        {"contract_satisfied", stage.contract_satisfied},
        {"score", stage.score},
    };
}

[[nodiscard]] nlohmann::json placement_json(
    const cubey::projects::terrain::TerrainDirectionalPlacementPlan& placement) {
    nlohmann::json sectors = nlohmann::json::array();
    for (const auto& sector : placement.sectors) {
        sectors.push_back({
            {"yaw_radians", sector.yaw_radians},
            {"prominence_m", sector.prominence_m},
            {"mountain", sector.mountain},
            {"open", sector.open},
            {"gradual_rise", sector.gradual_rise},
        });
    }
    return {
        {"source_focus_xz_m", {placement.source_focus_xz.x, placement.source_focus_xz.y}},
        {"mountain_yaw_radians", placement.mountain_yaw_radians},
        {"center_height_m", placement.center_height_m},
        {"local_relief_m", placement.local_relief_m},
        {"local_p95_slope", placement.local_p95_slope},
        {"mean_mountain_prominence_m", placement.mean_mountain_prominence_m},
        {"mountain_sector_count", placement.mountain_sector_count},
        {"open_sector_count", placement.open_sector_count},
        {"gradual_rise_sector_count", placement.gradual_rise_sector_count},
        {"largest_mountain_arc_sectors", placement.largest_mountain_arc_sectors},
        {"largest_open_arc_sectors", placement.largest_open_arc_sectors},
        {"candidate_counts",
         {placement.coarse_candidate_count, placement.refined_candidate_count,
          placement.full_candidate_count}},
        {"contract_satisfied", placement.contract_satisfied},
        {"score", placement.score},
        {"sectors", std::move(sectors)},
    };
}

} // namespace

int main(int argc, char** argv) {
    using namespace cubey::projects::terrain;
    try {
        const Options options = parse_options(argc, argv);
        const TerrainRasterHeightSource source(options.field);

        TerrainBackdropStageRequest strict_request =
            terrain_backdrop_stage_request(TerrainBackdropStageMode::Detached);
        strict_request.search_extent_m = 12'000.0F;
        strict_request.search_step_m = 4'000.0F;
        const auto strict_start = std::chrono::steady_clock::now();
        const TerrainBackdropStagePlan strict =
            plan_terrain_backdrop_stage(source, strict_request);
        const auto strict_end = std::chrono::steady_clock::now();

        TerrainNaturalBackdropStageRequest natural_request;
        natural_request.stage.focus_height_m = options.focus_height_m;
        const float centered_support = terrain_natural_backdrop_centered_support_radius(
            natural_request, source.metadata().gradient_step_m);
        const bool centered_coverage = source.contains_disk({0.0F, 0.0F}, centered_support);
        if (!centered_coverage) {
            throw std::runtime_error("terrain raster field does not cover the natural-stage search");
        }
        const auto natural_start = std::chrono::steady_clock::now();
        const TerrainNaturalBackdropStagePlan natural =
            plan_terrain_natural_backdrop_stage(source, natural_request);
        const auto natural_end = std::chrono::steady_clock::now();

        const auto milliseconds = [](auto begin, auto end) {
            return std::chrono::duration<double, std::milli>(end - begin).count();
        };
        const nlohmann::json report{
            {"schema", "cubey.terrain.natural-raster-stage.v1"},
            {"source",
             {
                 {"id", source.metadata().id},
                 {"seed", source.metadata().seed},
                 {"width", source.width()},
                 {"height", source.height()},
                 {"sample_spacing_m", source.sample_spacing_m()},
             }},
            {"support",
             {
                 {"outer_radius_m", natural_request.outer_radius_m},
                 {"centered_search_radius_m", centered_support},
                 {"selected_radius_m", natural.selected_support_radius_m},
                 {"centered_search_covered", centered_coverage},
                 {"strict_selection_covered",
                  source.contains_disk(strict.source_focus_xz,
                                       natural.selected_support_radius_m)},
                 {"natural_selection_covered",
                  source.contains_disk(natural.stage.source_focus_xz,
                                       natural.selected_support_radius_m)},
             }},
            {"strict",
             {
                 {"setup_ms", milliseconds(strict_start, strict_end)},
                 {"stage", stage_json(strict)},
             }},
            {"natural",
             {
                 {"setup_ms", milliseconds(natural_start, natural_end)},
                 {"requested_focus_height_m", natural_request.stage.focus_height_m},
                 {"placement", placement_json(natural.placement)},
                 {"stage", stage_json(natural.stage)},
             }},
        };
        std::cout << report.dump(2) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain_natural_raster_report: " << error.what() << '\n';
        return 1;
    }
}
