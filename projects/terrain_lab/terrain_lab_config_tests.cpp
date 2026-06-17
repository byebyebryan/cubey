#include "terrain_lab_config.h"
#include "terrain_lab_fields.h"
#include "terrain_lab_mesh.h"

#include <cubey/core/run_config.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef CUBEY_TERRAIN_LAB_SOURCE_DIR
#error "CUBEY_TERRAIN_LAB_SOURCE_DIR must be defined by the terrain_lab test target"
#endif

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(float value, float expected, float tolerance, const char* message) {
    require(value >= expected - tolerance && value <= expected + tolerance, message);
}

void require_near(float value, float expected, float tolerance, const std::string& message) {
    require(value >= expected - tolerance && value <= expected + tolerance, message);
}

float material_sum(const cubey::projects::terrain_lab::TerrainLabMaterialMask& mask) {
    return mask.rock + mask.soil + mask.scree + mask.meadow + mask.forest + mask.snow + mask.sand;
}

struct FieldSampleStats {
    bool saw_material_variation = false;
    bool saw_tree_density = false;
    bool saw_detail = false;
    bool saw_process = false;
    bool saw_divide = false;
    bool saw_channel = false;
    std::vector<bool> saw_drainage_region{};
    double rock_sum = 0.0;
    double soil_sum = 0.0;
    double scree_sum = 0.0;
    double meadow_sum = 0.0;
    double forest_sum = 0.0;
    double snow_sum = 0.0;
    double sand_sum = 0.0;
    double grass_sum = 0.0;
    double shrub_sum = 0.0;
    double tree_sum = 0.0;
    double channel_height_sum = 0.0;
    double channel_soil_sum = 0.0;
    double channel_scree_sum = 0.0;
    double ridge_height_sum = 0.0;
    double ridge_rock_sum = 0.0;
    double ridge_soil_sum = 0.0;
    double ridge_scree_sum = 0.0;
    double channel_wetness_sum = 0.0;
    double channel_deposition_sum = 0.0;
    double channel_flow_sum = 0.0;
    double non_channel_height_sum = 0.0;
    double non_channel_soil_sum = 0.0;
    double non_channel_wetness_sum = 0.0;
    double non_channel_deposition_sum = 0.0;
    double non_channel_flow_sum = 0.0;
    double divide_height_sum = 0.0;
    std::size_t channel_count = 0;
    std::size_t non_channel_count = 0;
    std::size_t divide_count = 0;
    std::size_t ridge_count = 0;
    std::size_t shrub_count = 0;
};

struct RiverHierarchyStats {
    std::size_t active_count = 0;
    std::size_t low_order_count = 0;
    std::size_t high_order_count = 0;
    std::size_t water_sample_count = 0;
    std::size_t wet_component_sample_count = 0;
    std::size_t wet_component_count = 0;
    std::size_t largest_wet_component_count = 0;
    std::uint32_t largest_wet_component_x_span = 0;
    std::uint32_t largest_wet_component_y_span = 0;
    float min_active_width_m = 0.0F;
    float max_active_width_m = 0.0F;
    double low_order_width_sum = 0.0;
    double high_order_width_sum = 0.0;
    double low_order_discharge_sum = 0.0;
    double high_order_discharge_sum = 0.0;
};

RiverHierarchyStats
inspect_river_hierarchy(const cubey::projects::terrain_lab::TerrainLabFieldData& fields) {
    RiverHierarchyStats stats;
    std::vector<std::uint8_t> wet_component_mask(fields.sample_count(), 0U);
    const auto high_order_floor =
        static_cast<std::uint8_t>(fields.max_stream_order > 1U ? fields.max_stream_order - 1U : 1U);
    for (std::size_t index = 0; index < fields.sample_count(); ++index) {
        if (fields.water_presence[index] > 0.01F) {
            ++stats.water_sample_count;
        }
        if (fields.water_presence[index] > 0.08F &&
            fields.river_width_m[index] > fields.desc.cell_size_m * 0.05F) {
            wet_component_mask[index] = 1U;
            ++stats.wet_component_sample_count;
        }
        if (fields.river_width_m[index] > fields.desc.cell_size_m * 0.05F) {
            if (stats.active_count == 0U) {
                stats.min_active_width_m = fields.river_width_m[index];
                stats.max_active_width_m = fields.river_width_m[index];
            } else {
                stats.min_active_width_m =
                    std::min(stats.min_active_width_m, fields.river_width_m[index]);
                stats.max_active_width_m =
                    std::max(stats.max_active_width_m, fields.river_width_m[index]);
            }
            ++stats.active_count;
        }
        if (fields.stream_order[index] == 1U && fields.river_width_m[index] > 0.0F) {
            stats.low_order_width_sum += fields.river_width_m[index];
            stats.low_order_discharge_sum += fields.river_discharge[index];
            ++stats.low_order_count;
        }
        if (fields.max_stream_order > 1U && fields.stream_order[index] >= high_order_floor &&
            fields.river_width_m[index] > 0.0F) {
            stats.high_order_width_sum += fields.river_width_m[index];
            stats.high_order_discharge_sum += fields.river_discharge[index];
            ++stats.high_order_count;
        }
    }

    std::vector<std::uint8_t> visited(fields.sample_count(), 0U);
    std::vector<std::size_t> stack;
    for (std::size_t start = 0; start < fields.sample_count(); ++start) {
        if (wet_component_mask[start] == 0U || visited[start] != 0U) {
            continue;
        }
        ++stats.wet_component_count;
        std::size_t component_count = 0;
        auto min_x = fields.desc.width;
        std::uint32_t max_x = 0U;
        auto min_y = fields.desc.height;
        std::uint32_t max_y = 0U;
        stack.clear();
        stack.push_back(start);
        visited[start] = 1U;
        while (!stack.empty()) {
            const std::size_t sample = stack.back();
            stack.pop_back();
            ++component_count;

            const auto x = static_cast<std::uint32_t>(sample % fields.desc.width);
            const auto y = static_cast<std::uint32_t>(sample / fields.desc.width);
            min_x = std::min(min_x, x);
            max_x = std::max(max_x, x);
            min_y = std::min(min_y, y);
            max_y = std::max(max_y, y);
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    const auto nx = static_cast<std::int32_t>(x) + dx;
                    const auto ny = static_cast<std::int32_t>(y) + dy;
                    if (nx < 0 || ny < 0 || nx >= static_cast<std::int32_t>(fields.desc.width) ||
                        ny >= static_cast<std::int32_t>(fields.desc.height)) {
                        continue;
                    }
                    const std::size_t neighbor =
                        static_cast<std::size_t>(ny) * fields.desc.width +
                        static_cast<std::size_t>(nx);
                    if (wet_component_mask[neighbor] == 0U || visited[neighbor] != 0U) {
                        continue;
                    }
                    visited[neighbor] = 1U;
                    stack.push_back(neighbor);
                }
            }
        }
        if (component_count > stats.largest_wet_component_count) {
            stats.largest_wet_component_count = component_count;
            stats.largest_wet_component_x_span = max_x >= min_x ? max_x - min_x + 1U : 0U;
            stats.largest_wet_component_y_span = max_y >= min_y ? max_y - min_y + 1U : 0U;
        }
    }
    return stats;
}

float mean_or_zero(double sum, std::size_t count) {
    return count == 0U ? 0.0F : static_cast<float>(sum / static_cast<double>(count));
}

FieldSampleStats
inspect_field_samples(const cubey::projects::terrain_lab::TerrainLabFieldData& fields) {
    FieldSampleStats stats;
    stats.saw_drainage_region.assign(fields.drainage_region_count, false);
    for (std::size_t index = 0; index < fields.sample_count(); ++index) {
        require(std::isfinite(fields.height_m[index]), "terrain lab height should be finite");
        require(fields.driver_base_potential[index] >= 0.0F &&
                    fields.driver_base_potential[index] <= 1.0F,
                "terrain lab driver base should be normalized");
        require(fields.driver_relief_potential[index] >= 0.0F &&
                    fields.driver_relief_potential[index] <= 1.0F,
                "terrain lab driver relief should be normalized");
        require(fields.driver_process_potential[index] >= 0.0F &&
                    fields.driver_process_potential[index] <= 1.0F,
                "terrain lab driver process should be normalized");
        require(fields.driver_selection_mask[index] >= 0.0F &&
                    fields.driver_selection_mask[index] <= 1.0F,
                "terrain lab driver selection should be normalized");
        require(std::isfinite(fields.structure_height_m[index]),
                "terrain lab structure should be finite");
        require(std::isfinite(fields.process_delta_m[index]),
                "terrain lab process should be finite");
        require(std::isfinite(fields.detail_height_m[index]),
                "terrain lab detail should be finite");
        require(std::isfinite(fields.slope[index]), "terrain lab slope should be finite");
        require(std::isfinite(fields.curvature[index]), "terrain lab curvature should be finite");
        require(fields.flow_direction[index] <= 8U, "terrain lab flow direction should be valid");
        require(fields.flow_accumulation[index] >= 0.0F,
                "terrain lab flow accumulation should be nonnegative");
        require(fields.stream_power[index] >= 0.0F,
                "terrain lab stream power should be nonnegative");
        require(fields.wetness[index] >= 0.0F && fields.wetness[index] <= 1.0F,
                "terrain lab wetness should be normalized");
        require(fields.deposition[index] >= 0.0F && fields.deposition[index] <= 1.0F,
                "terrain lab deposition should be normalized");
        require(fields.grass_density[index] >= 0.0F && fields.grass_density[index] <= 1.0F,
                "terrain lab grass density should be normalized");
        require(fields.shrub_density[index] >= 0.0F && fields.shrub_density[index] <= 1.0F,
                "terrain lab shrub density should be normalized");
        require(fields.tree_density[index] >= 0.0F && fields.tree_density[index] <= 1.0F,
                "terrain lab tree density should be normalized");
        require(fields.canopy_height_m[index] >= 0.0F,
                "terrain lab canopy height should be nonnegative");
        require(fields.drainage_region_id[index] < fields.drainage_region_count,
                "terrain lab drainage region id should be valid");
        require(fields.divide_influence[index] >= 0.0F && fields.divide_influence[index] <= 1.0F,
                "terrain lab divide influence should be normalized");
        require(fields.channel_influence[index] >= 0.0F && fields.channel_influence[index] <= 1.0F,
                "terrain lab channel influence should be normalized");
        require(fields.channel_distance_m[index] >= 0.0F,
                "terrain lab channel distance should be nonnegative");
        require_near(material_sum(fields.material_masks[index]), 1.0F, 0.001F,
                     "terrain lab material masks should sum to one");

        const cubey::projects::terrain_lab::TerrainLabMaterialMask& material =
            fields.material_masks[index];
        stats.rock_sum += material.rock;
        stats.soil_sum += material.soil;
        stats.scree_sum += material.scree;
        stats.meadow_sum += material.meadow;
        stats.forest_sum += material.forest;
        stats.snow_sum += material.snow;
        stats.sand_sum += material.sand;
        stats.grass_sum += fields.grass_density[index];
        stats.shrub_sum += fields.shrub_density[index];
        stats.tree_sum += fields.tree_density[index];
        stats.saw_material_variation = stats.saw_material_variation || material.rock > 0.05F ||
                                       material.forest > 0.05F || material.snow > 0.05F ||
                                       material.sand > 0.05F;
        stats.saw_tree_density = stats.saw_tree_density || fields.tree_density[index] > 0.01F;
        stats.saw_detail = stats.saw_detail || std::abs(fields.detail_height_m[index]) > 0.001F;
        stats.saw_process = stats.saw_process || std::abs(fields.process_delta_m[index]) > 0.001F;
        stats.saw_divide = stats.saw_divide || fields.divide_influence[index] > 0.2F;
        stats.saw_channel = stats.saw_channel || fields.channel_influence[index] > 0.2F;
        stats.saw_drainage_region[fields.drainage_region_id[index]] = true;
        if (fields.shrub_density[index] > 0.012F) {
            ++stats.shrub_count;
        }
        if (fields.channel_influence[index] > 0.45F) {
            stats.channel_height_sum += fields.height_m[index];
            stats.channel_soil_sum += material.soil;
            stats.channel_scree_sum += material.scree;
            stats.channel_wetness_sum += fields.wetness[index];
            stats.channel_deposition_sum += fields.deposition[index];
            stats.channel_flow_sum += fields.flow_accumulation[index];
            ++stats.channel_count;
        }
        if (fields.channel_influence[index] < 0.05F && fields.divide_influence[index] < 0.25F) {
            stats.non_channel_height_sum += fields.height_m[index];
            stats.non_channel_soil_sum += material.soil;
            stats.non_channel_wetness_sum += fields.wetness[index];
            stats.non_channel_deposition_sum += fields.deposition[index];
            stats.non_channel_flow_sum += fields.flow_accumulation[index];
            ++stats.non_channel_count;
        }
        if (fields.divide_influence[index] > 0.55F && fields.channel_influence[index] < 0.20F) {
            stats.divide_height_sum += fields.height_m[index];
            ++stats.divide_count;
        }
        if (fields.ridge_influence[index] > 0.45F && fields.channel_influence[index] < 0.35F) {
            stats.ridge_height_sum += fields.height_m[index];
            stats.ridge_rock_sum += material.rock;
            stats.ridge_soil_sum += material.soil;
            stats.ridge_scree_sum += material.scree;
            ++stats.ridge_count;
        }
    }
    return stats;
}

struct DriverFieldStats {
    float min_base = 0.0F;
    float max_base = 0.0F;
    float min_relief = 0.0F;
    float max_relief = 0.0F;
    float min_process = 0.0F;
    float max_process = 0.0F;
    float min_selection = 0.0F;
    float max_selection = 0.0F;
};

DriverFieldStats
inspect_driver_fields(const cubey::projects::terrain_lab::TerrainLabFieldData& fields) {
    require(fields.sample_count() > 0U, "terrain lab driver fields require samples");
    DriverFieldStats stats{
        .min_base = fields.driver_base_potential.front(),
        .max_base = fields.driver_base_potential.front(),
        .min_relief = fields.driver_relief_potential.front(),
        .max_relief = fields.driver_relief_potential.front(),
        .min_process = fields.driver_process_potential.front(),
        .max_process = fields.driver_process_potential.front(),
        .min_selection = fields.driver_selection_mask.front(),
        .max_selection = fields.driver_selection_mask.front(),
    };
    for (std::size_t index = 0; index < fields.sample_count(); ++index) {
        stats.min_base = std::min(stats.min_base, fields.driver_base_potential[index]);
        stats.max_base = std::max(stats.max_base, fields.driver_base_potential[index]);
        stats.min_relief = std::min(stats.min_relief, fields.driver_relief_potential[index]);
        stats.max_relief = std::max(stats.max_relief, fields.driver_relief_potential[index]);
        stats.min_process = std::min(stats.min_process, fields.driver_process_potential[index]);
        stats.max_process = std::max(stats.max_process, fields.driver_process_potential[index]);
        stats.min_selection = std::min(stats.min_selection, fields.driver_selection_mask[index]);
        stats.max_selection = std::max(stats.max_selection, fields.driver_selection_mask[index]);
    }
    return stats;
}

void require_slice_driver_guardrails(
    const cubey::projects::terrain_lab::TerrainLabFieldData& fields, const char* slice_name) {
    const DriverFieldStats stats = inspect_driver_fields(fields);
    require_near(stats.min_selection, 1.0F, 0.001F,
                 std::string(slice_name) + " driver selection should use the whole patch");
    require_near(stats.max_selection, 1.0F, 0.001F,
                 std::string(slice_name) + " driver selection should not add a footprint");
    require(stats.max_base - stats.min_base > 0.005F,
            std::string(slice_name) + " driver base should be inspectable");
    require(stats.max_relief - stats.min_relief > 0.005F,
            std::string(slice_name) + " driver relief should be inspectable");
    require(stats.max_process - stats.min_process > 0.001F,
            std::string(slice_name) + " driver process should be inspectable");
}

void require_noise_off_driver_guardrails(
    cubey::projects::terrain_lab::TerrainLabConfig config, const char* slice_name,
    float min_structure_span_m) {
    namespace terrain = cubey::projects::terrain_lab;
    config.detail_strength = 0.0F;
    const terrain::TerrainLabFieldData fields = terrain::generate_terrain_lab_fields(config);
    terrain::validate_terrain_lab_fields(fields);
    require_slice_driver_guardrails(fields, slice_name);

    float min_structure = fields.structure_height_m.front();
    float max_structure = fields.structure_height_m.front();
    bool all_detail_zero = true;
    for (std::size_t index = 0; index < fields.sample_count(); ++index) {
        min_structure = std::min(min_structure, fields.structure_height_m[index]);
        max_structure = std::max(max_structure, fields.structure_height_m[index]);
        all_detail_zero =
            all_detail_zero && std::abs(fields.detail_height_m[index]) <= 0.001F;
        require_near(fields.height_m[index],
                     fields.structure_height_m[index] + fields.process_delta_m[index],
                     0.001F, std::string(slice_name) +
                                 " noise-off height should equal structure plus process");
    }
    require(all_detail_zero, std::string(slice_name) +
                                 " detail can be disabled independently");
    require(max_structure - min_structure > min_structure_span_m,
            std::string(slice_name) + " noise-off structure should remain non-flat");
    require(fields.max_flow_accumulation > 1.0F,
            std::string(slice_name) + " noise-off fields should still route drainage");
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("failed to open text file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

void require_contains(const std::string& text, const std::string& needle) {
    require(text.find(needle) != std::string::npos, "expected text not found: " + needle);
}

void require_shader_debug_constant(const std::string& shader_source,
                                   cubey::projects::terrain_lab::TerrainLabDebugView view,
                                   const char* shader_suffix) {
    const std::string expected = "const uint TERRAIN_LAB_VIEW_" + std::string(shader_suffix) +
                                 " = " + std::to_string(static_cast<std::uint32_t>(view)) + "u;";
    require_contains(shader_source, expected);
}

} // namespace

int main() {
    namespace terrain = cubey::projects::terrain_lab;

    const terrain::TerrainLabConfig defaults{};
    require(defaults.grid_width == terrain::kTerrainLabDefaultGridWidth,
            "terrain lab default width should be stable");
    require(defaults.grid_height == terrain::kTerrainLabDefaultGridHeight,
            "terrain lab default height should be stable");
    require(defaults.seed == terrain::kTerrainLabDefaultSeed,
            "terrain lab default seed should be stable");
    require_near(defaults.cell_size_m, terrain::kTerrainLabDefaultCellSizeMeters, 0.001F,
                 "terrain lab default cell size should be stable");
    require_near(defaults.elevation_scale_m, terrain::kTerrainLabDefaultElevationScaleMeters,
                 0.001F, "terrain lab default elevation scale should be stable");
    require(defaults.slice_preset == terrain::TerrainLabSlicePreset::TemperateMountainRivers,
            "terrain lab should default to the temperate river reference slice");
    require(defaults.camera_preset == terrain::TerrainLabCameraPreset::Orbit,
            "terrain lab should default to the orbit camera");
    require(defaults.noise_source == terrain::TerrainLabNoiseSource::LegacyValue,
            "terrain lab should default to the legacy value noise source");
    require(defaults.debug_view == terrain::TerrainLabDebugView::Final,
            "terrain lab should default to final debug view");
    terrain::validate_terrain_lab_config(defaults);

    require(terrain::terrain_lab_slice_preset_from_name("") ==
                terrain::TerrainLabSlicePreset::TemperateMountainRivers,
            "empty terrain lab slice preset should use the temperate river reference");
    require(terrain::terrain_lab_slice_preset_from_name("arid-mesa-canyon") ==
                terrain::TerrainLabSlicePreset::AridMesaCanyon,
            "terrain lab slice preset should parse arid mesa");
    require(terrain::terrain_lab_slice_preset_from_name("arid_mesa_canyon") ==
                terrain::TerrainLabSlicePreset::AridMesaCanyon,
            "terrain lab slice preset should accept arid mesa underscore alias");
    require(terrain::terrain_lab_slice_preset_from_name("temperate-mountain-rivers") ==
                terrain::TerrainLabSlicePreset::TemperateMountainRivers,
            "terrain lab slice preset should parse canonical name");
    require(terrain::terrain_lab_slice_preset_from_name("temperate_mountain_rivers") ==
                terrain::TerrainLabSlicePreset::TemperateMountainRivers,
            "terrain lab slice preset should accept underscore alias");
    require(terrain::terrain_lab_slice_preset_from_name("temperate-mountain-watershed") ==
                terrain::TerrainLabSlicePreset::TemperateMountainRivers,
            "terrain lab slice preset should accept old watershed alias");
    require(terrain::terrain_lab_slice_preset_from_name("desert-dunes") ==
                terrain::TerrainLabSlicePreset::DesertDunes,
            "terrain lab slice preset should parse desert dunes");
    require(terrain::terrain_lab_slice_preset_from_name("desert_dunes") ==
                terrain::TerrainLabSlicePreset::DesertDunes,
            "terrain lab slice preset should accept desert dunes underscore alias");
    require(terrain::terrain_lab_slice_preset_from_name("alpine-glacial-valley") ==
                terrain::TerrainLabSlicePreset::AlpineGlacialValley,
            "terrain lab slice preset should parse alpine glacial valley");
    require(terrain::terrain_lab_slice_preset_from_name("alpine_glacial_valley") ==
                terrain::TerrainLabSlicePreset::AlpineGlacialValley,
            "terrain lab slice preset should accept alpine glacial valley underscore alias");
    require(terrain::terrain_lab_slice_preset_from_name("mountain-ridges-peaks") ==
                terrain::TerrainLabSlicePreset::MountainRidgesPeaks,
            "terrain lab slice preset should parse mountain ridges and peaks");
    require(terrain::terrain_lab_slice_preset_from_name("mountain_ridges_peaks") ==
                terrain::TerrainLabSlicePreset::MountainRidgesPeaks,
            "terrain lab slice preset should accept mountain ridges and peaks underscore alias");

    require(terrain::terrain_lab_camera_preset_from_name("") ==
                terrain::TerrainLabCameraPreset::Orbit,
            "empty terrain lab camera preset should use orbit");
    require(terrain::terrain_lab_camera_preset_from_name("orbit") ==
                terrain::TerrainLabCameraPreset::Orbit,
            "terrain lab camera preset should parse orbit");
    require(terrain::terrain_lab_camera_preset_from_name("profile") ==
                terrain::TerrainLabCameraPreset::Profile,
            "terrain lab camera preset should parse profile");

    require(terrain::terrain_lab_noise_source_from_name("") ==
                terrain::TerrainLabNoiseSource::LegacyValue,
            "empty terrain lab noise source should use legacy value noise");
    require(terrain::terrain_lab_noise_source_from_name("legacy-value") ==
                terrain::TerrainLabNoiseSource::LegacyValue,
            "terrain lab noise source should parse legacy value");
    require(terrain::terrain_lab_noise_source_from_name("legacy_value") ==
                terrain::TerrainLabNoiseSource::LegacyValue,
            "terrain lab noise source should accept legacy underscore alias");
    require(terrain::terrain_lab_noise_source_from_name("fastnoise-lite") ==
                terrain::TerrainLabNoiseSource::FastNoiseLite,
            "terrain lab noise source should parse FastNoiseLite");
    require(terrain::terrain_lab_noise_source_from_name("fastnoise_lite") ==
                terrain::TerrainLabNoiseSource::FastNoiseLite,
            "terrain lab noise source should accept FastNoiseLite underscore alias");

    require(terrain::terrain_lab_debug_view_from_name("") == terrain::TerrainLabDebugView::Final,
            "empty terrain lab debug view should use final");
    require(terrain::terrain_lab_debug_view_from_name("height") ==
                terrain::TerrainLabDebugView::Height,
            "terrain lab height debug view should parse");
    require(terrain::terrain_lab_debug_view_from_name("flow-direction") ==
                terrain::TerrainLabDebugView::FlowDirection,
            "terrain lab flow direction debug view should parse");
    require(terrain::terrain_lab_debug_view_from_name("flow_direction") ==
                terrain::TerrainLabDebugView::FlowDirection,
            "terrain lab debug views should accept underscore aliases");
    require(terrain::terrain_lab_debug_view_from_name("flow-accumulation") ==
                terrain::TerrainLabDebugView::FlowAccumulation,
            "terrain lab flow accumulation debug view should parse");
    require(terrain::terrain_lab_debug_view_from_name("stream-power") ==
                terrain::TerrainLabDebugView::StreamPower,
            "terrain lab stream power debug view should parse");
    require(terrain::terrain_lab_debug_view_from_name("river-network") ==
                terrain::TerrainLabDebugView::RiverNetwork,
            "terrain lab river network debug view should parse");
    require(terrain::terrain_lab_debug_view_from_name("river_width") ==
                terrain::TerrainLabDebugView::RiverWidth,
            "terrain lab river width debug view should parse underscore alias");
    require(terrain::terrain_lab_debug_view_from_name("water-presence") ==
                terrain::TerrainLabDebugView::WaterPresence,
            "terrain lab water presence debug view should parse");
    require(terrain::terrain_lab_debug_view_from_name("biome-density") ==
                terrain::TerrainLabDebugView::BiomeDensity,
            "terrain lab biome density debug view should parse");
    require(terrain::terrain_lab_debug_view_from_name("canopy-height") ==
                terrain::TerrainLabDebugView::CanopyHeight,
            "terrain lab canopy height debug view should parse");
    require(terrain::terrain_lab_debug_view_from_name("noise-off") ==
                terrain::TerrainLabDebugView::NoiseOff,
            "terrain lab noise-off debug view should parse");
    require(terrain::terrain_lab_debug_view_from_name("feature-graph") ==
                terrain::TerrainLabDebugView::FeatureGraph,
            "terrain lab feature graph debug view should parse");
    require(terrain::terrain_lab_debug_view_from_name("drainage-regions") ==
                terrain::TerrainLabDebugView::DrainageRegions,
            "terrain lab drainage region debug view should parse");
    require(terrain::terrain_lab_debug_view_from_name("watershed") ==
                terrain::TerrainLabDebugView::DrainageRegions,
            "terrain lab debug view should accept old watershed alias");
    require(terrain::terrain_lab_debug_view_from_name("channel") ==
                terrain::TerrainLabDebugView::Channel,
            "terrain lab channel debug view should parse");
    require(terrain::terrain_lab_debug_view_from_name("divide") ==
                terrain::TerrainLabDebugView::Divide,
            "terrain lab divide debug view should parse");
    require(terrain::terrain_lab_debug_view_from_name("driver") ==
                terrain::TerrainLabDebugView::Driver,
            "terrain lab driver debug view should parse");
    require(terrain::next_terrain_lab_debug_view(terrain::TerrainLabDebugView::Detail) ==
                terrain::TerrainLabDebugView::Slope,
            "terrain lab debug view cycle should enter geometry views after detail");
    require(terrain::next_terrain_lab_debug_view(terrain::TerrainLabDebugView::StreamPower) ==
                terrain::TerrainLabDebugView::RiverNetwork,
            "terrain lab debug view cycle should enter river diagnostics after stream power");
    require(terrain::next_terrain_lab_debug_view(terrain::TerrainLabDebugView::WaterPresence) ==
                terrain::TerrainLabDebugView::Wetness,
            "terrain lab debug view cycle should leave river diagnostics for wetness");
    require(terrain::next_terrain_lab_debug_view(terrain::TerrainLabDebugView::NoiseOff) ==
                terrain::TerrainLabDebugView::FeatureGraph,
            "terrain lab debug view cycle should enter feature graph diagnostics after noise-off");
    require(terrain::next_terrain_lab_debug_view(terrain::TerrainLabDebugView::Divide) ==
                terrain::TerrainLabDebugView::Driver,
            "terrain lab debug view cycle should enter driver diagnostics after divide");
    require(terrain::next_terrain_lab_debug_view(terrain::TerrainLabDebugView::Driver) ==
                terrain::TerrainLabDebugView::Final,
            "terrain lab debug view cycle should wrap");

    const std::string terrain_lab_fragment_shader = read_text_file(
        std::filesystem::path(CUBEY_TERRAIN_LAB_SOURCE_DIR) / "shaders" / "terrain_lab.frag");
    require_shader_debug_constant(terrain_lab_fragment_shader, terrain::TerrainLabDebugView::Final,
                                  "FINAL");
    require_shader_debug_constant(terrain_lab_fragment_shader, terrain::TerrainLabDebugView::Height,
                                  "HEIGHT");
    require_shader_debug_constant(terrain_lab_fragment_shader,
                                  terrain::TerrainLabDebugView::Structure, "STRUCTURE");
    require_shader_debug_constant(terrain_lab_fragment_shader,
                                  terrain::TerrainLabDebugView::Process, "PROCESS");
    require_shader_debug_constant(terrain_lab_fragment_shader, terrain::TerrainLabDebugView::Detail,
                                  "DETAIL");
    require_shader_debug_constant(terrain_lab_fragment_shader, terrain::TerrainLabDebugView::Slope,
                                  "SLOPE");
    require_shader_debug_constant(terrain_lab_fragment_shader,
                                  terrain::TerrainLabDebugView::Curvature, "CURVATURE");
    require_shader_debug_constant(terrain_lab_fragment_shader,
                                  terrain::TerrainLabDebugView::FlowDirection, "FLOW_DIRECTION");
    require_shader_debug_constant(terrain_lab_fragment_shader,
                                  terrain::TerrainLabDebugView::FlowAccumulation,
                                  "FLOW_ACCUMULATION");
    require_shader_debug_constant(terrain_lab_fragment_shader,
                                  terrain::TerrainLabDebugView::StreamPower, "STREAM_POWER");
    require_shader_debug_constant(terrain_lab_fragment_shader,
                                  terrain::TerrainLabDebugView::RiverNetwork, "RIVER_NETWORK");
    require_shader_debug_constant(terrain_lab_fragment_shader,
                                  terrain::TerrainLabDebugView::RiverWidth, "RIVER_WIDTH");
    require_shader_debug_constant(terrain_lab_fragment_shader,
                                  terrain::TerrainLabDebugView::WaterPresence, "WATER_PRESENCE");
    require_shader_debug_constant(terrain_lab_fragment_shader,
                                  terrain::TerrainLabDebugView::Wetness, "WETNESS");
    require_shader_debug_constant(terrain_lab_fragment_shader,
                                  terrain::TerrainLabDebugView::Deposition, "DEPOSITION");
    require_shader_debug_constant(terrain_lab_fragment_shader,
                                  terrain::TerrainLabDebugView::Material, "MATERIAL");
    require_shader_debug_constant(terrain_lab_fragment_shader,
                                  terrain::TerrainLabDebugView::BiomeDensity, "BIOME_DENSITY");
    require_shader_debug_constant(terrain_lab_fragment_shader,
                                  terrain::TerrainLabDebugView::CanopyHeight, "CANOPY_HEIGHT");
    require_shader_debug_constant(terrain_lab_fragment_shader,
                                  terrain::TerrainLabDebugView::NoiseOff, "NOISE_OFF");
    require_shader_debug_constant(terrain_lab_fragment_shader,
                                  terrain::TerrainLabDebugView::FeatureGraph, "FEATURE_GRAPH");
    require_shader_debug_constant(terrain_lab_fragment_shader,
                                  terrain::TerrainLabDebugView::DrainageRegions, "DRAINAGE_REGIONS");
    require_shader_debug_constant(terrain_lab_fragment_shader,
                                  terrain::TerrainLabDebugView::Channel, "CHANNEL");
    require_shader_debug_constant(terrain_lab_fragment_shader, terrain::TerrainLabDebugView::Divide,
                                  "DIVIDE");
    require_shader_debug_constant(terrain_lab_fragment_shader, terrain::TerrainLabDebugView::Driver,
                                  "DRIVER");
    require_contains(terrain_lab_fragment_shader, "strata_band_strength");
    require_contains(terrain_lab_fragment_shader, "caprock_strength");
    require_contains(terrain_lab_fragment_shader, "talus_proxy");
    require_contains(terrain_lab_fragment_shader, "scrub_proxy");

    bool rejected = false;
    try {
        static_cast<void>(terrain::terrain_lab_debug_view_from_name("shoreline"));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain lab should reject unknown debug views");

    rejected = false;
    try {
        static_cast<void>(terrain::terrain_lab_slice_preset_from_name("coastal-island"));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain lab should reject unknown slice presets");

    rejected = false;
    try {
        static_cast<void>(terrain::terrain_lab_camera_preset_from_name("telephoto"));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain lab should reject unknown camera presets");

    rejected = false;
    try {
        static_cast<void>(terrain::terrain_lab_noise_source_from_name("shader-noise"));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain lab should reject unknown noise sources");

    cubey::RunConfig run_config;
    run_config.grid.width = 65;
    run_config.grid.height = 33;
    run_config.debug_view = "wetness";
    run_config.terrain_lab.slice_preset = "temperate-mountain-rivers";
    run_config.terrain_lab.camera_preset = "profile";
    run_config.terrain_lab.noise_source = "fastnoise-lite";
    run_config.terrain.seed = 77U;
    run_config.terrain.seed_set = true;
    run_config.terrain.cell_size = 6.0F;
    run_config.terrain.relief = 1.25F;
    const terrain::TerrainLabConfig from_run_config =
        terrain::terrain_lab_config_from_run_config(run_config);
    require(from_run_config.grid_width == 65U,
            "terrain lab should read grid width from common run config");
    require(from_run_config.grid_height == 33U,
            "terrain lab should read grid height from common run config");
    require(from_run_config.debug_view == terrain::TerrainLabDebugView::Wetness,
            "terrain lab should read debug view from common run config");
    require(from_run_config.slice_preset ==
                terrain::TerrainLabSlicePreset::TemperateMountainRivers,
            "terrain lab should read slice preset from common run config");
    require(from_run_config.camera_preset == terrain::TerrainLabCameraPreset::Profile,
            "terrain lab should read camera preset from common run config");
    require(from_run_config.noise_source == terrain::TerrainLabNoiseSource::FastNoiseLite,
            "terrain lab should read noise source from common run config");
    require(from_run_config.seed == terrain::kTerrainLabDefaultSeed,
            "terrain lab should not read coast-oriented terrain seed flags");
    require_near(from_run_config.cell_size_m, terrain::kTerrainLabDefaultCellSizeMeters, 0.001F,
                 "terrain lab should not read coast-oriented terrain cell-size flags");
    require_near(from_run_config.structure_strength, terrain::kTerrainLabDefaultStructureStrength,
                 0.001F, "terrain lab should not read coast-oriented terrain relief flags");

    terrain::TerrainLabConfig invalid = defaults;
    invalid.grid_width = terrain::kTerrainLabMinGridExtent - 1U;
    rejected = false;
    try {
        terrain::validate_terrain_lab_config(invalid);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain lab should reject too-small grids");

    terrain::TerrainLabConfig large = defaults;
    large.grid_width = terrain::kTerrainLabMaxGridExtent;
    large.grid_height = terrain::kTerrainLabMaxGridExtent;
    terrain::validate_terrain_lab_config(large);

    invalid = defaults;
    invalid.grid_height = terrain::kTerrainLabMaxGridExtent + 1U;
    rejected = false;
    try {
        terrain::validate_terrain_lab_config(invalid);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain lab should reject too-large grids");

    invalid = defaults;
    invalid.cell_size_m = 0.0F;
    rejected = false;
    try {
        terrain::validate_terrain_lab_config(invalid);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain lab should reject nonpositive cell size");

    invalid = defaults;
    invalid.elevation_scale_m = std::nanf("");
    rejected = false;
    try {
        terrain::validate_terrain_lab_config(invalid);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain lab should reject nonfinite elevation scale");

    invalid = defaults;
    invalid.structure_strength = -0.1F;
    rejected = false;
    try {
        terrain::validate_terrain_lab_config(invalid);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain lab should reject invalid structure strength");

    invalid = defaults;
    invalid.process_strength = 4.5F;
    rejected = false;
    try {
        terrain::validate_terrain_lab_config(invalid);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain lab should reject invalid process strength");

    invalid = defaults;
    invalid.detail_strength = -0.1F;
    rejected = false;
    try {
        terrain::validate_terrain_lab_config(invalid);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain lab should reject invalid detail strength");

    terrain::TerrainLabConfig small = defaults;
    small.slice_preset = terrain::TerrainLabSlicePreset::AridMesaCanyon;
    small.grid_width = 65U;
    small.grid_height = 65U;
    small.cell_size_m = 64.0F;
    const terrain::TerrainLabFieldData fields = terrain::generate_terrain_lab_fields(small);
    terrain::validate_terrain_lab_fields(fields);
    require_slice_driver_guardrails(fields, "terrain lab arid slice");
    const float half_extent =
        static_cast<float>(fields.desc.width - 1U) * fields.desc.cell_size_m * 0.5F;
    require(fields.sample_count() == 65U * 65U, "terrain lab fields should match grid dimensions");
    require_near(terrain::terrain_lab_grid_sample_x_m(fields.desc, 0U), -half_extent, 0.001F,
                 "terrain lab grid should be centered on X");
    require_near(terrain::terrain_lab_grid_sample_z_m(fields.desc, 0U), -half_extent, 0.001F,
                 "terrain lab grid should be centered on Z");
    require_near(terrain::terrain_lab_grid_sample_x_m(fields.desc, fields.desc.width / 2U), 0.0F,
                 0.001F, "terrain lab center sample should land on origin X");
    require_near(terrain::terrain_lab_grid_sample_z_m(fields.desc, fields.desc.height / 2U), 0.0F,
                 0.001F, "terrain lab center sample should land on origin Z");

    require(fields.height_m.size() == fields.sample_count(),
            "terrain lab height field size mismatch");
    require(fields.driver_base_potential.size() == fields.sample_count(),
            "terrain lab driver base field size mismatch");
    require(fields.driver_relief_potential.size() == fields.sample_count(),
            "terrain lab driver relief field size mismatch");
    require(fields.driver_process_potential.size() == fields.sample_count(),
            "terrain lab driver process field size mismatch");
    require(fields.driver_selection_mask.size() == fields.sample_count(),
            "terrain lab driver selection field size mismatch");
    require(fields.structure_height_m.size() == fields.sample_count(),
            "terrain lab structure field size mismatch");
    require(fields.process_delta_m.size() == fields.sample_count(),
            "terrain lab process field size mismatch");
    require(fields.detail_height_m.size() == fields.sample_count(),
            "terrain lab detail field size mismatch");
    require(fields.slope.size() == fields.sample_count(), "terrain lab slope field size mismatch");
    require(fields.curvature.size() == fields.sample_count(),
            "terrain lab curvature field size mismatch");
    require(fields.flow_direction.size() == fields.sample_count(),
            "terrain lab flow direction field size mismatch");
    require(fields.flow_accumulation.size() == fields.sample_count(),
            "terrain lab flow accumulation field size mismatch");
    require(fields.stream_power.size() == fields.sample_count(),
            "terrain lab stream power field size mismatch");
    require(fields.river_discharge.size() == fields.sample_count(),
            "terrain lab river discharge field size mismatch");
    require(fields.stream_order.size() == fields.sample_count(),
            "terrain lab stream order field size mismatch");
    require(fields.river_width_m.size() == fields.sample_count(),
            "terrain lab river width field size mismatch");
    require(fields.valley_width_m.size() == fields.sample_count(),
            "terrain lab valley width field size mismatch");
    require(fields.water_presence.size() == fields.sample_count(),
            "terrain lab water presence field size mismatch");
    require(fields.wetness.size() == fields.sample_count(),
            "terrain lab wetness field size mismatch");
    require(fields.deposition.size() == fields.sample_count(),
            "terrain lab deposition field size mismatch");
    require(fields.material_masks.size() == fields.sample_count(),
            "terrain lab material field size mismatch");
    require(fields.grass_density.size() == fields.sample_count(),
            "terrain lab grass field size mismatch");
    require(fields.shrub_density.size() == fields.sample_count(),
            "terrain lab shrub field size mismatch");
    require(fields.tree_density.size() == fields.sample_count(),
            "terrain lab tree field size mismatch");
    require(fields.canopy_height_m.size() == fields.sample_count(),
            "terrain lab canopy field size mismatch");
    require(fields.ridge_influence.size() == fields.sample_count(),
            "terrain lab ridge field size mismatch");
    require(fields.valley_influence.size() == fields.sample_count(),
            "terrain lab valley field size mismatch");
    require(fields.basin_influence.size() == fields.sample_count(),
            "terrain lab basin field size mismatch");
    require(fields.drainage_region_id.size() == fields.sample_count(),
            "terrain lab drainage region field size mismatch");
    require(fields.divide_influence.size() == fields.sample_count(),
            "terrain lab divide field size mismatch");
    require(fields.channel_influence.size() == fields.sample_count(),
            "terrain lab channel field size mismatch");
    require(fields.channel_distance_m.size() == fields.sample_count(),
            "terrain lab channel distance field size mismatch");
    require(fields.drainage_region_count == 1U, "terrain lab arid slice should use one local basin");
    require(fields.max_channel_distance_m > fields.desc.cell_size_m,
            "terrain lab feature graph should track channel distance range");

    require(fields.max_height_m - fields.min_height_m > 100.0F,
            "terrain lab fields should produce non-flat terrain");
    require(fields.max_slope > 0.0F, "terrain lab fields should produce nonzero slopes");
    require(fields.max_abs_curvature > 0.0F, "terrain lab fields should produce nonzero curvature");
    require(fields.max_flow_accumulation > 1.0F, "terrain lab fields should accumulate drainage");
    require(fields.max_stream_power >= 0.0F, "terrain lab stream power should stay nonnegative");
    require(fields.max_wetness > 0.0F && fields.max_wetness <= 1.0F,
            "terrain lab wetness should be normalized");
    require(fields.max_deposition > 0.0F && fields.max_deposition <= 1.0F,
            "terrain lab deposition should be normalized");

    const FieldSampleStats arid_stats = inspect_field_samples(fields);
    const RiverHierarchyStats arid_river_stats = inspect_river_hierarchy(fields);
    require(arid_stats.saw_material_variation,
            "terrain lab arid slice should produce varied material masks");
    require(arid_stats.saw_detail, "terrain lab arid slice should produce detail fields");
    require(arid_stats.saw_process, "terrain lab arid slice should produce process fields");
    require(arid_stats.saw_divide, "terrain lab arid slice should produce mesa divide fields");
    require(arid_stats.saw_channel,
            "terrain lab arid slice should produce dry wash and canyon channel fields");
    require(std::all_of(arid_stats.saw_drainage_region.begin(), arid_stats.saw_drainage_region.end(),
                        [](bool saw) { return saw; }),
            "terrain lab arid slice should rasterize its local basin");
    require(arid_stats.channel_count > 16U,
            "terrain lab arid slice should produce enough channel samples");
    require(arid_stats.non_channel_count > 16U,
            "terrain lab arid slice should produce enough non-channel samples: " +
                std::to_string(arid_stats.non_channel_count));
    require(arid_stats.divide_count > 16U,
            "terrain lab arid slice should produce enough mesa divide samples: " +
                std::to_string(arid_stats.divide_count));
    require(arid_stats.ridge_count > 16U,
            "terrain lab arid slice should produce enough rim and wall samples: " +
                std::to_string(arid_stats.ridge_count));
    require(arid_stats.channel_count < fields.sample_count() / 5U,
            "terrain lab arid slice should keep secondary washes subordinate");
    require(fields.max_stream_order >= 3U,
            "terrain lab arid slice should expose a hierarchical dry river network");
    require(fields.max_river_width_m > fields.desc.cell_size_m * 1.5F,
            "terrain lab arid slice should widen trunk channels");
    require(fields.max_valley_width_m > fields.max_river_width_m * 4.0F,
            "terrain lab arid slice should derive valley width from river hierarchy");
    require(arid_river_stats.active_count > 64U,
            "terrain lab arid slice should produce enough dry river samples");
    require(arid_river_stats.low_order_count > 16U && arid_river_stats.high_order_count > 0U,
            "terrain lab arid slice should expose low and high stream orders");
    const float arid_low_order_width =
        mean_or_zero(arid_river_stats.low_order_width_sum, arid_river_stats.low_order_count);
    const float arid_high_order_width =
        mean_or_zero(arid_river_stats.high_order_width_sum, arid_river_stats.high_order_count);
    const float arid_low_order_discharge =
        mean_or_zero(arid_river_stats.low_order_discharge_sum, arid_river_stats.low_order_count);
    const float arid_high_order_discharge =
        mean_or_zero(arid_river_stats.high_order_discharge_sum, arid_river_stats.high_order_count);
    require(arid_high_order_width > arid_low_order_width + fields.desc.cell_size_m * 0.45F,
            "terrain lab arid trunk channels should be wider than tributaries");
    require(arid_high_order_discharge > arid_low_order_discharge * 2.0F,
            "terrain lab arid trunk channels should carry more derived discharge");
    require(arid_river_stats.max_active_width_m >
                arid_river_stats.min_active_width_m + fields.desc.cell_size_m * 1.25F,
            "terrain lab arid river widths should vary across the network");
    require(arid_river_stats.water_sample_count == 0U,
            "terrain lab arid canyon network should remain a dry river expression");
    const double inv_arid_count = 1.0 / static_cast<double>(fields.sample_count());
    const float arid_rock_scree_soil = static_cast<float>(
        (arid_stats.rock_sum + arid_stats.scree_sum + arid_stats.soil_sum) * inv_arid_count);
    const float arid_mean_forest = static_cast<float>(arid_stats.forest_sum * inv_arid_count);
    const float arid_mean_shrub_density = static_cast<float>(arid_stats.shrub_sum * inv_arid_count);
    const float arid_mean_tree_density = static_cast<float>(arid_stats.tree_sum * inv_arid_count);
    require(arid_rock_scree_soil > 0.80F,
            "terrain lab arid slice should be dominated by rock, scree, and soil");
    require(arid_mean_forest < 0.002F,
            "terrain lab arid slice should keep forest material nearly absent");
    require(static_cast<float>(arid_stats.snow_sum * inv_arid_count) < 0.001F,
            "terrain lab arid slice should not produce snow");
    require(arid_mean_shrub_density > arid_mean_tree_density + 0.005F,
            "terrain lab arid slice should expose sparse shrub proxies before trees");
    require(arid_mean_shrub_density < 0.12F,
            "terrain lab arid slice should keep shrub density sparse");
    require(arid_stats.shrub_count > 16U,
            "terrain lab arid slice should produce enough sparse shrub samples");
    const float arid_mean_channel_height = static_cast<float>(
        arid_stats.channel_height_sum / static_cast<double>(arid_stats.channel_count));
    const float arid_mean_non_channel_height = static_cast<float>(
        arid_stats.non_channel_height_sum / static_cast<double>(arid_stats.non_channel_count));
    const float arid_mean_channel_soil = static_cast<float>(
        arid_stats.channel_soil_sum / static_cast<double>(arid_stats.channel_count));
    const float arid_mean_channel_scree = static_cast<float>(
        arid_stats.channel_scree_sum / static_cast<double>(arid_stats.channel_count));
    const float arid_mean_non_channel_soil = static_cast<float>(
        arid_stats.non_channel_soil_sum / static_cast<double>(arid_stats.non_channel_count));
    const float arid_mean_ridge_rock_scree =
        static_cast<float>((arid_stats.ridge_rock_sum + arid_stats.ridge_scree_sum) /
                           static_cast<double>(arid_stats.ridge_count));
    const float arid_mean_ridge_soil =
        static_cast<float>(arid_stats.ridge_soil_sum / static_cast<double>(arid_stats.ridge_count));
    require(arid_mean_channel_height < arid_mean_non_channel_height - 8.0F,
            "terrain lab arid canyon floor should stay lower than mesa terrain");
    require(arid_mean_channel_soil > 0.28F,
            "terrain lab arid canyon floor should keep a warm soil/deposition component");
    require(arid_mean_channel_soil > arid_mean_non_channel_soil,
            "terrain lab arid canyon floor should be soilier than non-channel terrain");
    require(arid_mean_ridge_rock_scree > arid_mean_ridge_soil,
            "terrain lab arid canyon should retain exposed rock and scree walls");
    require(arid_mean_ridge_rock_scree > arid_mean_channel_scree + 0.10F,
            "terrain lab arid canyon should favor talus and rock on walls over wash floors");
    std::size_t arid_driver_ridge_count = 0;
    std::size_t arid_driver_channel_count = 0;
    std::size_t arid_driver_non_channel_count = 0;
    std::size_t arid_driver_high_relief_count = 0;
    double arid_driver_ridge_relief_sum = 0.0;
    double arid_driver_channel_relief_sum = 0.0;
    double arid_driver_channel_process_sum = 0.0;
    double arid_driver_non_channel_process_sum = 0.0;
    double arid_driver_high_relief_height_sum = 0.0;
    std::size_t arid_river_channel_count = 0;
    std::size_t arid_river_non_channel_count = 0;
    double arid_river_channel_width_sum = 0.0;
    double arid_river_non_channel_width_sum = 0.0;
    double arid_river_channel_order_sum = 0.0;
    double arid_river_non_channel_order_sum = 0.0;
    double arid_river_channel_discharge_sum = 0.0;
    double arid_river_non_channel_discharge_sum = 0.0;
    for (std::size_t index = 0; index < fields.sample_count(); ++index) {
        const float relief = fields.driver_relief_potential[index];
        const float process = fields.driver_process_potential[index];
        if (fields.ridge_influence[index] > 0.45F && fields.channel_influence[index] < 0.35F) {
            arid_driver_ridge_relief_sum += relief;
            ++arid_driver_ridge_count;
        }
        if (fields.channel_influence[index] > 0.45F) {
            arid_driver_channel_relief_sum += relief;
            arid_driver_channel_process_sum += process;
            ++arid_driver_channel_count;
            arid_river_channel_width_sum += fields.river_width_m[index];
            arid_river_channel_order_sum += fields.stream_order[index];
            arid_river_channel_discharge_sum += fields.river_discharge[index];
            ++arid_river_channel_count;
        }
        if (fields.channel_influence[index] < 0.05F && fields.divide_influence[index] < 0.25F) {
            arid_driver_non_channel_process_sum += process;
            ++arid_driver_non_channel_count;
            arid_river_non_channel_width_sum += fields.river_width_m[index];
            arid_river_non_channel_order_sum += fields.stream_order[index];
            arid_river_non_channel_discharge_sum += fields.river_discharge[index];
            ++arid_river_non_channel_count;
        }
        if (relief > 0.38F) {
            arid_driver_high_relief_height_sum += fields.height_m[index];
            ++arid_driver_high_relief_count;
        }
    }
    require(arid_driver_ridge_count > 16U && arid_driver_channel_count > 16U,
            "terrain lab arid driver should produce enough ridge and channel samples");
    require(arid_driver_non_channel_count > 16U,
            "terrain lab arid driver should produce enough non-channel comparison samples");
    require(arid_driver_high_relief_count > 16U,
            "terrain lab arid driver should produce enough high-relief samples");
    require(arid_river_channel_count > 16U && arid_river_non_channel_count > 16U,
            "terrain lab arid river-derived canyon checks need enough comparison samples");
    const float arid_mean_ridge_driver_relief = static_cast<float>(
        arid_driver_ridge_relief_sum / static_cast<double>(arid_driver_ridge_count));
    const float arid_mean_channel_driver_relief = static_cast<float>(
        arid_driver_channel_relief_sum / static_cast<double>(arid_driver_channel_count));
    const float arid_mean_channel_driver_process = static_cast<float>(
        arid_driver_channel_process_sum / static_cast<double>(arid_driver_channel_count));
    const float arid_mean_non_channel_driver_process = static_cast<float>(
        arid_driver_non_channel_process_sum / static_cast<double>(arid_driver_non_channel_count));
    const float arid_mean_high_relief_height = static_cast<float>(
        arid_driver_high_relief_height_sum / static_cast<double>(arid_driver_high_relief_count));
    const float arid_mean_channel_river_width =
        static_cast<float>(arid_river_channel_width_sum /
                           static_cast<double>(arid_river_channel_count));
    const float arid_mean_non_channel_river_width =
        static_cast<float>(arid_river_non_channel_width_sum /
                           static_cast<double>(arid_river_non_channel_count));
    const float arid_mean_channel_stream_order =
        static_cast<float>(arid_river_channel_order_sum /
                           static_cast<double>(arid_river_channel_count));
    const float arid_mean_non_channel_stream_order =
        static_cast<float>(arid_river_non_channel_order_sum /
                           static_cast<double>(arid_river_non_channel_count));
    const float arid_mean_channel_discharge =
        static_cast<float>(arid_river_channel_discharge_sum /
                           static_cast<double>(arid_river_channel_count));
    const float arid_mean_non_channel_discharge =
        static_cast<float>(arid_river_non_channel_discharge_sum /
                           static_cast<double>(arid_river_non_channel_count));
    require(arid_mean_ridge_driver_relief > arid_mean_channel_driver_relief + 0.04F,
            "terrain lab arid canyon walls should derive from higher relief driver values");
    require(arid_mean_channel_driver_process > arid_mean_non_channel_driver_process + 0.03F,
            "terrain lab arid washes should derive from higher process driver values");
    require(arid_mean_high_relief_height > arid_mean_channel_height + 8.0F,
            "terrain lab arid canyon height should keep high-relief walls above wash floors");
    require(arid_mean_channel_river_width >
                arid_mean_non_channel_river_width + fields.desc.cell_size_m * 0.20F,
            "terrain lab arid canyon channels should derive from wider river hierarchy");
    require(arid_mean_channel_stream_order > arid_mean_non_channel_stream_order + 0.20F,
            "terrain lab arid canyon channels should carry higher stream order");
    require(arid_mean_channel_discharge > arid_mean_non_channel_discharge * 1.25F,
            "terrain lab arid canyon channels should carry higher river discharge");
    std::array<bool, 4> arid_channel_quadrants{false, false, false, false};
    constexpr std::size_t kAridNetworkBinCount = 8U;
    std::array<bool, kAridNetworkBinCount> arid_channel_x_bins{};
    std::array<bool, kAridNetworkBinCount> arid_channel_z_bins{};
    std::array<bool, 8> arid_channel_flow_directions{};
    std::size_t arid_network_channel_count = 0;
    double arid_network_x_sum = 0.0;
    double arid_network_z_sum = 0.0;
    double arid_network_xz_sum = 0.0;
    double arid_network_zz_sum = 0.0;
    const float arid_half_x =
        static_cast<float>(fields.desc.width - 1U) * fields.desc.cell_size_m * 0.5F;
    const float arid_half_z =
        static_cast<float>(fields.desc.height - 1U) * fields.desc.cell_size_m * 0.5F;
    for (std::size_t index = 0; index < fields.sample_count(); ++index) {
        if (fields.channel_influence[index] <= 0.30F) {
            continue;
        }
        const auto x = static_cast<std::uint32_t>(index % fields.desc.width);
        const auto y = static_cast<std::uint32_t>(index / fields.desc.width);
        const float nx = arid_half_x == 0.0F
                             ? 0.0F
                             : terrain::terrain_lab_grid_sample_x_m(fields.desc, x) / arid_half_x;
        const float nz = arid_half_z == 0.0F
                             ? 0.0F
                             : terrain::terrain_lab_grid_sample_z_m(fields.desc, y) / arid_half_z;
        arid_network_x_sum += nx;
        arid_network_z_sum += nz;
        arid_network_xz_sum += nx * nz;
        arid_network_zz_sum += nz * nz;
        ++arid_network_channel_count;
        const std::size_t quadrant =
            (x >= fields.desc.width / 2U ? 1U : 0U) + (y >= fields.desc.height / 2U ? 2U : 0U);
        arid_channel_quadrants[quadrant] = true;
        const std::size_t x_bin = std::min(kAridNetworkBinCount - 1U,
                                           static_cast<std::size_t>(x) *
                                               kAridNetworkBinCount / fields.desc.width);
        const std::size_t z_bin = std::min(kAridNetworkBinCount - 1U,
                                           static_cast<std::size_t>(y) *
                                               kAridNetworkBinCount / fields.desc.height);
        arid_channel_x_bins[x_bin] = true;
        arid_channel_z_bins[z_bin] = true;
        const std::uint8_t flow_direction = fields.flow_direction[index];
        if (flow_direction < arid_channel_flow_directions.size()) {
            arid_channel_flow_directions[flow_direction] = true;
        }
    }
    const std::uint32_t arid_channel_quadrant_count = static_cast<std::uint32_t>(
        std::count(arid_channel_quadrants.begin(), arid_channel_quadrants.end(), true));
    const std::uint32_t arid_channel_x_bin_count = static_cast<std::uint32_t>(
        std::count(arid_channel_x_bins.begin(), arid_channel_x_bins.end(), true));
    const std::uint32_t arid_channel_z_bin_count = static_cast<std::uint32_t>(
        std::count(arid_channel_z_bins.begin(), arid_channel_z_bins.end(), true));
    const std::uint32_t arid_channel_flow_direction_count = static_cast<std::uint32_t>(
        std::count(arid_channel_flow_directions.begin(), arid_channel_flow_directions.end(), true));
    require(arid_network_channel_count > 16U,
            "terrain lab arid network should expose enough routed channel samples");
    require(arid_channel_quadrant_count >= 3U,
            "terrain lab arid network should span multiple crop quadrants");
    require(arid_channel_x_bin_count >= 4U && arid_channel_z_bin_count >= 4U,
            "terrain lab arid network should span routing bins (x=" +
                std::to_string(arid_channel_x_bin_count) +
                ", z=" + std::to_string(arid_channel_z_bin_count) + ")");
    require(arid_channel_flow_direction_count >= 4U,
            "terrain lab arid network should use multiple downstream directions (directions=" +
                std::to_string(arid_channel_flow_direction_count) + ")");
    const double line_denominator =
        static_cast<double>(arid_network_channel_count) * arid_network_zz_sum -
        arid_network_z_sum * arid_network_z_sum;
    require(std::abs(line_denominator) > 0.00001,
            "terrain lab arid network should not collapse to one horizontal band");
    const double fitted_slope =
        ((static_cast<double>(arid_network_channel_count) * arid_network_xz_sum) -
         (arid_network_z_sum * arid_network_x_sum)) /
        line_denominator;
    const double fitted_intercept = (arid_network_x_sum - fitted_slope * arid_network_z_sum) /
                                    static_cast<double>(arid_network_channel_count);
    double residual_sum = 0.0;
    for (std::size_t index = 0; index < fields.sample_count(); ++index) {
        if (fields.channel_influence[index] <= 0.30F) {
            continue;
        }
        const auto x = static_cast<std::uint32_t>(index % fields.desc.width);
        const auto y = static_cast<std::uint32_t>(index / fields.desc.width);
        const float nx = arid_half_x == 0.0F
                             ? 0.0F
                             : terrain::terrain_lab_grid_sample_x_m(fields.desc, x) / arid_half_x;
        const float nz = arid_half_z == 0.0F
                             ? 0.0F
                             : terrain::terrain_lab_grid_sample_z_m(fields.desc, y) / arid_half_z;
        residual_sum +=
            std::abs(static_cast<double>(nx) - ((fitted_slope * nz) + fitted_intercept));
    }
    const float arid_network_mean_line_residual =
        static_cast<float>(residual_sum / static_cast<double>(arid_network_channel_count));
    require(arid_network_mean_line_residual > 0.055F,
            "terrain lab arid network should not fit one regular centerline");
    std::size_t arid_direction_transition_count = 0;
    for (std::uint32_t y = 1U; y + 1U < fields.desc.height; ++y) {
        for (std::uint32_t x = 1U; x + 1U < fields.desc.width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * fields.desc.width + static_cast<std::size_t>(x);
            if (fields.channel_influence[index] <= 0.35F || fields.flow_direction[index] >= 8U) {
                continue;
            }
            bool saw_other_direction = false;
            for (int dy = -1; dy <= 1 && !saw_other_direction; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    const auto neighbor_x = static_cast<std::uint32_t>(static_cast<int>(x) + dx);
                    const auto neighbor_y = static_cast<std::uint32_t>(static_cast<int>(y) + dy);
                    const std::size_t neighbor_index = static_cast<std::size_t>(neighbor_y) *
                                                           fields.desc.width +
                                                       static_cast<std::size_t>(neighbor_x);
                    if (fields.channel_influence[neighbor_index] > 0.30F &&
                        fields.flow_direction[neighbor_index] < 8U &&
                        fields.flow_direction[neighbor_index] != fields.flow_direction[index]) {
                        saw_other_direction = true;
                        break;
                    }
                }
            }
            if (saw_other_direction) {
                ++arid_direction_transition_count;
            }
        }
    }
    require(arid_direction_transition_count > arid_network_channel_count / 12U,
            "terrain lab arid network should turn through local direction changes (transitions=" +
                std::to_string(arid_direction_transition_count) +
                ", samples=" + std::to_string(arid_network_channel_count) + ")");
    constexpr std::size_t kAridCoarseNetworkSize = 16U;
    std::array<bool, kAridCoarseNetworkSize * kAridCoarseNetworkSize> arid_coarse_network{};
    for (std::size_t index = 0; index < fields.sample_count(); ++index) {
        if (fields.channel_influence[index] <= 0.35F) {
            continue;
        }
        const auto x = static_cast<std::uint32_t>(index % fields.desc.width);
        const auto y = static_cast<std::uint32_t>(index / fields.desc.width);
        const std::size_t coarse_x = std::min(kAridCoarseNetworkSize - 1U,
                                              static_cast<std::size_t>(x) *
                                                  kAridCoarseNetworkSize / fields.desc.width);
        const std::size_t coarse_y = std::min(kAridCoarseNetworkSize - 1U,
                                              static_cast<std::size_t>(y) *
                                                  kAridCoarseNetworkSize / fields.desc.height);
        arid_coarse_network[coarse_y * kAridCoarseNetworkSize + coarse_x] = true;
    }
    std::size_t arid_coarse_terminal_count = 0;
    std::size_t arid_coarse_junction_count = 0;
    for (std::size_t y = 1U; y + 1U < kAridCoarseNetworkSize; ++y) {
        for (std::size_t x = 1U; x + 1U < kAridCoarseNetworkSize; ++x) {
            const std::size_t index = y * kAridCoarseNetworkSize + x;
            if (!arid_coarse_network[index]) {
                continue;
            }
            const std::uint32_t cardinal_neighbor_count =
                static_cast<std::uint32_t>(arid_coarse_network[index - 1U] ? 1U : 0U) +
                static_cast<std::uint32_t>(arid_coarse_network[index + 1U] ? 1U : 0U) +
                static_cast<std::uint32_t>(
                    arid_coarse_network[index - kAridCoarseNetworkSize] ? 1U : 0U) +
                static_cast<std::uint32_t>(
                    arid_coarse_network[index + kAridCoarseNetworkSize] ? 1U : 0U);
            if (cardinal_neighbor_count <= 1U) {
                ++arid_coarse_terminal_count;
            }
            if (cardinal_neighbor_count >= 3U) {
                ++arid_coarse_junction_count;
            }
        }
    }
    require(arid_coarse_terminal_count > 0U && arid_coarse_junction_count > 0U,
            "terrain lab arid network should read as a coarse canyon system (terminals=" +
                std::to_string(arid_coarse_terminal_count) +
                ", junctions=" + std::to_string(arid_coarse_junction_count) + ")");

    const terrain::TerrainLabFieldData fields_repeat = terrain::generate_terrain_lab_fields(small);
    const terrain::TerrainLabFieldSummary summary = terrain::summarize_terrain_lab_fields(fields);
    const terrain::TerrainLabFieldSummary summary_repeat =
        terrain::summarize_terrain_lab_fields(fields_repeat);
    require(summary.sample_count == summary_repeat.sample_count,
            "terrain lab summaries should be repeatable");
    require_near(summary.min_height_m, summary_repeat.min_height_m, 0.001F,
                 "terrain lab min height summary should be repeatable");
    require_near(summary.max_height_m, summary_repeat.max_height_m, 0.001F,
                 "terrain lab max height summary should be repeatable");
    require_near(summary.height_span_m, summary_repeat.height_span_m, 0.001F,
                 "terrain lab height span summary should be repeatable");
    require_near(summary.mean_height_m, summary_repeat.mean_height_m, 0.001F,
                 "terrain lab mean height summary should be repeatable");
    require_near(summary.mean_slope, summary_repeat.mean_slope, 0.001F,
                 "terrain lab mean slope summary should be repeatable");
    require_near(summary.max_flow_accumulation, summary_repeat.max_flow_accumulation, 0.001F,
                 "terrain lab flow summary should be repeatable");
    require_near(summary.mean_channel_height_m, summary_repeat.mean_channel_height_m, 0.001F,
                 "terrain lab channel height summary should be repeatable");
    require_near(summary.mean_divide_height_m, summary_repeat.mean_divide_height_m, 0.001F,
                 "terrain lab divide height summary should be repeatable");
    require_near(summary.divide_channel_height_gap_m, summary_repeat.divide_channel_height_gap_m,
                 0.001F, "terrain lab divide-channel height gap summary should be repeatable");
    require_near(summary.mean_channel_flow_accumulation,
                 summary_repeat.mean_channel_flow_accumulation, 0.001F,
                 "terrain lab channel flow summary should be repeatable");
    require_near(summary.mean_non_channel_flow_accumulation,
                 summary_repeat.mean_non_channel_flow_accumulation, 0.001F,
                 "terrain lab non-channel flow summary should be repeatable");
    require_near(summary.mean_channel_stream_power, summary_repeat.mean_channel_stream_power,
                 0.001F, "terrain lab channel stream-power summary should be repeatable");
    require_near(summary.mean_non_channel_stream_power,
                 summary_repeat.mean_non_channel_stream_power, 0.001F,
                 "terrain lab non-channel stream-power summary should be repeatable");
    require_near(summary.max_river_discharge, summary_repeat.max_river_discharge, 0.001F,
                 "terrain lab river discharge summary should be repeatable");
    require(summary.max_stream_order == summary_repeat.max_stream_order,
            "terrain lab stream-order summary should be repeatable");
    require_near(summary.max_river_width_m, summary_repeat.max_river_width_m, 0.001F,
                 "terrain lab river width summary should be repeatable");
    require_near(summary.max_valley_width_m, summary_repeat.max_valley_width_m, 0.001F,
                 "terrain lab valley width summary should be repeatable");
    require_near(summary.mean_water_presence, summary_repeat.mean_water_presence, 0.001F,
                 "terrain lab water-presence summary should be repeatable");
    require_near(summary.mean_wetness, summary_repeat.mean_wetness, 0.001F,
                 "terrain lab wetness summary should be repeatable");
    require_near(summary.mean_tree_density, summary_repeat.mean_tree_density, 0.001F,
                 "terrain lab tree summary should be repeatable");
    require_near(summary.mean_material_entropy, summary_repeat.mean_material_entropy, 0.001F,
                 "terrain lab material entropy summary should be repeatable");
    require_near(summary.mean_edge_step_m, summary_repeat.mean_edge_step_m, 0.001F,
                 "terrain lab edge step summary should be repeatable");
    require(summary.channel_sample_count == summary_repeat.channel_sample_count,
            "terrain lab channel sample count should be repeatable");
    require(summary.non_channel_sample_count == summary_repeat.non_channel_sample_count,
            "terrain lab non-channel sample count should be repeatable");
    require(summary.divide_sample_count == summary_repeat.divide_sample_count,
            "terrain lab divide sample count should be repeatable");
    require(summary.drainage_region_count == summary_repeat.drainage_region_count,
            "terrain lab drainage region summary should be repeatable");
    require(summary.sink_sample_count == summary_repeat.sink_sample_count,
            "terrain lab sink sample count should be repeatable");
    require_near(summary.sink_sample_ratio, summary_repeat.sink_sample_ratio, 0.001F,
                 "terrain lab sink sample ratio should be repeatable");
    require_near(summary.mean_divide_influence, summary_repeat.mean_divide_influence, 0.001F,
                 "terrain lab divide summary should be repeatable");
    require_near(summary.mean_channel_influence, summary_repeat.mean_channel_influence, 0.001F,
                 "terrain lab channel summary should be repeatable");
    require_near(summary.max_channel_distance_m, summary_repeat.max_channel_distance_m, 0.001F,
                 "terrain lab channel distance summary should be repeatable");
    require(summary.drainage_region_count == 1U, "terrain lab summary should count arid local basin");
    require(summary.height_span_m > 100.0F, "terrain lab summary should expose height span");
    require(summary.channel_sample_count == arid_stats.channel_count,
            "terrain lab summary should expose channel sample count");
    require(summary.non_channel_sample_count == arid_stats.non_channel_count,
            "terrain lab summary should expose non-channel sample count");
    require(summary.divide_sample_count == arid_stats.divide_count,
            "terrain lab summary should expose divide sample count");
    require_near(summary.mean_channel_height_m, arid_mean_channel_height, 0.001F,
                 "terrain lab summary should expose channel height");
    require(summary.mean_wetness < 0.30F,
            "terrain lab arid slice should stay drier than the temperate river fixture");
    require(summary.mean_water_presence == 0.0F,
            "terrain lab arid summary should report no standing water");
    require(summary.mean_tree_density < 0.01F,
            "terrain lab arid slice should keep tree density sparse");
    require(summary.mean_material_entropy > 0.05F && summary.mean_material_entropy <= 1.0F,
            "terrain lab summary should expose material mask diversity");
    require(summary.mean_edge_step_m >= 0.0F && summary.mean_edge_step_m < summary.height_span_m,
            "terrain lab summary should expose bounded edge discontinuity");
    require(summary.sink_sample_count > 0U && summary.sink_sample_count < summary.sample_count,
            "terrain lab drainage analysis should expose bounded sink samples");
    require(summary.sink_sample_ratio > 0.0F && summary.sink_sample_ratio < 0.50F,
            "terrain lab drainage analysis should avoid sink-dominated terrain");
    require(summary.mean_divide_influence > 0.0F,
            "terrain lab summary should include divide coverage");
    require(summary.mean_channel_influence > 0.0F,
            "terrain lab summary should include channel coverage");

    terrain::TerrainLabConfig river_config = small;
    river_config.slice_preset = terrain::TerrainLabSlicePreset::TemperateMountainRivers;
    const terrain::TerrainLabFieldData river_fields =
        terrain::generate_terrain_lab_fields(river_config);
    terrain::validate_terrain_lab_fields(river_fields);
    require_slice_driver_guardrails(river_fields, "terrain lab temperate river fixture");
    require(river_fields.drainage_region_count == 1U,
            "terrain lab temperate river fixture should use one local drainage region");
    require(river_fields.max_channel_distance_m > river_fields.desc.cell_size_m,
            "terrain lab temperate river fixture should track channel distance range");
    const FieldSampleStats river_stats = inspect_field_samples(river_fields);
    require(river_stats.saw_material_variation,
            "terrain lab temperate river fixture should produce varied material masks");
    require(river_stats.saw_tree_density,
            "terrain lab temperate river fixture should produce tree density fields");
    require(river_stats.saw_detail,
            "terrain lab temperate river fixture should produce detail fields");
    require(river_stats.saw_process,
            "terrain lab temperate river fixture should produce process fields");
    require(river_stats.saw_divide,
            "terrain lab temperate river fixture should produce divide fields");
    require(river_stats.saw_channel,
            "terrain lab temperate river fixture should produce flow-derived channel fields");
    require(std::all_of(river_stats.saw_drainage_region.begin(), river_stats.saw_drainage_region.end(),
                        [](bool saw) { return saw; }),
            "terrain lab temperate river fixture should cover its local drainage region");
    require(river_stats.channel_count > 16U,
            "terrain lab temperate river fixture should produce enough channel samples");
    require(river_stats.non_channel_count > 16U,
            "terrain lab temperate river fixture should produce enough non-channel samples");
    require(river_stats.divide_count > 16U,
            "terrain lab temperate river fixture should produce enough divide samples");
    const float river_mean_channel_height =
        static_cast<float>(river_stats.channel_height_sum / static_cast<double>(river_stats.channel_count));
    const float river_mean_channel_wetness =
        static_cast<float>(river_stats.channel_wetness_sum / static_cast<double>(river_stats.channel_count));
    const float river_mean_channel_deposition =
        static_cast<float>(river_stats.channel_deposition_sum / static_cast<double>(river_stats.channel_count));
    const float river_mean_channel_flow =
        static_cast<float>(river_stats.channel_flow_sum / static_cast<double>(river_stats.channel_count));
    const float river_mean_non_channel_wetness = static_cast<float>(
        river_stats.non_channel_wetness_sum / static_cast<double>(river_stats.non_channel_count));
    const float river_mean_non_channel_deposition = static_cast<float>(
        river_stats.non_channel_deposition_sum / static_cast<double>(river_stats.non_channel_count));
    const float river_mean_non_channel_flow =
        static_cast<float>(river_stats.non_channel_flow_sum / static_cast<double>(river_stats.non_channel_count));
    const float river_mean_divide_height =
        static_cast<float>(river_stats.divide_height_sum / static_cast<double>(river_stats.divide_count));
    require(river_mean_channel_wetness > river_mean_non_channel_wetness + 0.04F,
            "terrain lab temperate river channels should be wetter than non-channel terrain");
    require(river_mean_channel_deposition > river_mean_non_channel_deposition + 0.02F,
            "terrain lab temperate river channels should be more depositional than non-channel terrain");
    require(river_mean_channel_flow > river_mean_non_channel_flow,
            "terrain lab temperate river channels should carry more flow than non-channel terrain");
    require(river_mean_divide_height > river_mean_channel_height + 15.0F,
            "terrain lab temperate river divides should remain higher than channels");
    std::size_t river_driver_ridge_count = 0;
    std::size_t river_driver_channel_count = 0;
    std::size_t river_driver_non_channel_count = 0;
    std::size_t river_driver_high_relief_count = 0;
    std::size_t river_driver_low_relief_count = 0;
    double river_driver_ridge_relief_sum = 0.0;
    double river_driver_channel_relief_sum = 0.0;
    double river_driver_channel_process_sum = 0.0;
    double river_driver_non_channel_process_sum = 0.0;
    double river_driver_high_relief_height_sum = 0.0;
    double river_driver_low_relief_height_sum = 0.0;
    for (std::size_t index = 0; index < river_fields.sample_count(); ++index) {
        const float relief = river_fields.driver_relief_potential[index];
        const float process = river_fields.driver_process_potential[index];
        if (river_fields.ridge_influence[index] > 0.42F &&
            river_fields.channel_influence[index] < 0.35F) {
            river_driver_ridge_relief_sum += relief;
            ++river_driver_ridge_count;
        }
        if (river_fields.channel_influence[index] > 0.45F) {
            river_driver_channel_relief_sum += relief;
            river_driver_channel_process_sum += process;
            ++river_driver_channel_count;
        }
        if (river_fields.channel_influence[index] < 0.05F &&
            river_fields.valley_influence[index] < 0.25F) {
            river_driver_non_channel_process_sum += process;
            ++river_driver_non_channel_count;
        }
        if (relief > 0.42F) {
            river_driver_high_relief_height_sum += river_fields.height_m[index];
            ++river_driver_high_relief_count;
        }
        if (relief < 0.24F) {
            river_driver_low_relief_height_sum += river_fields.height_m[index];
            ++river_driver_low_relief_count;
        }
    }
    require(river_driver_ridge_count > 16U && river_driver_channel_count > 16U,
            "terrain lab temperate river driver should produce enough ridge and channel samples");
    require(river_driver_non_channel_count > 16U,
            "terrain lab temperate river driver should produce enough non-channel comparison samples");
    require(river_driver_high_relief_count > 16U && river_driver_low_relief_count > 16U,
            "terrain lab temperate river driver should produce high and low relief samples");
    const float river_mean_ridge_driver_relief = static_cast<float>(
        river_driver_ridge_relief_sum / static_cast<double>(river_driver_ridge_count));
    const float river_mean_channel_driver_relief = static_cast<float>(
        river_driver_channel_relief_sum / static_cast<double>(river_driver_channel_count));
    const float river_mean_channel_driver_process = static_cast<float>(
        river_driver_channel_process_sum / static_cast<double>(river_driver_channel_count));
    const float river_mean_non_channel_driver_process =
        static_cast<float>(river_driver_non_channel_process_sum /
                           static_cast<double>(river_driver_non_channel_count));
    const float river_mean_high_relief_height =
        static_cast<float>(river_driver_high_relief_height_sum /
                           static_cast<double>(river_driver_high_relief_count));
    const float river_mean_low_relief_height =
        static_cast<float>(river_driver_low_relief_height_sum /
                           static_cast<double>(river_driver_low_relief_count));
    require(river_mean_ridge_driver_relief > river_mean_channel_driver_relief + 0.04F,
            "terrain lab temperate river ridges should derive from higher relief driver values");
    require(river_mean_channel_driver_process >
                river_mean_non_channel_driver_process + 0.02F,
            "terrain lab temperate river channels should derive from higher process driver values");
    require(river_mean_high_relief_height > river_mean_low_relief_height + 20.0F,
            "terrain lab temperate river height should follow the relief driver");
    const terrain::TerrainLabFieldSummary river_summary =
        terrain::summarize_terrain_lab_fields(river_fields);
    const RiverHierarchyStats river_hierarchy_stats = inspect_river_hierarchy(river_fields);
    require(river_summary.drainage_region_count == 1U,
            "terrain lab temperate river summary should count one local drainage region");
    require(river_summary.channel_sample_count == river_stats.channel_count,
            "terrain lab temperate river summary should expose channel sample count");
    require(river_summary.non_channel_sample_count == river_stats.non_channel_count,
            "terrain lab temperate river summary should expose non-channel sample count");
    require(river_summary.divide_sample_count == river_stats.divide_count,
            "terrain lab temperate river summary should expose divide sample count");
    require_near(river_summary.mean_channel_height_m, river_mean_channel_height, 0.001F,
                 "terrain lab temperate river summary should expose channel height");
    require_near(river_summary.mean_divide_height_m, river_mean_divide_height, 0.001F,
                 "terrain lab temperate river summary should expose divide height");
    require(river_summary.divide_channel_height_gap_m > 15.0F,
            "terrain lab temperate river summary should expose divide-channel height separation");
    require(river_summary.mean_channel_flow_accumulation >
                river_summary.mean_non_channel_flow_accumulation,
            "terrain lab temperate river summary should expose channel-flow alignment");
    require(river_summary.mean_channel_stream_power >= river_summary.mean_non_channel_stream_power,
            "terrain lab temperate river summary should expose channel stream-power alignment");
    require(river_summary.max_stream_order >= 3U,
            "terrain lab temperate river summary should expose hierarchical streams");
    require(river_summary.max_river_width_m > river_fields.desc.cell_size_m * 1.4F,
            "terrain lab temperate river summary should expose widened trunk rivers");
    require(river_summary.max_valley_width_m > river_summary.max_river_width_m * 5.0F,
            "terrain lab temperate river summary should expose broad valley corridors");
    require(river_summary.mean_water_presence > 0.008F,
            "terrain lab temperate river summary should expose wet river presence (mean=" +
                std::to_string(river_summary.mean_water_presence) + ")");
    require(river_hierarchy_stats.water_sample_count > river_fields.sample_count() / 36U,
            "terrain lab temperate river should contain enough wet river samples");
    require(river_hierarchy_stats.wet_component_count > 0U,
            "terrain lab temperate river should produce wet river components");
    require(river_hierarchy_stats.largest_wet_component_count >
                river_hierarchy_stats.wet_component_sample_count / 4U,
            "terrain lab temperate river should be dominated by connected wet paths (largest=" +
                std::to_string(river_hierarchy_stats.largest_wet_component_count) +
                ", wet=" + std::to_string(river_hierarchy_stats.wet_component_sample_count) +
                ", components=" + std::to_string(river_hierarchy_stats.wet_component_count) + ")");
    require(std::max(river_hierarchy_stats.largest_wet_component_x_span,
                     river_hierarchy_stats.largest_wet_component_y_span) >
                std::min(river_fields.desc.width, river_fields.desc.height) / 3U,
            "terrain lab temperate river should expose a long visible trunk (x_span=" +
                std::to_string(river_hierarchy_stats.largest_wet_component_x_span) +
                ", y_span=" + std::to_string(river_hierarchy_stats.largest_wet_component_y_span) + ")");
    require(river_hierarchy_stats.low_order_count > 16U && river_hierarchy_stats.high_order_count > 0U,
            "terrain lab temperate river should expose low and high stream orders");
    const float river_low_order_width =
        mean_or_zero(river_hierarchy_stats.low_order_width_sum, river_hierarchy_stats.low_order_count);
    const float river_high_order_width =
        mean_or_zero(river_hierarchy_stats.high_order_width_sum, river_hierarchy_stats.high_order_count);
    const float river_low_order_discharge =
        mean_or_zero(river_hierarchy_stats.low_order_discharge_sum, river_hierarchy_stats.low_order_count);
    const float river_high_order_discharge =
        mean_or_zero(river_hierarchy_stats.high_order_discharge_sum, river_hierarchy_stats.high_order_count);
    require(river_high_order_width > river_low_order_width + river_fields.desc.cell_size_m * 0.35F,
            "terrain lab temperate river trunks should be wider than tributaries");
    require(river_high_order_discharge > river_low_order_discharge * 3.0F,
            "terrain lab temperate river trunks should carry more derived discharge");
    require(river_summary.sink_sample_count > 0U &&
                river_summary.sink_sample_count < river_summary.sample_count,
            "terrain lab temperate river drainage analysis should expose bounded sink samples");
    require(river_summary.sink_sample_ratio > 0.0F && river_summary.sink_sample_ratio < 0.50F,
            "terrain lab temperate river drainage analysis should avoid sink-dominated terrain");

    terrain::TerrainLabConfig dunes_config = small;
    dunes_config.slice_preset = terrain::TerrainLabSlicePreset::DesertDunes;
    const terrain::TerrainLabFieldData dunes_fields =
        terrain::generate_terrain_lab_fields(dunes_config);
    terrain::validate_terrain_lab_fields(dunes_fields);
    require_slice_driver_guardrails(dunes_fields, "terrain lab dunes sentinel");
    require(dunes_fields.drainage_region_count == 1U,
            "terrain lab dunes sentinel should use one diagnostic basin");
    require(dunes_fields.max_channel_distance_m > dunes_fields.desc.cell_size_m,
            "terrain lab dunes sentinel should track interdune distance range");
    const FieldSampleStats dunes_stats = inspect_field_samples(dunes_fields);
    const terrain::TerrainLabFieldSummary dunes_summary =
        terrain::summarize_terrain_lab_fields(dunes_fields);
    require(dunes_stats.saw_material_variation,
            "terrain lab dunes sentinel should produce varied sand material masks");
    require(dunes_stats.saw_detail, "terrain lab dunes sentinel should produce ripple detail");
    require(dunes_stats.saw_process,
            "terrain lab dunes sentinel should produce wind-process fields");
    require(dunes_stats.saw_divide, "terrain lab dunes sentinel should expose dune crests");
    require(dunes_stats.saw_channel,
            "terrain lab dunes sentinel should expose weak interdune diagnostics");
    require(dunes_stats.channel_count == 0U,
            "terrain lab dunes sentinel should not promote interdunes to hydrology channels");
    require(dunes_stats.non_channel_count > 16U,
            "terrain lab dunes sentinel should produce enough non-channel samples");
    require(dunes_stats.shrub_count > 8U,
            "terrain lab dunes sentinel should produce sparse shrub samples");
    std::size_t dune_driver_selected_count = 0;
    std::size_t dune_driver_ridge_count = 0;
    std::size_t dune_driver_valley_count = 0;
    std::size_t dune_driver_high_relief_count = 0;
    std::size_t dune_driver_low_relief_count = 0;
    double dune_driver_selection_sum = 0.0;
    double dune_driver_ridge_relief_sum = 0.0;
    double dune_driver_valley_relief_sum = 0.0;
    double dune_driver_high_relief_height_sum = 0.0;
    double dune_driver_low_relief_height_sum = 0.0;
    for (std::size_t index = 0; index < dunes_fields.sample_count(); ++index) {
        const float selection = dunes_fields.driver_selection_mask[index];
        const float relief = dunes_fields.driver_relief_potential[index];
        dune_driver_selection_sum += selection;
        if (selection > 0.20F) {
            ++dune_driver_selected_count;
        }
        if (dunes_fields.ridge_influence[index] > 0.22F) {
            dune_driver_ridge_relief_sum += relief;
            ++dune_driver_ridge_count;
        }
        if (dunes_fields.valley_influence[index] > 0.45F) {
            dune_driver_valley_relief_sum += relief;
            ++dune_driver_valley_count;
        }
        if (selection > 0.20F && relief > 0.56F) {
            dune_driver_high_relief_height_sum += dunes_fields.height_m[index];
            ++dune_driver_high_relief_count;
        }
        if (selection > 0.20F && relief < 0.30F) {
            dune_driver_low_relief_height_sum += dunes_fields.height_m[index];
            ++dune_driver_low_relief_count;
        }
    }
    const double inv_dunes_count = 1.0 / static_cast<double>(dunes_fields.sample_count());
    const float dunes_mean_sand = static_cast<float>(dunes_stats.sand_sum * inv_dunes_count);
    const float dunes_mean_forest = static_cast<float>(dunes_stats.forest_sum * inv_dunes_count);
    const float dunes_mean_snow = static_cast<float>(dunes_stats.snow_sum * inv_dunes_count);
    const float dunes_mean_shrub = static_cast<float>(dunes_stats.shrub_sum * inv_dunes_count);
    const float dunes_mean_driver_selection =
        static_cast<float>(dune_driver_selection_sum * inv_dunes_count);
    require(dune_driver_selected_count == dunes_fields.sample_count(),
            "terrain lab dunes driver should use the whole local patch as the sand field");
    require_near(dunes_mean_driver_selection, 1.0F, 0.001F,
                 "terrain lab dunes driver selection should not add a local footprint mask");
    require(dune_driver_ridge_count > 16U && dune_driver_valley_count > 16U,
            "terrain lab dunes driver should produce enough ridge and valley samples");
    require(dune_driver_high_relief_count > 16U && dune_driver_low_relief_count > 16U,
            "terrain lab dunes driver should produce high and low relief samples");
    const float dunes_mean_ridge_driver_relief = static_cast<float>(
        dune_driver_ridge_relief_sum / static_cast<double>(dune_driver_ridge_count));
    const float dunes_mean_valley_driver_relief = static_cast<float>(
        dune_driver_valley_relief_sum / static_cast<double>(dune_driver_valley_count));
    const float dunes_mean_high_relief_height = static_cast<float>(
        dune_driver_high_relief_height_sum / static_cast<double>(dune_driver_high_relief_count));
    const float dunes_mean_low_relief_height = static_cast<float>(
        dune_driver_low_relief_height_sum / static_cast<double>(dune_driver_low_relief_count));
    require(dunes_mean_ridge_driver_relief > dunes_mean_valley_driver_relief + 0.08F,
            "terrain lab dune ridges should derive from higher relief driver values");
    require(dunes_mean_high_relief_height > dunes_mean_low_relief_height + 20.0F,
            "terrain lab dune height should follow the relief driver");
    require(dunes_mean_sand > 0.72F,
            "terrain lab dunes sentinel should be dominated by sand material");
    require(dunes_mean_forest < 0.001F,
            "terrain lab dunes sentinel should keep forest material absent");
    require(dunes_mean_snow < 0.001F,
            "terrain lab dunes sentinel should keep snow material absent");
    require(dunes_summary.mean_wetness < 0.08F,
            "terrain lab dunes sentinel should stay hydro-light");
    require(dunes_summary.mean_water_presence == 0.0F,
            "terrain lab dunes sentinel should not expose standing water");
    require(dunes_summary.mean_channel_influence > 0.0F &&
                dunes_summary.mean_channel_influence < 0.18F,
            "terrain lab dunes sentinel should keep channel influence diagnostic only");
    require(dunes_summary.mean_tree_density == 0.0F,
            "terrain lab dunes sentinel should not produce trees");
    require(dunes_mean_shrub > 0.001F && dunes_mean_shrub < 0.040F,
            "terrain lab dunes sentinel should keep shrubs sparse");
    require(dunes_summary.sink_sample_count > 0U &&
                dunes_summary.sink_sample_count < dunes_summary.sample_count,
            "terrain lab dunes drainage analysis should expose bounded sink samples");
    require(dunes_summary.sink_sample_ratio > 0.0F && dunes_summary.sink_sample_ratio < 0.50F,
            "terrain lab dunes drainage analysis should avoid sink-dominated terrain");

    terrain::TerrainLabConfig fastnoise_dunes_config = dunes_config;
    fastnoise_dunes_config.noise_source = terrain::TerrainLabNoiseSource::FastNoiseLite;
    const terrain::TerrainLabFieldData fastnoise_dunes_fields =
        terrain::generate_terrain_lab_fields(fastnoise_dunes_config);
    terrain::validate_terrain_lab_fields(fastnoise_dunes_fields);
    require_slice_driver_guardrails(fastnoise_dunes_fields,
                                    "terrain lab FastNoiseLite dunes sentinel");
    double fastnoise_dune_driver_delta_sum = 0.0;
    for (std::size_t index = 0; index < dunes_fields.sample_count(); ++index) {
        fastnoise_dune_driver_delta_sum += std::abs(
            static_cast<double>(fastnoise_dunes_fields.driver_relief_potential[index]) -
            static_cast<double>(dunes_fields.driver_relief_potential[index]));
    }
    const float fastnoise_dune_mean_driver_delta = static_cast<float>(
        fastnoise_dune_driver_delta_sum / static_cast<double>(dunes_fields.sample_count()));
    require(fastnoise_dune_mean_driver_delta > 0.01F,
            "terrain lab FastNoiseLite dunes source should differ from legacy value noise");

    terrain::TerrainLabConfig glacial_config = small;
    glacial_config.slice_preset = terrain::TerrainLabSlicePreset::AlpineGlacialValley;
    const terrain::TerrainLabFieldData glacial_fields =
        terrain::generate_terrain_lab_fields(glacial_config);
    terrain::validate_terrain_lab_fields(glacial_fields);
    require_slice_driver_guardrails(glacial_fields, "terrain lab glacial sentinel");
    require(glacial_fields.drainage_region_count == 1U,
            "terrain lab glacial sentinel should use one diagnostic basin");
    require(glacial_fields.max_channel_distance_m > glacial_fields.desc.cell_size_m,
            "terrain lab glacial sentinel should track trunk-valley distance range");
    const FieldSampleStats glacial_stats = inspect_field_samples(glacial_fields);
    const terrain::TerrainLabFieldSummary glacial_summary =
        terrain::summarize_terrain_lab_fields(glacial_fields);
    require(glacial_stats.saw_material_variation,
            "terrain lab glacial sentinel should produce varied material masks");
    require(glacial_stats.saw_detail, "terrain lab glacial sentinel should produce terrain detail");
    require(glacial_stats.saw_process,
            "terrain lab glacial sentinel should produce glacial process fields");
    require(glacial_stats.saw_divide, "terrain lab glacial sentinel should expose high divides");
    require(glacial_stats.saw_channel,
            "terrain lab glacial sentinel should expose trunk-valley diagnostics");
    require(glacial_stats.channel_count > 16U,
            "terrain lab glacial sentinel should produce enough trunk-valley samples");
    require(glacial_stats.non_channel_count > 16U,
            "terrain lab glacial sentinel should produce enough non-channel samples: " +
                std::to_string(glacial_stats.non_channel_count));
    require(glacial_stats.divide_count > 16U,
            "terrain lab glacial sentinel should produce enough divide samples");
    require(glacial_stats.ridge_count > 16U,
            "terrain lab glacial sentinel should produce enough ridge samples");
    const double inv_glacial_count = 1.0 / static_cast<double>(glacial_fields.sample_count());
    const float glacial_rock_scree_snow = static_cast<float>(
        (glacial_stats.rock_sum + glacial_stats.scree_sum + glacial_stats.snow_sum) *
        inv_glacial_count);
    const float glacial_mean_forest =
        static_cast<float>(glacial_stats.forest_sum * inv_glacial_count);
    const float glacial_mean_sand = static_cast<float>(glacial_stats.sand_sum * inv_glacial_count);
    const float glacial_mean_channel_height = static_cast<float>(
        glacial_stats.channel_height_sum / static_cast<double>(glacial_stats.channel_count));
    const float glacial_mean_divide_height = static_cast<float>(
        glacial_stats.divide_height_sum / static_cast<double>(glacial_stats.divide_count));
    const float glacial_mean_ridge_height = static_cast<float>(
        glacial_stats.ridge_height_sum / static_cast<double>(glacial_stats.ridge_count));
    const float glacial_mean_channel_deposition = static_cast<float>(
        glacial_stats.channel_deposition_sum / static_cast<double>(glacial_stats.channel_count));
    const float glacial_mean_non_channel_deposition =
        static_cast<float>(glacial_stats.non_channel_deposition_sum /
                           static_cast<double>(glacial_stats.non_channel_count));
    std::size_t glacial_driver_ridge_count = 0;
    std::size_t glacial_driver_valley_count = 0;
    std::size_t glacial_driver_high_relief_count = 0;
    std::size_t glacial_driver_low_relief_count = 0;
    std::size_t glacial_driver_non_valley_count = 0;
    std::size_t glacial_axis_channel_count = 0;
    std::size_t glacial_axis_ridge_count = 0;
    double glacial_driver_ridge_relief_sum = 0.0;
    double glacial_driver_valley_relief_sum = 0.0;
    double glacial_driver_valley_process_sum = 0.0;
    double glacial_driver_non_valley_process_sum = 0.0;
    double glacial_driver_high_relief_height_sum = 0.0;
    double glacial_driver_low_relief_height_sum = 0.0;
    double glacial_channel_abs_x_sum = 0.0;
    double glacial_ridge_abs_x_sum = 0.0;
    std::array<bool, 5> glacial_channel_y_bins{};
    const float glacial_half_x =
        static_cast<float>(glacial_fields.desc.width - 1U) * glacial_fields.desc.cell_size_m *
        0.5F;
    for (std::size_t index = 0; index < glacial_fields.sample_count(); ++index) {
        const float relief = glacial_fields.driver_relief_potential[index];
        const float process = glacial_fields.driver_process_potential[index];
        const auto x = static_cast<std::uint32_t>(index % glacial_fields.desc.width);
        const auto y = static_cast<std::uint32_t>(index / glacial_fields.desc.width);
        const float nx = glacial_half_x == 0.0F
                             ? 0.0F
                             : terrain::terrain_lab_grid_sample_x_m(glacial_fields.desc, x) /
                                   glacial_half_x;
        if (glacial_fields.ridge_influence[index] > 0.45F &&
            glacial_fields.channel_influence[index] < 0.35F) {
            glacial_driver_ridge_relief_sum += relief;
            ++glacial_driver_ridge_count;
            glacial_ridge_abs_x_sum += std::abs(nx);
            ++glacial_axis_ridge_count;
        }
        if (glacial_fields.channel_influence[index] > 0.45F ||
            glacial_fields.valley_influence[index] > 0.58F) {
            glacial_driver_valley_relief_sum += relief;
            glacial_driver_valley_process_sum += process;
            ++glacial_driver_valley_count;
        }
        if (glacial_fields.channel_influence[index] > 0.45F) {
            glacial_channel_abs_x_sum += std::abs(nx);
            ++glacial_axis_channel_count;
            const std::size_t y_bin =
                std::min<std::size_t>(glacial_channel_y_bins.size() - 1U,
                                      static_cast<std::size_t>((y * glacial_channel_y_bins.size()) /
                                                               glacial_fields.desc.height));
            glacial_channel_y_bins[y_bin] = true;
        }
        if (glacial_fields.channel_influence[index] < 0.05F &&
            glacial_fields.valley_influence[index] < 0.25F) {
            glacial_driver_non_valley_process_sum += process;
            ++glacial_driver_non_valley_count;
        }
        if (relief > 0.65F) {
            glacial_driver_high_relief_height_sum += glacial_fields.height_m[index];
            ++glacial_driver_high_relief_count;
        }
        if (relief < 0.50F) {
            glacial_driver_low_relief_height_sum += glacial_fields.height_m[index];
            ++glacial_driver_low_relief_count;
        }
    }
    require(glacial_driver_ridge_count > 16U && glacial_driver_valley_count > 16U,
            "terrain lab glacial driver should produce enough ridge and valley samples");
    require(glacial_driver_high_relief_count > 16U && glacial_driver_low_relief_count > 16U,
            "terrain lab glacial driver should produce high and low relief samples");
    require(glacial_driver_non_valley_count > 16U,
            "terrain lab glacial driver should produce non-valley comparison samples");
    require(glacial_axis_channel_count > 16U && glacial_axis_ridge_count > 16U,
            "terrain lab glacial source should expose enough valley-axis and wall samples");
    const auto glacial_channel_y_bin_count =
        static_cast<std::size_t>(std::count(glacial_channel_y_bins.begin(),
                                            glacial_channel_y_bins.end(), true));
    require(glacial_channel_y_bin_count >= 4U,
            "terrain lab glacial source should expose a longitudinal valley path");
    const float glacial_mean_ridge_driver_relief = static_cast<float>(
        glacial_driver_ridge_relief_sum / static_cast<double>(glacial_driver_ridge_count));
    const float glacial_mean_valley_driver_relief = static_cast<float>(
        glacial_driver_valley_relief_sum / static_cast<double>(glacial_driver_valley_count));
    const float glacial_mean_valley_driver_process = static_cast<float>(
        glacial_driver_valley_process_sum / static_cast<double>(glacial_driver_valley_count));
    const float glacial_mean_non_valley_driver_process =
        static_cast<float>(glacial_driver_non_valley_process_sum /
                           static_cast<double>(glacial_driver_non_valley_count));
    const float glacial_mean_high_relief_height =
        static_cast<float>(glacial_driver_high_relief_height_sum /
                           static_cast<double>(glacial_driver_high_relief_count));
    const float glacial_mean_low_relief_height =
        static_cast<float>(glacial_driver_low_relief_height_sum /
                           static_cast<double>(glacial_driver_low_relief_count));
    const float glacial_mean_channel_abs_x =
        static_cast<float>(glacial_channel_abs_x_sum /
                           static_cast<double>(glacial_axis_channel_count));
    const float glacial_mean_ridge_abs_x =
        static_cast<float>(glacial_ridge_abs_x_sum /
                           static_cast<double>(glacial_axis_ridge_count));
    require(glacial_mean_ridge_driver_relief > glacial_mean_valley_driver_relief + 0.05F,
            "terrain lab glacial ridges should derive from higher relief driver values");
    require(glacial_mean_valley_driver_process > glacial_mean_non_valley_driver_process + 0.02F,
            "terrain lab glacial valley process should derive from higher process driver values");
    require(glacial_mean_high_relief_height > glacial_mean_low_relief_height + 20.0F,
            "terrain lab glacial height should follow the relief driver");
    require(glacial_mean_ridge_abs_x > glacial_mean_channel_abs_x + 0.10F,
            "terrain lab glacial source should frame a valley with mountain-wall contrast");
    require(glacial_rock_scree_snow > 0.62F,
            "terrain lab glacial sentinel should be dominated by rock, scree, and snow/ice");
    require(glacial_mean_forest < 0.04F,
            "terrain lab glacial sentinel should keep forest material limited");
    require(glacial_mean_sand < 0.001F,
            "terrain lab glacial sentinel should not use sand material");
    require(glacial_mean_divide_height > glacial_mean_channel_height + 20.0F,
            "terrain lab glacial sentinel should keep divides above the trunk valley");
    require(glacial_mean_ridge_height > glacial_mean_channel_height + 55.0F,
            "terrain lab glacial sentinel should keep alpine ridges well above the trunk valley");
    require(glacial_mean_channel_deposition > glacial_mean_non_channel_deposition,
            "terrain lab glacial sentinel should concentrate moraine/deposition in valleys");
    require(glacial_summary.mean_wetness > 0.05F && glacial_summary.mean_wetness < 0.35F,
            "terrain lab glacial sentinel should expose moderate meltwater wetness");
    require(glacial_summary.max_stream_order >= 2U,
            "terrain lab glacial sentinel should expose routed alpine channels");
    require(glacial_summary.max_river_width_m > glacial_fields.desc.cell_size_m * 0.75F,
            "terrain lab glacial sentinel should expose channel width diagnostics");
    require(glacial_summary.mean_water_presence > 0.001F &&
                glacial_summary.mean_water_presence < 0.03F,
            "terrain lab glacial sentinel should expose limited alpine water presence");
    require(glacial_summary.mean_tree_density < 0.05F,
            "terrain lab glacial sentinel should keep tree density limited");
    require(glacial_summary.mean_channel_flow_accumulation >
                glacial_summary.mean_non_channel_flow_accumulation,
            "terrain lab glacial sentinel should keep trunk valleys aligned with flow diagnostics "
            "(channel=" +
                std::to_string(glacial_summary.mean_channel_flow_accumulation) +
                ", non_channel=" +
                std::to_string(glacial_summary.mean_non_channel_flow_accumulation) + ")");
    require(glacial_summary.sink_sample_count > 0U &&
                glacial_summary.sink_sample_count < glacial_summary.sample_count,
            "terrain lab glacial drainage analysis should expose bounded sink samples");
    require(glacial_summary.sink_sample_ratio > 0.0F && glacial_summary.sink_sample_ratio < 0.50F,
            "terrain lab glacial drainage analysis should avoid sink-dominated terrain");

    terrain::TerrainLabConfig mountain_config = small;
    mountain_config.slice_preset = terrain::TerrainLabSlicePreset::MountainRidgesPeaks;
    const terrain::TerrainLabFieldData mountain_fields =
        terrain::generate_terrain_lab_fields(mountain_config);
    terrain::validate_terrain_lab_fields(mountain_fields);
    require_slice_driver_guardrails(mountain_fields, "terrain lab mountain ridge sentinel");
    require(mountain_fields.drainage_region_count == 1U,
            "terrain lab mountain ridge sentinel should use one diagnostic basin");
    require(mountain_fields.max_channel_distance_m > mountain_fields.desc.cell_size_m,
            "terrain lab mountain ridge sentinel should track diagnostic channel distance");
    const FieldSampleStats mountain_stats = inspect_field_samples(mountain_fields);
    const terrain::TerrainLabFieldSummary mountain_summary =
        terrain::summarize_terrain_lab_fields(mountain_fields);
    require(mountain_stats.saw_material_variation,
            "terrain lab mountain ridge sentinel should produce varied material masks");
    require(mountain_stats.saw_detail,
            "terrain lab mountain ridge sentinel should produce terrain detail");
    require(mountain_stats.saw_process,
            "terrain lab mountain ridge sentinel should produce scree/cliff process fields");
    require(mountain_stats.saw_divide,
            "terrain lab mountain ridge sentinel should expose divides");
    require(mountain_stats.saw_channel,
            "terrain lab mountain ridge sentinel should keep diagnostic valley channels");
    require(mountain_stats.ridge_count > 16U,
            "terrain lab mountain ridge sentinel should produce enough ridge samples");
    require(mountain_stats.divide_count > 16U,
            "terrain lab mountain ridge sentinel should produce enough divide samples");
    require(mountain_stats.non_channel_count > 16U,
            "terrain lab mountain ridge sentinel should produce enough non-channel samples");
    const double inv_mountain_count = 1.0 / static_cast<double>(mountain_fields.sample_count());
    const float mountain_rock_scree_snow = static_cast<float>(
        (mountain_stats.rock_sum + mountain_stats.scree_sum + mountain_stats.snow_sum) *
        inv_mountain_count);
    const float mountain_mean_forest =
        static_cast<float>(mountain_stats.forest_sum * inv_mountain_count);
    const float mountain_mean_sand =
        static_cast<float>(mountain_stats.sand_sum * inv_mountain_count);
    const float mountain_mean_ridge_height = static_cast<float>(
        mountain_stats.ridge_height_sum / static_cast<double>(mountain_stats.ridge_count));
    const float mountain_mean_non_channel_height =
        static_cast<float>(mountain_stats.non_channel_height_sum /
                           static_cast<double>(mountain_stats.non_channel_count));
    std::size_t mountain_driver_ridge_count = 0;
    std::size_t mountain_driver_valley_count = 0;
    std::size_t mountain_driver_high_relief_count = 0;
    std::size_t mountain_driver_low_relief_count = 0;
    double mountain_driver_ridge_relief_sum = 0.0;
    double mountain_driver_valley_relief_sum = 0.0;
    double mountain_driver_high_relief_height_sum = 0.0;
    double mountain_driver_low_relief_height_sum = 0.0;
    std::array<bool, 5> mountain_ridge_x_bins{};
    std::array<bool, 5> mountain_ridge_y_bins{};
    std::array<std::size_t, 4> mountain_high_relief_quadrants{};
    double mountain_relief_weight_sum = 0.0;
    double mountain_relief_weighted_x_sum = 0.0;
    double mountain_relief_weighted_y_sum = 0.0;
    const float mountain_half_x =
        static_cast<float>(mountain_fields.desc.width - 1U) * mountain_fields.desc.cell_size_m *
        0.5F;
    const float mountain_half_y =
        static_cast<float>(mountain_fields.desc.height - 1U) * mountain_fields.desc.cell_size_m *
        0.5F;
    for (std::size_t index = 0; index < mountain_fields.sample_count(); ++index) {
        const float relief = mountain_fields.driver_relief_potential[index];
        const auto x = static_cast<std::uint32_t>(index % mountain_fields.desc.width);
        const auto y = static_cast<std::uint32_t>(index / mountain_fields.desc.width);
        const float nx = mountain_half_x == 0.0F
                             ? 0.0F
                             : terrain::terrain_lab_grid_sample_x_m(mountain_fields.desc, x) /
                                   mountain_half_x;
        const float ny = mountain_half_y == 0.0F
                             ? 0.0F
                             : terrain::terrain_lab_grid_sample_z_m(mountain_fields.desc, y) /
                                   mountain_half_y;
        if (mountain_fields.ridge_influence[index] > 0.48F &&
            mountain_fields.channel_influence[index] < 0.32F) {
            mountain_driver_ridge_relief_sum += relief;
            ++mountain_driver_ridge_count;
            const std::size_t x_bin =
                std::min<std::size_t>(mountain_ridge_x_bins.size() - 1U,
                                      static_cast<std::size_t>((x * mountain_ridge_x_bins.size()) /
                                                               mountain_fields.desc.width));
            const std::size_t y_bin =
                std::min<std::size_t>(mountain_ridge_y_bins.size() - 1U,
                                      static_cast<std::size_t>((y * mountain_ridge_y_bins.size()) /
                                                               mountain_fields.desc.height));
            mountain_ridge_x_bins[x_bin] = true;
            mountain_ridge_y_bins[y_bin] = true;
        }
        if (mountain_fields.channel_influence[index] > 0.20F ||
            mountain_fields.valley_influence[index] > 0.20F) {
            mountain_driver_valley_relief_sum += relief;
            ++mountain_driver_valley_count;
        }
        if (relief > 0.66F) {
            mountain_driver_high_relief_height_sum += mountain_fields.height_m[index];
            ++mountain_driver_high_relief_count;
        }
        if (relief < 0.48F) {
            mountain_driver_low_relief_height_sum += mountain_fields.height_m[index];
            ++mountain_driver_low_relief_count;
        }
        if (relief > 0.62F) {
            const std::size_t quadrant =
                (x >= mountain_fields.desc.width / 2U ? 1U : 0U) +
                (y >= mountain_fields.desc.height / 2U ? 2U : 0U);
            ++mountain_high_relief_quadrants[quadrant];
        }
        const double relief_weight = std::max(static_cast<double>(relief) - 0.54, 0.0);
        mountain_relief_weight_sum += relief_weight;
        mountain_relief_weighted_x_sum += static_cast<double>(nx) * relief_weight;
        mountain_relief_weighted_y_sum += static_cast<double>(ny) * relief_weight;
    }
    const auto mountain_ridge_x_bin_count =
        static_cast<std::size_t>(std::count(mountain_ridge_x_bins.begin(),
                                            mountain_ridge_x_bins.end(), true));
    const auto mountain_ridge_y_bin_count =
        static_cast<std::size_t>(std::count(mountain_ridge_y_bins.begin(),
                                            mountain_ridge_y_bins.end(), true));
    std::size_t mountain_high_relief_count = 0;
    std::size_t mountain_high_relief_max_quadrant_count = 0;
    for (const std::size_t quadrant_count : mountain_high_relief_quadrants) {
        mountain_high_relief_count += quadrant_count;
        mountain_high_relief_max_quadrant_count =
            std::max(mountain_high_relief_max_quadrant_count, quadrant_count);
    }
    std::size_t mountain_high_relief_quadrant_count = 0;
    for (const std::size_t quadrant_count : mountain_high_relief_quadrants) {
        if (quadrant_count * 10U > mountain_high_relief_count) {
            ++mountain_high_relief_quadrant_count;
        }
    }
    require(mountain_driver_ridge_count > 16U && mountain_driver_valley_count > 16U,
            "terrain lab mountain ridge driver should produce enough ridge and valley samples");
    require(mountain_driver_high_relief_count > 16U &&
                mountain_driver_low_relief_count > 16U,
            "terrain lab mountain ridge driver should produce high and low relief samples");
    require(mountain_ridge_x_bin_count >= 3U && mountain_ridge_y_bin_count >= 3U,
            "terrain lab mountain ridge driver should distribute ridges across the patch");
    require(mountain_high_relief_count > 32U,
            "terrain lab mountain source should produce enough high-relief samples");
    require(mountain_high_relief_quadrant_count >= 3U,
            "terrain lab mountain source should not collapse high relief into one quadrant");
    require(mountain_high_relief_max_quadrant_count * 5U < mountain_high_relief_count * 3U,
            "terrain lab mountain source should avoid a dominant high-relief quadrant");
    require(mountain_relief_weight_sum > 0.001,
            "terrain lab mountain source should have measurable relief weight");
    const float mountain_relief_center_x =
        static_cast<float>(mountain_relief_weighted_x_sum / mountain_relief_weight_sum);
    const float mountain_relief_center_y =
        static_cast<float>(mountain_relief_weighted_y_sum / mountain_relief_weight_sum);
    require(std::abs(mountain_relief_center_x) < 0.45F &&
                std::abs(mountain_relief_center_y) < 0.45F,
            "terrain lab mountain source should keep relief center away from patch corners");
    const float mountain_mean_ridge_driver_relief = static_cast<float>(
        mountain_driver_ridge_relief_sum / static_cast<double>(mountain_driver_ridge_count));
    const float mountain_mean_valley_driver_relief = static_cast<float>(
        mountain_driver_valley_relief_sum / static_cast<double>(mountain_driver_valley_count));
    const float mountain_mean_high_relief_height =
        static_cast<float>(mountain_driver_high_relief_height_sum /
                           static_cast<double>(mountain_driver_high_relief_count));
    const float mountain_mean_low_relief_height =
        static_cast<float>(mountain_driver_low_relief_height_sum /
                           static_cast<double>(mountain_driver_low_relief_count));
    require(mountain_mean_ridge_driver_relief > mountain_mean_valley_driver_relief + 0.06F,
            "terrain lab mountain ridges should derive from higher relief driver values");
    require(mountain_mean_high_relief_height > mountain_mean_low_relief_height + 60.0F,
            "terrain lab mountain ridge height should follow the relief driver");
    require(mountain_mean_ridge_height > mountain_mean_non_channel_height + 45.0F,
            "terrain lab mountain ridges should stand above non-ridge terrain");
    require(mountain_rock_scree_snow > 0.70F,
            "terrain lab mountain ridge sentinel should be dominated by rock, scree, and snow");
    require(mountain_mean_forest < 0.001F,
            "terrain lab mountain ridge sentinel should not use forest material");
    require(mountain_mean_sand < 0.001F,
            "terrain lab mountain ridge sentinel should not use sand material");
    require(mountain_summary.mean_wetness < 0.08F,
            "terrain lab mountain ridge sentinel should stay hydro-light");
    require(mountain_summary.mean_water_presence < 0.012F,
            "terrain lab mountain ridge sentinel should expose only diagnostic water presence");
    require(mountain_summary.mean_tree_density == 0.0F,
            "terrain lab mountain ridge sentinel should not produce trees");
    require(mountain_summary.sink_sample_count > 0U &&
                mountain_summary.sink_sample_count < mountain_summary.sample_count,
            "terrain lab mountain ridge drainage analysis should expose bounded sink samples");
    require(mountain_summary.sink_sample_ratio > 0.0F &&
                mountain_summary.sink_sample_ratio < 0.50F,
            "terrain lab mountain ridge drainage analysis should avoid sink-dominated terrain");
    require(glacial_summary.mean_wetness > mountain_summary.mean_wetness + 0.015F,
            "terrain lab glacial valley should stay wetter than the mountain ridge sentinel");
    require(glacial_summary.mean_channel_influence > mountain_summary.mean_channel_influence,
            "terrain lab glacial valley should expose stronger valley/channel influence");
    require(mountain_summary.height_span_m > glacial_summary.height_span_m + 25.0F,
            "terrain lab mountain ridge sentinel should keep larger relief than glacial valley");
    require(mountain_rock_scree_snow > glacial_rock_scree_snow + 0.02F,
            "terrain lab mountain ridge sentinel should be colder/rockier than glacial valley");

    terrain::TerrainLabConfig other_seed = small;
    other_seed.seed += 1U;
    const terrain::TerrainLabFieldSummary other_summary =
        terrain::summarize_terrain_lab_fields(terrain::generate_terrain_lab_fields(other_seed));
    require(std::abs(other_summary.mean_height_m - summary.mean_height_m) > 0.001F ||
                std::abs(other_summary.mean_wetness - summary.mean_wetness) > 0.00001F,
            "terrain lab seed should influence generated fields");

    require_noise_off_driver_guardrails(small, "terrain lab arid slice", 100.0F);
    require_noise_off_driver_guardrails(river_config, "terrain lab temperate river fixture",
                                        100.0F);
    require_noise_off_driver_guardrails(dunes_config, "terrain lab dunes sentinel", 25.0F);
    require_noise_off_driver_guardrails(glacial_config, "terrain lab glacial sentinel", 100.0F);
    require_noise_off_driver_guardrails(mountain_config, "terrain lab mountain ridge sentinel",
                                        100.0F);

    const terrain::TerrainLabMeshData mesh = terrain::make_terrain_lab_mesh(fields);
    const std::size_t terrain_index_count = static_cast<std::size_t>(fields.desc.width - 1U) *
                                            static_cast<std::size_t>(fields.desc.height - 1U) * 6U;
    require(mesh.terrain_vertex_count == fields.sample_count(),
            "terrain lab mesh should track one heightfield vertex per field sample");
    require(mesh.terrain_index_count == terrain_index_count,
            "terrain lab mesh should track two terrain triangles per field cell");
    require(mesh.vertices.size() > fields.sample_count(),
            "terrain lab mesh should include arid proxy dressing vertices");
    require(mesh.indices.size() > terrain_index_count,
            "terrain lab mesh should include arid proxy dressing indices");
    require(mesh.proxy_vertex_count > 0U && mesh.proxy_index_count > 0U,
            "terrain lab mesh should expose proxy dressing counts");
    require(terrain::terrain_lab_triangle_count(mesh) >
                (fields.desc.width - 1U) * (fields.desc.height - 1U) * 2U,
            "terrain lab triangle count should include proxy dressing");
    const terrain::TerrainLabVertex& first_vertex = mesh.vertices.front();
    require_near(first_vertex.position.x, -half_extent, 0.001F,
                 "terrain lab mesh should place first column relative to center origin");
    require_near(first_vertex.position.z, -half_extent, 0.001F,
                 "terrain lab mesh should place first row relative to center origin");
    require_near(first_vertex.position.y, fields.height_m.front(), 0.001F,
                 "terrain lab mesh should use field height as vertex elevation");
    require_near(first_vertex.terrain.x, fields.height_m.front(), 0.001F,
                 "terrain lab mesh should pack height");
    require_near(first_vertex.terrain.y, fields.slope.front(), 0.001F,
                 "terrain lab mesh should pack slope");
    require_near(first_vertex.terrain.z, fields.curvature.front(), 0.001F,
                 "terrain lab mesh should pack curvature");
    require_near(first_vertex.contributions.x, fields.structure_height_m.front(), 0.001F,
                 "terrain lab mesh should pack structure contribution");
    require_near(first_vertex.contributions.y, fields.process_delta_m.front(), 0.001F,
                 "terrain lab mesh should pack process contribution");
    require_near(first_vertex.contributions.z, fields.detail_height_m.front(), 0.001F,
                 "terrain lab mesh should pack detail contribution");
    require_near(first_vertex.contributions.w,
                 fields.structure_height_m.front() + fields.process_delta_m.front(), 0.001F,
                 "terrain lab mesh should pack noise-off height");
    require_near(first_vertex.hydrology.x, static_cast<float>(fields.flow_direction.front()),
                 0.001F, "terrain lab mesh should pack flow direction");
    require_near(first_vertex.hydrology.y, fields.flow_accumulation.front(), 0.001F,
                 "terrain lab mesh should pack flow accumulation");
    require_near(first_vertex.hydrology.z, fields.stream_power.front(), 0.001F,
                 "terrain lab mesh should pack stream power");
    require_near(first_vertex.hydrology.w, fields.wetness.front(), 0.001F,
                 "terrain lab mesh should pack wetness");
    require_near(first_vertex.material_a.x, fields.material_masks.front().rock, 0.001F,
                 "terrain lab mesh should pack rock material");
    require_near(first_vertex.material_a.y, fields.material_masks.front().soil, 0.001F,
                 "terrain lab mesh should pack soil material");
    require_near(first_vertex.material_a.z, fields.material_masks.front().scree, 0.001F,
                 "terrain lab mesh should pack scree material");
    require_near(first_vertex.material_a.w, fields.material_masks.front().meadow, 0.001F,
                 "terrain lab mesh should pack meadow material");
    require_near(first_vertex.material_b.x, fields.material_masks.front().forest, 0.001F,
                 "terrain lab mesh should pack forest material");
    require_near(first_vertex.material_b.y, fields.material_masks.front().snow, 0.001F,
                 "terrain lab mesh should pack snow material");
    require_near(first_vertex.material_b.z, fields.deposition.front(), 0.001F,
                 "terrain lab mesh should pack deposition");
    require_near(first_vertex.material_b.w, fields.material_masks.front().sand, 0.001F,
                 "terrain lab mesh should pack sand");
    require_near(first_vertex.vegetation.x, fields.grass_density.front(), 0.001F,
                 "terrain lab mesh should pack grass density");
    require_near(first_vertex.vegetation.y, fields.shrub_density.front(), 0.001F,
                 "terrain lab mesh should pack shrub density");
    require_near(first_vertex.vegetation.z, fields.tree_density.front(), 0.001F,
                 "terrain lab mesh should pack tree density");
    require_near(first_vertex.vegetation.w, fields.canopy_height_m.front(), 0.001F,
                 "terrain lab mesh should pack canopy height");
    require_near(first_vertex.influences.x, fields.ridge_influence.front(), 0.001F,
                 "terrain lab mesh should pack ridge influence");
    require_near(first_vertex.influences.y, fields.basin_influence.front(), 0.001F,
                 "terrain lab mesh should pack basin influence");
    require_near(first_vertex.influences.z, fields.divide_influence.front(), 0.001F,
                 "terrain lab mesh should pack divide influence");
    require_near(first_vertex.influences.w, fields.channel_influence.front(), 0.001F,
                 "terrain lab mesh should pack channel influence");
    const float drainage_region_denominator =
        fields.drainage_region_count <= 1U ? 1.0F : static_cast<float>(fields.drainage_region_count - 1U);
    require_near(first_vertex.feature_tags.x,
                 static_cast<float>(fields.drainage_region_id.front()) / drainage_region_denominator, 0.001F,
                 "terrain lab mesh should pack normalized drainage region id");
    require_near(first_vertex.feature_tags.y,
                 fields.channel_distance_m.front() / fields.max_channel_distance_m, 0.001F,
                 "terrain lab mesh should pack normalized channel distance");
    require_near(first_vertex.feature_tags.z, static_cast<float>(fields.drainage_region_id.front()),
                 0.001F, "terrain lab mesh should pack raw drainage region id");
    require_near(first_vertex.feature_tags.w, static_cast<float>(fields.drainage_region_count), 0.001F,
                 "terrain lab mesh should pack drainage region count");
    require_near(first_vertex.drivers.x, fields.driver_base_potential.front(), 0.001F,
                 "terrain lab mesh should pack driver base potential");
    require_near(first_vertex.drivers.y, fields.driver_relief_potential.front(), 0.001F,
                 "terrain lab mesh should pack driver relief potential");
    require_near(first_vertex.drivers.z, fields.driver_process_potential.front(), 0.001F,
                 "terrain lab mesh should pack driver process potential");
    require_near(first_vertex.drivers.w, fields.driver_selection_mask.front(), 0.001F,
                 "terrain lab mesh should pack driver selection mask");
    const float stream_order_denominator =
        fields.max_stream_order <= 1U ? 1.0F : static_cast<float>(fields.max_stream_order);
    require_near(first_vertex.river.x, fields.river_discharge.front(), 0.001F,
                 "terrain lab mesh should pack river discharge");
    require_near(first_vertex.river.y,
                 static_cast<float>(fields.stream_order.front()) / stream_order_denominator,
                 0.001F, "terrain lab mesh should pack normalized stream order");
    require_near(first_vertex.river.z, fields.river_width_m.front(), 0.001F,
                 "terrain lab mesh should pack river width");
    require_near(first_vertex.river.w, fields.water_presence.front(), 0.001F,
                 "terrain lab mesh should pack water presence");

    const std::size_t center_index = fields.index(fields.desc.width / 2U, fields.desc.height / 2U);
    require_near(mesh.vertices[center_index].position.x, 0.0F, 0.001F,
                 "terrain lab mesh center vertex should land on origin X");
    require_near(mesh.vertices[center_index].position.z, 0.0F, 0.001F,
                 "terrain lab mesh center vertex should land on origin Z");
    for (const terrain::TerrainLabVertex& vertex : mesh.vertices) {
        const float normal_length =
            std::sqrt((vertex.normal.x * vertex.normal.x) + (vertex.normal.y * vertex.normal.y) +
                      (vertex.normal.z * vertex.normal.z));
        require_near(normal_length, 1.0F, 0.001F, "terrain lab mesh normals should be normalized");
        require(vertex.terrain.w >= 0.0F && vertex.terrain.w <= 1.0F,
                "terrain lab mesh should pack normalized height");
    }
    for (const std::uint32_t index : mesh.indices) {
        require(index < mesh.vertices.size(), "terrain lab mesh indices should address vertices");
    }

    return 0;
}
