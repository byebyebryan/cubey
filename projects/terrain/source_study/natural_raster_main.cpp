#include "terrain_raster_height_source.h"

#include "terrain_app.h"
#include "terrain_config.h"
#include "terrain_natural_backdrop_stage.h"

#include <cubey/core/run_config.h>

#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

int run_natural_raster_study(
    cubey::RunConfig config,
    cubey::projects::terrain::TerrainBackdropCenterSampling center_sampling, float focus_height_m) {
    using namespace cubey::projects::terrain;
    if (config.terrain.study_field_path.empty()) {
        throw std::runtime_error("terrain natural raster study requires --terrain-study-field");
    }
    if (!config.terrain.render_path.empty() && config.terrain.render_path != "backdrop") {
        throw std::runtime_error("terrain natural raster study supports only the backdrop path");
    }
    if (!config.terrain.backdrop_mesh_density.empty() &&
        config.terrain.backdrop_mesh_density != "high") {
        throw std::runtime_error("terrain natural raster study requires high mesh density");
    }
    if (!config.terrain.weathering.empty() && config.terrain.weathering != "off") {
        throw std::runtime_error("terrain natural raster study disables weathering");
    }

    TerrainBackdropCenterMode center_mode = TerrainBackdropCenterMode::Continuous;
    if (config.terrain.backdrop_center == "consumer-owned") {
        center_mode = TerrainBackdropCenterMode::Cutout;
    } else if (!config.terrain.backdrop_center.empty() &&
               config.terrain.backdrop_center != "continuous") {
        throw std::runtime_error("unknown terrain backdrop center ownership: " +
                                 config.terrain.backdrop_center);
    }
    const std::filesystem::path field_path = config.terrain.study_field_path;
    config.terrain.study_field_path.clear();
    config.terrain.backdrop_center.clear();
    TerrainRasterHeightSource source(field_path);
    if (config.terrain.seed_set && config.terrain.seed != source.metadata().seed) {
        throw std::runtime_error("terrain seed does not match raster field manifest");
    }
    config.terrain.seed = source.metadata().seed;
    config.terrain.seed_set = true;
    config.terrain.render_path = "backdrop";
    config.terrain.backdrop_profile = "hard-cut-v1";
    config.terrain.backdrop_mesh_density = "high";
    config.terrain.camera_preset =
        config.terrain.camera_preset.empty() ? "backdrop" : config.terrain.camera_preset;
    config.terrain.preset = "mountain";
    config.terrain.source_version = "v2.1";
    config.terrain.weathering = "off";
    config.terrain.presentation =
        config.terrain.presentation.empty() ? "backdrop" : config.terrain.presentation;

    const TerrainRuntimeConfig runtime = terrain_runtime_config_from_run_config(config);
    TerrainNaturalBackdropStageRequest request;
    request.stage.focus_height_m = focus_height_m;
    request.vertical_scale = runtime.vertical_scale;
    request.outer_radius_m = 16'384.0F;
    if (runtime.backdrop_orbit_radius_m.has_value()) {
        request.stage.orbit_default_radius_m = runtime.backdrop_orbit_radius_m.value();
    }
    if (runtime.backdrop_elevation_radians.has_value()) {
        request.stage.orbit_default_elevation_radians =
            runtime.backdrop_elevation_radians.value();
    }
    const float centered_support =
        terrain_natural_backdrop_centered_support_radius(request,
                                                         source.metadata().gradient_step_m);
    if (!source.contains_disk({0.0F, 0.0F}, centered_support)) {
        throw std::runtime_error("terrain raster field does not cover the natural-stage search");
    }
    const TerrainNaturalBackdropStagePlan natural =
        plan_terrain_natural_backdrop_stage(source, request);
    if (!natural.placement.contract_satisfied || !natural.stage.contract_satisfied) {
        throw std::runtime_error("terrain raster field has no passing natural backdrop stage");
    }
    if (!source.contains_disk(natural.stage.source_focus_xz,
                              natural.selected_support_radius_m)) {
        throw std::runtime_error("terrain raster field does not cover the selected backdrop");
    }
    return run_terrain_with_options(config,
                                    {
                                        .backdrop_source = &source,
                                        .backdrop_render_stride = 1U,
                                        .backdrop_center_mode = center_mode,
                                        .backdrop_center_sampling = center_sampling,
                                        .backdrop_stage_plan = natural.stage,
                                        .backdrop_outer_radius_m = request.outer_radius_m,
                                    });
}

} // namespace

int main(int argc, char** argv) {
    using cubey::projects::terrain::TerrainBackdropCenterSampling;
    TerrainBackdropCenterSampling center_sampling = TerrainBackdropCenterSampling::Uniform;
    float focus_height_m = 500.0F;
    std::vector<char*> forwarded;
    forwarded.reserve(static_cast<std::size_t>(argc));
    forwarded.push_back(argv[0]);
    try {
        for (int index = 1; index < argc; ++index) {
            const std::string_view option = argv[index];
            if (option == "--natural-center-sampling") {
                if (index + 1 >= argc) {
                    throw std::runtime_error("missing natural center sampling name");
                }
                const std::string_view name = argv[++index];
                if (name == "split-log") {
                    center_sampling = TerrainBackdropCenterSampling::SplitLinearLog;
                } else if (name == "uniform") {
                    center_sampling = TerrainBackdropCenterSampling::Uniform;
                } else {
                    throw std::runtime_error("unknown natural center sampling: " +
                                             std::string(name));
                }
            } else if (option == "--natural-focus-height") {
                if (index + 1 >= argc) {
                    throw std::runtime_error("missing natural focus height");
                }
                focus_height_m = std::stof(argv[++index]);
                if (!std::isfinite(focus_height_m) || focus_height_m < 100.0F ||
                    focus_height_m > 1'000.0F) {
                    throw std::runtime_error("natural focus height must be 100..1000 m");
                }
            } else {
                forwarded.push_back(argv[index]);
            }
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "terrain_natural_raster_study: %s\n", error.what());
        return 1;
    }
    return cubey::run_cli_app(
        static_cast<int>(forwarded.size()), forwarded.data(),
        {
            .app_name = "terrain_natural_raster_study",
            .default_title = "cubey terrain natural raster study",
        },
        [center_sampling, focus_height_m](cubey::RunConfig config) {
            return run_natural_raster_study(std::move(config), center_sampling, focus_height_m);
        });
}
