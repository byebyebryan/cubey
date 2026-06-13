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

float material_sum(const cubey::projects::terrain_lab::TerrainLabMaterialMask& mask) {
    return mask.rock + mask.soil + mask.scree + mask.meadow + mask.forest + mask.snow;
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

void require_shader_debug_constant(
    const std::string& shader_source,
    cubey::projects::terrain_lab::TerrainLabDebugView view,
    const char* shader_suffix) {
    const std::string expected = "const uint TERRAIN_LAB_VIEW_" + std::string(shader_suffix) +
                                 " = " +
                                 std::to_string(static_cast<std::uint32_t>(view)) + "u;";
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
    require(defaults.slice_preset == terrain::TerrainLabSlicePreset::TemperateMountainWatershed,
            "terrain lab should default to the watershed slice");
    require(defaults.debug_view == terrain::TerrainLabDebugView::Final,
            "terrain lab should default to final debug view");
    terrain::validate_terrain_lab_config(defaults);

    require(terrain::terrain_lab_slice_preset_from_name("") ==
                terrain::TerrainLabSlicePreset::TemperateMountainWatershed,
            "empty terrain lab slice preset should use watershed");
    require(terrain::terrain_lab_slice_preset_from_name("temperate-mountain-watershed") ==
                terrain::TerrainLabSlicePreset::TemperateMountainWatershed,
            "terrain lab slice preset should parse canonical name");
    require(terrain::terrain_lab_slice_preset_from_name("temperate_mountain_watershed") ==
                terrain::TerrainLabSlicePreset::TemperateMountainWatershed,
            "terrain lab slice preset should accept underscore alias");

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
    require(terrain::terrain_lab_debug_view_from_name("watershed") ==
                terrain::TerrainLabDebugView::Watershed,
            "terrain lab watershed debug view should parse");
    require(terrain::terrain_lab_debug_view_from_name("channel") ==
                terrain::TerrainLabDebugView::Channel,
            "terrain lab channel debug view should parse");
    require(terrain::terrain_lab_debug_view_from_name("divide") ==
                terrain::TerrainLabDebugView::Divide,
            "terrain lab divide debug view should parse");
    require(terrain::next_terrain_lab_debug_view(terrain::TerrainLabDebugView::Detail) ==
                terrain::TerrainLabDebugView::Slope,
            "terrain lab debug view cycle should enter geometry views after detail");
    require(terrain::next_terrain_lab_debug_view(terrain::TerrainLabDebugView::NoiseOff) ==
                terrain::TerrainLabDebugView::FeatureGraph,
            "terrain lab debug view cycle should enter watershed diagnostics after noise-off");
    require(terrain::next_terrain_lab_debug_view(terrain::TerrainLabDebugView::Divide) ==
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
                                  terrain::TerrainLabDebugView::Watershed, "WATERSHED");
    require_shader_debug_constant(terrain_lab_fragment_shader,
                                  terrain::TerrainLabDebugView::Channel, "CHANNEL");
    require_shader_debug_constant(terrain_lab_fragment_shader,
                                  terrain::TerrainLabDebugView::Divide, "DIVIDE");

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

    cubey::RunConfig run_config;
    run_config.grid.width = 65;
    run_config.grid.height = 33;
    run_config.debug_view = "wetness";
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
    small.grid_width = 65U;
    small.grid_height = 65U;
    small.cell_size_m = 64.0F;
    const terrain::TerrainLabFieldData fields = terrain::generate_terrain_lab_fields(small);
    terrain::validate_terrain_lab_fields(fields);
    const float half_extent =
        static_cast<float>(fields.desc.width - 1U) * fields.desc.cell_size_m * 0.5F;
    require(fields.sample_count() == 65U * 65U,
            "terrain lab fields should match grid dimensions");
    require_near(terrain::terrain_lab_grid_sample_x_m(fields.desc, 0U), -half_extent, 0.001F,
                 "terrain lab grid should be centered on X");
    require_near(terrain::terrain_lab_grid_sample_z_m(fields.desc, 0U), -half_extent, 0.001F,
                 "terrain lab grid should be centered on Z");
    require_near(terrain::terrain_lab_grid_sample_x_m(fields.desc, fields.desc.width / 2U),
                 0.0F, 0.001F, "terrain lab center sample should land on origin X");
    require_near(terrain::terrain_lab_grid_sample_z_m(fields.desc, fields.desc.height / 2U),
                 0.0F, 0.001F, "terrain lab center sample should land on origin Z");

    require(fields.height_m.size() == fields.sample_count(),
            "terrain lab height field size mismatch");
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
    require(fields.watershed_id.size() == fields.sample_count(),
            "terrain lab watershed field size mismatch");
    require(fields.divide_influence.size() == fields.sample_count(),
            "terrain lab divide field size mismatch");
    require(fields.channel_influence.size() == fields.sample_count(),
            "terrain lab channel field size mismatch");
    require(fields.channel_distance_m.size() == fields.sample_count(),
            "terrain lab channel distance field size mismatch");
    require(fields.watershed_count == 4U, "terrain lab watershed graph should expose four basins");
    require(fields.max_channel_distance_m > fields.desc.cell_size_m,
            "terrain lab watershed graph should track channel distance range");

    require(fields.max_height_m - fields.min_height_m > 100.0F,
            "terrain lab fields should produce non-flat terrain");
    require(fields.max_slope > 0.0F, "terrain lab fields should produce nonzero slopes");
    require(fields.max_abs_curvature > 0.0F,
            "terrain lab fields should produce nonzero curvature");
    require(fields.max_flow_accumulation > 1.0F,
            "terrain lab fields should accumulate drainage");
    require(fields.max_stream_power >= 0.0F,
            "terrain lab stream power should stay nonnegative");
    require(fields.max_wetness > 0.0F && fields.max_wetness <= 1.0F,
            "terrain lab wetness should be normalized");
    require(fields.max_deposition > 0.0F && fields.max_deposition <= 1.0F,
            "terrain lab deposition should be normalized");

    bool saw_material_variation = false;
    bool saw_tree_density = false;
    bool saw_detail = false;
    bool saw_process = false;
    bool saw_divide = false;
    bool saw_channel = false;
    std::array<bool, 4> saw_watershed{};
    double channel_height_sum = 0.0;
    double channel_wetness_sum = 0.0;
    double channel_deposition_sum = 0.0;
    double channel_flow_sum = 0.0;
    double non_channel_wetness_sum = 0.0;
    double non_channel_deposition_sum = 0.0;
    double non_channel_flow_sum = 0.0;
    double divide_height_sum = 0.0;
    std::size_t channel_count = 0;
    std::size_t non_channel_count = 0;
    std::size_t divide_count = 0;
    for (std::size_t index = 0; index < fields.sample_count(); ++index) {
        require(std::isfinite(fields.height_m[index]), "terrain lab height should be finite");
        require(std::isfinite(fields.structure_height_m[index]),
                "terrain lab structure should be finite");
        require(std::isfinite(fields.process_delta_m[index]),
                "terrain lab process should be finite");
        require(std::isfinite(fields.detail_height_m[index]), "terrain lab detail should be finite");
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
        require(fields.watershed_id[index] < fields.watershed_count,
                "terrain lab watershed id should be valid");
        require(fields.divide_influence[index] >= 0.0F && fields.divide_influence[index] <= 1.0F,
                "terrain lab divide influence should be normalized");
        require(fields.channel_influence[index] >= 0.0F &&
                    fields.channel_influence[index] <= 1.0F,
                "terrain lab channel influence should be normalized");
        require(fields.channel_distance_m[index] >= 0.0F,
                "terrain lab channel distance should be nonnegative");
        require_near(material_sum(fields.material_masks[index]), 1.0F, 0.001F,
                     "terrain lab material masks should sum to one");
        saw_material_variation =
            saw_material_variation || fields.material_masks[index].rock > 0.05F ||
            fields.material_masks[index].forest > 0.05F || fields.material_masks[index].snow > 0.05F;
        saw_tree_density = saw_tree_density || fields.tree_density[index] > 0.01F;
        saw_detail = saw_detail || std::abs(fields.detail_height_m[index]) > 0.001F;
        saw_process = saw_process || std::abs(fields.process_delta_m[index]) > 0.001F;
        saw_divide = saw_divide || fields.divide_influence[index] > 0.2F;
        saw_channel = saw_channel || fields.channel_influence[index] > 0.2F;
        saw_watershed[fields.watershed_id[index]] = true;
        if (fields.channel_influence[index] > 0.45F) {
            channel_height_sum += fields.height_m[index];
            channel_wetness_sum += fields.wetness[index];
            channel_deposition_sum += fields.deposition[index];
            channel_flow_sum += fields.flow_accumulation[index];
            ++channel_count;
        }
        if (fields.channel_influence[index] < 0.05F && fields.divide_influence[index] < 0.25F) {
            non_channel_wetness_sum += fields.wetness[index];
            non_channel_deposition_sum += fields.deposition[index];
            non_channel_flow_sum += fields.flow_accumulation[index];
            ++non_channel_count;
        }
        if (fields.divide_influence[index] > 0.55F && fields.channel_influence[index] < 0.20F) {
            divide_height_sum += fields.height_m[index];
            ++divide_count;
        }
    }
    require(saw_material_variation, "terrain lab should produce varied material masks");
    require(saw_tree_density, "terrain lab should produce tree density fields");
    require(saw_detail, "terrain lab should produce detail contribution fields");
    require(saw_process, "terrain lab should produce process contribution fields");
    require(saw_divide, "terrain lab should produce divide influence fields");
    require(saw_channel, "terrain lab should produce channel influence fields");
    require(std::all_of(saw_watershed.begin(), saw_watershed.end(), [](bool saw) { return saw; }),
            "terrain lab should rasterize every watershed basin");
    require(channel_count > 16U, "terrain lab should produce enough channel samples to compare");
    require(non_channel_count > 16U,
            "terrain lab should produce enough non-channel samples to compare");
    require(divide_count > 16U, "terrain lab should produce enough divide samples to compare");
    const float mean_channel_height =
        static_cast<float>(channel_height_sum / static_cast<double>(channel_count));
    const float mean_channel_wetness =
        static_cast<float>(channel_wetness_sum / static_cast<double>(channel_count));
    const float mean_channel_deposition =
        static_cast<float>(channel_deposition_sum / static_cast<double>(channel_count));
    const float mean_channel_flow =
        static_cast<float>(channel_flow_sum / static_cast<double>(channel_count));
    const float mean_non_channel_wetness =
        static_cast<float>(non_channel_wetness_sum / static_cast<double>(non_channel_count));
    const float mean_non_channel_deposition =
        static_cast<float>(non_channel_deposition_sum / static_cast<double>(non_channel_count));
    const float mean_non_channel_flow =
        static_cast<float>(non_channel_flow_sum / static_cast<double>(non_channel_count));
    const float mean_divide_height =
        static_cast<float>(divide_height_sum / static_cast<double>(divide_count));
    require(mean_channel_wetness > mean_non_channel_wetness + 0.04F,
            "terrain lab channels should be wetter than non-channel terrain");
    require(mean_channel_deposition > mean_non_channel_deposition + 0.02F,
            "terrain lab channels should be more depositional than non-channel terrain");
    require(mean_channel_flow > mean_non_channel_flow,
            "terrain lab channels should carry more accumulated flow than non-channel terrain");
    require(mean_divide_height > mean_channel_height + 15.0F,
            "terrain lab divides should remain higher than channels");

    const terrain::TerrainLabFieldData fields_repeat =
        terrain::generate_terrain_lab_fields(small);
    const terrain::TerrainLabFieldSummary summary =
        terrain::summarize_terrain_lab_fields(fields);
    const terrain::TerrainLabFieldSummary summary_repeat =
        terrain::summarize_terrain_lab_fields(fields_repeat);
    require(summary.sample_count == summary_repeat.sample_count,
            "terrain lab summaries should be repeatable");
    require_near(summary.min_height_m, summary_repeat.min_height_m, 0.001F,
                 "terrain lab min height summary should be repeatable");
    require_near(summary.max_height_m, summary_repeat.max_height_m, 0.001F,
                 "terrain lab max height summary should be repeatable");
    require_near(summary.mean_height_m, summary_repeat.mean_height_m, 0.001F,
                 "terrain lab mean height summary should be repeatable");
    require_near(summary.mean_slope, summary_repeat.mean_slope, 0.001F,
                 "terrain lab mean slope summary should be repeatable");
    require_near(summary.max_flow_accumulation, summary_repeat.max_flow_accumulation, 0.001F,
                 "terrain lab flow summary should be repeatable");
    require_near(summary.mean_wetness, summary_repeat.mean_wetness, 0.001F,
                 "terrain lab wetness summary should be repeatable");
    require_near(summary.mean_tree_density, summary_repeat.mean_tree_density, 0.001F,
                 "terrain lab tree summary should be repeatable");
    require(summary.watershed_count == summary_repeat.watershed_count,
            "terrain lab watershed summary should be repeatable");
    require_near(summary.mean_divide_influence, summary_repeat.mean_divide_influence, 0.001F,
                 "terrain lab divide summary should be repeatable");
    require_near(summary.mean_channel_influence, summary_repeat.mean_channel_influence, 0.001F,
                 "terrain lab channel summary should be repeatable");
    require_near(summary.max_channel_distance_m, summary_repeat.max_channel_distance_m, 0.001F,
                 "terrain lab channel distance summary should be repeatable");
    require(summary.watershed_count == 4U, "terrain lab summary should count watershed basins");
    require(summary.mean_divide_influence > 0.0F,
            "terrain lab summary should include divide coverage");
    require(summary.mean_channel_influence > 0.0F,
            "terrain lab summary should include channel coverage");

    terrain::TerrainLabConfig other_seed = small;
    other_seed.seed += 1U;
    const terrain::TerrainLabFieldSummary other_summary =
        terrain::summarize_terrain_lab_fields(terrain::generate_terrain_lab_fields(other_seed));
    require(std::abs(other_summary.mean_height_m - summary.mean_height_m) > 0.001F ||
                std::abs(other_summary.mean_wetness - summary.mean_wetness) > 0.00001F,
            "terrain lab seed should influence generated fields");

    terrain::TerrainLabConfig no_detail = small;
    no_detail.detail_strength = 0.0F;
    const terrain::TerrainLabFieldData no_detail_fields =
        terrain::generate_terrain_lab_fields(no_detail);
    float min_no_detail_structure = no_detail_fields.structure_height_m.front();
    float max_no_detail_structure = no_detail_fields.structure_height_m.front();
    bool all_detail_zero = true;
    for (std::size_t index = 0; index < no_detail_fields.sample_count(); ++index) {
        min_no_detail_structure =
            std::min(min_no_detail_structure, no_detail_fields.structure_height_m[index]);
        max_no_detail_structure =
            std::max(max_no_detail_structure, no_detail_fields.structure_height_m[index]);
        all_detail_zero =
            all_detail_zero && std::abs(no_detail_fields.detail_height_m[index]) <= 0.001F;
        require_near(no_detail_fields.height_m[index],
                     no_detail_fields.structure_height_m[index] +
                         no_detail_fields.process_delta_m[index],
                     0.001F,
                     "terrain lab noise-off height should equal structure plus process");
    }
    require(all_detail_zero, "terrain lab detail can be disabled independently");
    require(max_no_detail_structure - min_no_detail_structure > 100.0F,
            "terrain lab noise-off structure should remain non-flat");
    require(no_detail_fields.max_flow_accumulation > 1.0F,
            "terrain lab noise-off fields should still have drainage structure");

    const terrain::TerrainLabMeshData mesh = terrain::make_terrain_lab_mesh(fields);
    require(mesh.vertices.size() == fields.sample_count(),
            "terrain lab mesh should include one vertex per field sample");
    require(mesh.indices.size() == (fields.desc.width - 1U) * (fields.desc.height - 1U) * 6U,
            "terrain lab mesh should include two triangles per field cell");
    require(terrain::terrain_lab_triangle_count(mesh) ==
                (fields.desc.width - 1U) * (fields.desc.height - 1U) * 2U,
            "terrain lab triangle count should match index topology");
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
    const float watershed_denominator =
        fields.watershed_count <= 1U ? 1.0F : static_cast<float>(fields.watershed_count - 1U);
    require_near(first_vertex.feature_tags.x,
                 static_cast<float>(fields.watershed_id.front()) / watershed_denominator, 0.001F,
                 "terrain lab mesh should pack normalized watershed id");
    require_near(first_vertex.feature_tags.y,
                 fields.channel_distance_m.front() / fields.max_channel_distance_m, 0.001F,
                 "terrain lab mesh should pack normalized channel distance");
    require_near(first_vertex.feature_tags.z, static_cast<float>(fields.watershed_id.front()),
                 0.001F, "terrain lab mesh should pack raw watershed id");
    require_near(first_vertex.feature_tags.w, static_cast<float>(fields.watershed_count), 0.001F,
                 "terrain lab mesh should pack watershed count");

    const std::size_t center_index = fields.index(fields.desc.width / 2U, fields.desc.height / 2U);
    require_near(mesh.vertices[center_index].position.x, 0.0F, 0.001F,
                 "terrain lab mesh center vertex should land on origin X");
    require_near(mesh.vertices[center_index].position.z, 0.0F, 0.001F,
                 "terrain lab mesh center vertex should land on origin Z");
    for (const terrain::TerrainLabVertex& vertex : mesh.vertices) {
        const float normal_length =
            std::sqrt((vertex.normal.x * vertex.normal.x) +
                      (vertex.normal.y * vertex.normal.y) +
                      (vertex.normal.z * vertex.normal.z));
        require_near(normal_length, 1.0F, 0.001F,
                     "terrain lab mesh normals should be normalized");
        require(vertex.terrain.w >= 0.0F && vertex.terrain.w <= 1.0F,
                "terrain lab mesh should pack normalized height");
    }
    for (const std::uint32_t index : mesh.indices) {
        require(index < mesh.vertices.size(), "terrain lab mesh indices should address vertices");
    }

    return 0;
}
