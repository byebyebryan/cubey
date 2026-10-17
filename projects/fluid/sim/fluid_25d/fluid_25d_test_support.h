#pragma once

#include "fluid_25d_test_cases.h"

#include "../../fluid_25d/fluid_25d_project_config.h"
#include "fluid_25d_bank_comparison.h"
#include "fluid_25d_commands.h"
#include "fluid_25d_depth_palette.h"
#include "fluid_25d_diagnostics.h"
#include "fluid_25d_dye_palette.h"
#include "fluid_25d_finite_volume_oracle.h"
#include "fluid_25d_forcing_cubes.h"
#include "fluid_25d_hillside_supply.h"
#include "fluid_25d_mass_audit.h"
#include "fluid_25d_motion_markers.h"
#include "fluid_25d_natural_flow_recipe.h"
#include "fluid_25d_oracle.h"
#include "fluid_25d_presentation.h"
#include "fluid_25d_rain_study.h"
#include "fluid_25d_rain_visuals.h"
#include "fluid_25d_scenic_environment.h"
#include "fluid_25d_scenic_material.h"
#include "fluid_25d_whitewater.h"

#include <cubey/asset/file_digest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace cubey::projects::fluid::fluid_25d::test_detail {

// These tolerances cover float field updates and double-precision volume
// accumulation on the small reference grids; they are expressed in the
// contract's physical units.
constexpr double kDepthToleranceM = 0.000001;
constexpr double kLedgerToleranceM3 = 0.00001;
constexpr double kVolumeToleranceM3 = 0.0005;

[[nodiscard]] inline std::uint64_t headwaters_field_hash(std::span<const float> values) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const float value : values) {
        hash ^= std::bit_cast<std::uint32_t>(value);
        hash *= 1099511628211ULL;
    }
    return hash;
}

[[nodiscard]] inline std::uint64_t headwaters_mask_hash(std::span<const std::uint32_t> values) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const std::uint32_t value : values) {
        hash ^= value;
        hash *= 1099511628211ULL;
    }
    return hash;
}

inline void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

inline void require_close(double actual, double expected, double tolerance, const char* message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

template <typename Callable> void require_throws(Callable&& callable, const char* message) {
    bool threw = false;
    try {
        callable();
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw, message);
}

struct TerrainFixture {
    explicit TerrainFixture(float spacing_m = 3.5F, std::uint32_t grid_width = 5U,
                            std::uint32_t grid_height = 4U)
        : width(grid_width), height(grid_height), sample_spacing_m(spacing_m) {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() /
               ("cubey-fluid-25d-terrain-" + std::to_string(suffix));
        std::filesystem::create_directories(root);
        elevation.resize(static_cast<std::size_t>(width) * height);
        for (std::uint32_t z = 0U; z < height; ++z) {
            for (std::uint32_t x = 0U; x < width; ++x) {
                elevation[static_cast<std::size_t>(z) * width + x] =
                    static_cast<float>(z * width + x);
            }
        }
        const std::string hash = cubey::asset::sha256_hex(std::as_bytes(std::span{elevation}));
        manifest = {
            {"schema", "cubey.terrain.heightfield.v1"},
            {"source", {{"id", "fluid-25d-test-terrain"}}},
            {"seed", 731U},
            {"grid",
             {{"width", width},
              {"height", height},
              {"sample_spacing_m", sample_spacing_m},
              {"sample_origin_x_m", -10.0F},
              {"sample_origin_z_m", 25.0F}}},
            {"height", {{"offset_m", 10.0F}, {"scale", 1.5F}, {"relief_scale_m", 100.0F}}},
            {"files",
             {{"elevation",
               {{"path", "elevation.f32"},
                {"dtype", "float32-le"},
                {"layout", "row-major-zx"},
                {"shape", {height, width}},
                {"byte_count", elevation.size() * sizeof(float)},
                {"sha256", hash}}}}},
        };
        write();
    }

    ~TerrainFixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    void write() const {
        std::ofstream elevation_stream(root / "elevation.f32", std::ios::binary);
        elevation_stream.write(reinterpret_cast<const char*>(elevation.data()),
                               static_cast<std::streamsize>(elevation.size() * sizeof(float)));
        if (!elevation_stream) {
            throw std::runtime_error("failed to write fluid 2.5D terrain fixture payload");
        }
        std::ofstream manifest_stream(root / "heightfield.json");
        manifest_stream << manifest.dump(2) << '\n';
        if (!manifest_stream) {
            throw std::runtime_error("failed to write fluid 2.5D terrain fixture manifest");
        }
    }

    std::filesystem::path root{};
    std::uint32_t width = 5U;
    std::uint32_t height = 4U;
    float sample_spacing_m = 3.5F;
    std::vector<float> elevation{};
    nlohmann::json manifest{};
};

[[nodiscard]] inline nlohmann::json
make_synthetic_natural_flow_recipe_document(const TerrainFixture& fixture) {
    using namespace cubey::projects::fluid::fluid_25d;
    const cubey::asset::TerrainRasterHeightSource source(fixture.root);
    const Fluid25DScenarioData crop = make_fluid_25d_terrain_crop(5U, 5U, 30.0F, source, 1U, 1U);
    return {
        {"schema", kFluid25DNaturalFlowRecipeSchema},
        {"candidate_id", "synthetic-30m-fixture"},
        {"elevation_sha256", source.provenance().elevation_sha256},
        {"transformed_crop_sha256", crop.terrain_provenance->transformed_crop_sha256},
        {"crop_xzwh", {1U, 1U, 5U, 5U}},
        {"cell_size_m", 30.0},
        {"source_center_cell_xz", {2U, 2U}},
        {"source_cells_xz", {{2U, 2U}, {2U, 1U}, {2U, 3U}, {3U, 2U}, {1U, 2U}}},
        {"expected_outlet", {{"edge", "west"}, {"crop_cell_xz_bounds", {{0U, 2U}, {0U, 2U}}}}},
        {"gauges",
         {{{"name", "upstream"},
           {"cell_xz", {2U, 2U}},
           {"tangent_dx_dz", {-1, 0}},
           {"distance_m", 0.0},
           {"half_span_cells", 1U}}}},
    };
}

[[nodiscard]] inline cubey::projects::fluid::fluid_25d::Fluid25DConfig
natural_flow_test_config(std::uint32_t width = 5U, std::uint32_t height = 5U) {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config;
    config.grid_width = width;
    config.grid_height = height;
    config.cell_size_m = kFluid25DNaturalFlowCellSizeM;
    config.scenario = Fluid25DScenario::NaturalFlowStudy;
    config.solver = Fluid25DSolver::FiniteVolume;
    config.natural_flow_source_m3_per_s = 30.0F;
    validate_fluid_25d_config(config);
    return config;
}

[[nodiscard]] inline cubey::projects::fluid::fluid_25d::Fluid25DConfig
rain_study_test_config(std::uint32_t width = 8U, std::uint32_t height = 8U,
                       float rainfall_rate_mm_per_hour = 36000.0F) {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config;
    config.grid_width = width;
    config.grid_height = height;
    config.cell_size_m = kFluid25DHillsideRainStudyCellSizeM;
    config.scenario = Fluid25DScenario::HillsideRainStudy;
    config.solver = Fluid25DSolver::FiniteVolume;
    config.rainfall_depth_rate_m_per_s =
        fluid_25d_rainfall_depth_rate_m_per_s_from_mm_per_hour(rainfall_rate_mm_per_hour);
    validate_fluid_25d_config(config);
    return config;
}

[[nodiscard]] inline cubey::projects::fluid::fluid_25d::Fluid25DProjectConfig
parse_project(std::vector<std::string> arguments) {
    std::vector<char*> argv;
    argv.reserve(arguments.size());
    for (std::string& argument : arguments) {
        argv.push_back(argument.data());
    }
    return cubey::projects::fluid::fluid_25d::parse_fluid_25d_project_config(
        static_cast<int>(argv.size()), argv.data());
}

[[nodiscard]] inline cubey::projects::fluid::fluid_25d::Fluid25DConfig
test_config(std::uint32_t width, std::uint32_t height,
            cubey::projects::fluid::fluid_25d::Fluid25DScenario scenario) {
    cubey::projects::fluid::fluid_25d::Fluid25DConfig config;
    config.grid_width = width;
    config.grid_height = height;
    config.scenario = scenario;
    config.simulation_substeps = 2;
    cubey::projects::fluid::fluid_25d::validate_fluid_25d_config(config);
    return config;
}

[[nodiscard]] inline cubey::projects::fluid::fluid_25d::Fluid25DConfig
finite_volume_test_config(std::uint32_t width, std::uint32_t height,
                          cubey::projects::fluid::fluid_25d::Fluid25DScenario scenario) {
    cubey::projects::fluid::fluid_25d::Fluid25DConfig config;
    config.grid_width = width;
    config.grid_height = height;
    config.scenario = scenario;
    config.simulation_substeps = 2;
    config.solver = cubey::projects::fluid::fluid_25d::Fluid25DSolver::FiniteVolume;
    cubey::projects::fluid::fluid_25d::validate_fluid_25d_config(config);
    return config;
}

[[nodiscard]] inline cubey::projects::fluid::fluid_25d::Fluid25DScenarioData
make_dye_test_scenario(std::uint32_t width = 6U, std::uint32_t height = 3U) {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DScenarioData scenario =
        make_fluid_25d_scenario(Fluid25DScenario::SourceOutletDemo, width, height, 1.0F);
    std::fill(scenario.terrain_height_m.begin(), scenario.terrain_height_m.end(), 0.0F);
    std::fill(scenario.initial_water_depth_m.begin(), scenario.initial_water_depth_m.end(), 0.20F);
    std::fill(scenario.source_depth_rate_m_per_s.begin(), scenario.source_depth_rate_m_per_s.end(),
              0.0F);
    std::fill(scenario.sink_depth_rate_m_per_s.begin(), scenario.sink_depth_rate_m_per_s.end(),
              0.0F);
    scenario.source_cell = fluid_25d_scenario_index(width, height, 1U, height / 2U);
    scenario.sink_cell = fluid_25d_scenario_index(width, height, width - 2U, height / 2U);
    scenario.source_depth_rate_m_per_s[scenario.source_cell] = 0.20F;
    scenario.sink_depth_rate_m_per_s[scenario.sink_cell] = 0.20F;
    return scenario;
}

} // namespace cubey::projects::fluid::fluid_25d::test_detail
