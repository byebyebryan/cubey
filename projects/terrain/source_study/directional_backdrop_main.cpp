#include "terrain_directional_backdrop_study.h"
#include "terrain_source_study.h"

#include "terrain_app.h"
#include "terrain_config.h"
#include "terrain_directional_relief.h"

#include <cubey/core/run_config.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using cubey::projects::terrain::TerrainDirectionalBackdropLane;

[[nodiscard]] float aspect_ratio(const cubey::RunConfig& config) {
    return static_cast<float>(config.width) / static_cast<float>(config.height);
}

int run_study(cubey::RunConfig config, TerrainDirectionalBackdropLane lane) {
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

    TerrainAppOptions options{
        .backdrop_source = &base_source,
        .backdrop_render_stride = 1U,
        .backdrop_center_mode = lane == TerrainDirectionalBackdropLane::HardCut
                                    ? TerrainBackdropCenterMode::Cutout
                                    : TerrainBackdropCenterMode::Continuous,
    };
    std::unique_ptr<TerrainDirectionalReliefSource> shaped_source;
    if (lane == TerrainDirectionalBackdropLane::HardCut) {
        options.backdrop_stage_plan = current_stage;
    } else if (lane == TerrainDirectionalBackdropLane::ContinuousCurrent) {
        const TerrainDirectionalPlacementPlan current_placement =
            evaluate_terrain_directional_placement(base_source,
                                                   directional_backdrop_placement_request(),
                                                   current_stage.source_focus_xz);
        options.backdrop_stage_plan =
            make_directional_backdrop_stage_plan(base_source, current_placement,
                                                 runtime.vertical_scale);
    } else if (lane == TerrainDirectionalBackdropLane::Placement) {
        options.backdrop_stage_plan =
            make_directional_backdrop_stage_plan(base_source, placement, runtime.vertical_scale);
    } else {
        shaped_source = std::make_unique<TerrainDirectionalReliefSource>(
            base_source, TerrainDirectionalReliefParameters{
                             .focus_xz = placement.source_focus_xz,
                             .mountain_yaw_radians = placement.mountain_yaw_radians,
                         });
        options.backdrop_source = shaped_source.get();
        options.backdrop_stage_plan =
            make_directional_backdrop_stage_plan(*shaped_source, placement,
                                                 runtime.vertical_scale);
    }
    return run_terrain_with_options(config, options);
}

} // namespace

int main(int argc, char** argv) {
    TerrainDirectionalBackdropLane lane = TerrainDirectionalBackdropLane::Placement;
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
        [lane](cubey::RunConfig config) { return run_study(std::move(config), lane); });
}
