#include "terrain_directional_backdrop_study.h"
#include "terrain_source_study.h"

#include "terrain_app.h"
#include "terrain_config.h"
#include "terrain_directional_relief.h"

#include <cubey/core/run_config.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using cubey::projects::terrain::TerrainDirectionalBackdropLane;

[[nodiscard]] float aspect_ratio(const cubey::RunConfig& config) {
    return static_cast<float>(config.width) / static_cast<float>(config.height);
}

[[nodiscard]] cubey::projects::terrain::TerrainBackdropStagePlan apply_orbit_overrides(
    cubey::projects::terrain::TerrainBackdropStagePlan plan,
    const cubey::projects::terrain::TerrainRuntimeConfig& runtime) {
    if (runtime.backdrop_orbit_radius_m.has_value()) {
        plan.orbit_default_radius_m = std::clamp(runtime.backdrop_orbit_radius_m.value(),
                                                 plan.orbit_min_radius_m,
                                                 plan.orbit_max_radius_m);
    }
    if (runtime.backdrop_elevation_radians.has_value()) {
        plan.orbit_default_elevation_radians =
            std::clamp(runtime.backdrop_elevation_radians.value(),
                       plan.orbit_min_elevation_radians, plan.orbit_max_elevation_radians);
    }
    return plan;
}

int run_study(cubey::RunConfig config, TerrainDirectionalBackdropLane lane,
              float expanded_focus_height_m,
              std::optional<float> expanded_orbit_radius_m) {
    using namespace cubey::projects::terrain;
    if (!config.terrain.render_path.empty() && config.terrain.render_path != "backdrop") {
        throw std::runtime_error("directional backdrop study supports only the backdrop path");
    }
    if (!config.terrain.backdrop_mesh_density.empty() &&
        config.terrain.backdrop_mesh_density != "high") {
        throw std::runtime_error("directional backdrop study requires high mesh density");
    }
    if (!config.terrain.weathering.empty() && config.terrain.weathering != "off") {
        throw std::runtime_error("directional backdrop study disables weathering");
    }

    const TerrainSourceStudyRecipe recipe =
        terrain_source_study_recipe_from_name(config.terrain.recipe);
    config.terrain.render_path = "backdrop";
    config.terrain.backdrop_mesh_density = "high";
    config.terrain.camera_preset =
        config.terrain.camera_preset.empty() ? "backdrop" : config.terrain.camera_preset;
    config.terrain.preset = "mountain";
    config.terrain.source_version = "v2.1";
    config.terrain.weathering = "off";
    config.terrain.presentation =
        config.terrain.presentation.empty() ? "backdrop" : config.terrain.presentation;
    config.terrain.backdrop_minimum_visible_distance_m = 6'000.0F;

    const TerrainRuntimeConfig runtime = terrain_runtime_config_from_run_config(config);
    const TerrainSourceStudySource base_source(recipe, runtime.source.seed);
    TerrainBackdropStageRequest current_request = terrain_backdrop_stage_request(
        TerrainBackdropStageMode::Detached, aspect_ratio(config), runtime.vertical_scale);
    current_request.minimum_visible_terrain_distance_m = 6'000.0F;
    if (runtime.backdrop_orbit_radius_m.has_value()) {
        current_request.orbit_default_radius_m = runtime.backdrop_orbit_radius_m.value();
    }
    if (runtime.backdrop_elevation_radians.has_value()) {
        current_request.orbit_default_elevation_radians =
            runtime.backdrop_elevation_radians.value();
    }
    const TerrainBackdropStagePlan current_stage =
        plan_terrain_backdrop_stage(base_source, current_request);
    const TerrainDirectionalPlacementPlan placement =
        plan_terrain_directional_placement(base_source, directional_backdrop_placement_request());
    const bool expanded = lane == TerrainDirectionalBackdropLane::ExpandedShaped;

    TerrainAppOptions options{
        .backdrop_source = &base_source,
        .backdrop_render_stride = 1U,
        .backdrop_center_mode = lane == TerrainDirectionalBackdropLane::HardCut
                                    ? TerrainBackdropCenterMode::Cutout
                                    : TerrainBackdropCenterMode::Continuous,
    };
    if (expanded) {
        options.backdrop_outer_radius_m = expanded_directional_backdrop_outer_radius_m();
    }
    std::unique_ptr<TerrainDirectionalReliefSource> shaped_source;
    if (lane == TerrainDirectionalBackdropLane::HardCut) {
        options.backdrop_stage_plan = current_stage;
    } else if (lane == TerrainDirectionalBackdropLane::ContinuousCurrent) {
        const TerrainDirectionalPlacementPlan current_placement =
            evaluate_terrain_directional_placement(base_source,
                                                   directional_backdrop_placement_request(),
                                                   current_stage.source_focus_xz);
        options.backdrop_stage_plan = apply_orbit_overrides(
            make_directional_backdrop_stage_plan(base_source, current_placement,
                                                 runtime.vertical_scale),
            runtime);
    } else if (lane == TerrainDirectionalBackdropLane::Placement) {
        options.backdrop_stage_plan = apply_orbit_overrides(
            make_directional_backdrop_stage_plan(base_source, placement,
                                                 runtime.vertical_scale),
            runtime);
    } else {
        const TerrainDirectionalReliefParameters relief_parameters =
            expanded ? expanded_directional_backdrop_relief_parameters(placement)
                     : TerrainDirectionalReliefParameters{
                           .focus_xz = placement.source_focus_xz,
                           .mountain_yaw_radians = placement.mountain_yaw_radians,
                       };
        shaped_source = std::make_unique<TerrainDirectionalReliefSource>(
            base_source, relief_parameters);
        options.backdrop_source = shaped_source.get();
        TerrainDirectionalBackdropStageParameters stage_parameters;
        if (expanded) {
            stage_parameters = {
                .focus_height_m = expanded_focus_height_m,
                .orbit_min_radius_m = 100.0F,
                .orbit_default_radius_m = expanded_orbit_radius_m.value_or(400.0F),
                .orbit_max_radius_m = 1'000.0F,
            };
        }
        options.backdrop_stage_plan = apply_orbit_overrides(
            make_directional_backdrop_stage_plan(*shaped_source, placement,
                                                 runtime.vertical_scale, stage_parameters),
            runtime);
    }
    return run_terrain_with_options(config, options);
}

} // namespace

int main(int argc, char** argv) {
    TerrainDirectionalBackdropLane lane = TerrainDirectionalBackdropLane::Placement;
    float expanded_focus_height_m = 500.0F;
    std::optional<float> expanded_orbit_radius_m;
    std::vector<char*> forwarded;
    forwarded.reserve(static_cast<std::size_t>(argc));
    forwarded.push_back(argv[0]);
    for (int index = 1; index < argc; ++index) {
        const std::string_view option = argv[index];
        if (option == "--directional-lane") {
            if (index + 1 >= argc) {
                std::fprintf(stderr, "terrain_directional_backdrop_study: missing lane name\n");
                return 1;
            }
            lane = cubey::projects::terrain::terrain_directional_backdrop_lane_from_name(
                argv[++index]);
        } else if (option == "--directional-focus-height") {
            if (index + 1 >= argc) {
                std::fprintf(stderr,
                             "terrain_directional_backdrop_study: missing focus height\n");
                return 1;
            }
            expanded_focus_height_m = std::stof(argv[++index]);
            if (!std::isfinite(expanded_focus_height_m) || expanded_focus_height_m < 100.0F ||
                expanded_focus_height_m > 1'000.0F) {
                std::fprintf(stderr,
                             "terrain_directional_backdrop_study: focus height must be 100..1000 m\n");
                return 1;
            }
        } else if (option == "--directional-orbit-radius") {
            if (index + 1 >= argc) {
                std::fprintf(stderr,
                             "terrain_directional_backdrop_study: missing orbit radius\n");
                return 1;
            }
            expanded_orbit_radius_m = std::stof(argv[++index]);
            if (!std::isfinite(expanded_orbit_radius_m.value()) ||
                expanded_orbit_radius_m.value() < 100.0F ||
                expanded_orbit_radius_m.value() > 1'000.0F) {
                std::fprintf(stderr,
                             "terrain_directional_backdrop_study: orbit radius must be 100..1000 m\n");
                return 1;
            }
        } else {
            forwarded.push_back(argv[index]);
        }
    }
    return cubey::run_cli_app(
        static_cast<int>(forwarded.size()), forwarded.data(),
        {
            .app_name = "terrain_directional_backdrop_study",
            .default_title = "cubey terrain directional backdrop study",
        },
        [lane, expanded_focus_height_m,
         expanded_orbit_radius_m](cubey::RunConfig config) {
            return run_study(std::move(config), lane, expanded_focus_height_m,
                             expanded_orbit_radius_m);
        });
}
