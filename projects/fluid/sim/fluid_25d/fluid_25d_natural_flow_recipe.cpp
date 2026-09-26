#include "fluid_25d_natural_flow_recipe.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace cubey::projects::fluid::fluid_25d {
namespace {

using Json = nlohmann::json;

void require_exact_fields(const Json& object, std::initializer_list<std::string_view> fields,
                          std::string_view context) {
    if (!object.is_object() || object.size() != fields.size()) {
        throw std::runtime_error("natural-flow recipe " + std::string(context) +
                                 " must be an object with the exact required fields");
    }
    for (const std::string_view field : fields) {
        if (!object.contains(std::string(field))) {
            throw std::runtime_error("natural-flow recipe " + std::string(context) +
                                     " is missing field " + std::string(field));
        }
    }
}

[[nodiscard]] std::uint32_t unsigned_u32(const Json& value, std::string_view label) {
    std::uint64_t parsed = 0U;
    if (value.is_number_unsigned()) {
        parsed = value.get<std::uint64_t>();
    } else if (value.is_number_integer()) {
        const std::int64_t signed_value = value.get<std::int64_t>();
        if (signed_value < 0) {
            throw std::runtime_error("natural-flow recipe " + std::string(label) +
                                     " must be a nonnegative integer");
        }
        parsed = static_cast<std::uint64_t>(signed_value);
    } else {
        throw std::runtime_error("natural-flow recipe " + std::string(label) +
                                 " must be an integer");
    }
    if (parsed > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("natural-flow recipe " + std::string(label) +
                                 " is outside the supported coordinate range");
    }
    return static_cast<std::uint32_t>(parsed);
}

[[nodiscard]] std::int32_t signed_i32(const Json& value, std::string_view label) {
    if (!value.is_number_integer()) {
        throw std::runtime_error("natural-flow recipe " + std::string(label) +
                                 " must be an integer");
    }
    std::int64_t parsed = 0;
    if (value.is_number_unsigned()) {
        const std::uint64_t unsigned_value = value.get<std::uint64_t>();
        if (unsigned_value > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
            throw std::runtime_error("natural-flow recipe " + std::string(label) +
                                     " is outside the supported tangent range");
        }
        parsed = static_cast<std::int64_t>(unsigned_value);
    } else {
        parsed = value.get<std::int64_t>();
    }
    if (parsed < std::numeric_limits<std::int32_t>::min() ||
        parsed > std::numeric_limits<std::int32_t>::max()) {
        throw std::runtime_error("natural-flow recipe " + std::string(label) +
                                 " is outside the supported tangent range");
    }
    return static_cast<std::int32_t>(parsed);
}

[[nodiscard]] std::array<std::uint32_t, 2U> xz_pair(const Json& value, std::string_view label) {
    if (!value.is_array() || value.size() != 2U) {
        throw std::runtime_error("natural-flow recipe " + std::string(label) +
                                 " must be a two-integer [x,z] pair");
    }
    return {unsigned_u32(value[0], label), unsigned_u32(value[1], label)};
}

[[nodiscard]] std::string required_string(const Json& value, std::string_view label) {
    if (!value.is_string()) {
        throw std::runtime_error("natural-flow recipe " + std::string(label) + " must be a string");
    }
    return value.get<std::string>();
}

[[nodiscard]] double finite_number(const Json& value, std::string_view label) {
    if (!value.is_number()) {
        throw std::runtime_error("natural-flow recipe " + std::string(label) + " must be numeric");
    }
    const double result = value.get<double>();
    if (!std::isfinite(result)) {
        throw std::runtime_error("natural-flow recipe " + std::string(label) + " must be finite");
    }
    return result;
}

[[nodiscard]] bool is_sha256(std::string_view value) {
    return value.size() == 64U && std::all_of(value.begin(), value.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

[[nodiscard]] Fluid25DNaturalFlowEdge edge_from_name(std::string_view name) {
    if (name == "west") {
        return Fluid25DNaturalFlowEdge::West;
    }
    if (name == "east") {
        return Fluid25DNaturalFlowEdge::East;
    }
    if (name == "north") {
        return Fluid25DNaturalFlowEdge::North;
    }
    if (name == "south") {
        return Fluid25DNaturalFlowEdge::South;
    }
    throw std::runtime_error(
        "natural-flow recipe expected_outlet.edge must be west, east, north, or south");
}

[[nodiscard]] bool valid_gauge_name(std::string_view value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '-';
    });
}

[[nodiscard]] std::array<std::array<std::uint32_t, 2U>, 5U>
expected_source_footprint(std::array<std::uint32_t, 2U> center) {
    return {{{center[0], center[1]},
             {center[0], center[1] - 1U},
             {center[0], center[1] + 1U},
             {center[0] + 1U, center[1]},
             {center[0] - 1U, center[1]}}};
}

void validate_recipe_shape(const Fluid25DNaturalFlowRecipe& recipe, std::uint32_t grid_width,
                           std::uint32_t grid_height, float cell_size_m) {
    if (recipe.candidate_id.empty()) {
        throw std::runtime_error("natural-flow recipe candidate_id must be nonempty");
    }
    if (!is_sha256(recipe.elevation_sha256) || !is_sha256(recipe.transformed_crop_sha256)) {
        throw std::runtime_error(
            "natural-flow recipe raw and transformed-crop hashes must be lowercase SHA-256");
    }
    if (!std::isfinite(recipe.cell_size_m) || recipe.cell_size_m != kFluid25DNaturalFlowCellSizeM ||
        !std::isfinite(cell_size_m) || cell_size_m != kFluid25DNaturalFlowCellSizeM) {
        throw std::runtime_error("natural-flow recipe requires exact native 30 metre cells");
    }
    if (recipe.width < 5U || recipe.height < 5U || recipe.width != grid_width ||
        recipe.height != grid_height) {
        throw std::runtime_error(
            "natural-flow recipe crop dimensions must exactly match the configured grid and be at "
            "least 5x5");
    }

    const std::uint32_t center_x = recipe.source_center_xz[0];
    const std::uint32_t center_z = recipe.source_center_xz[1];
    if (center_x < 2U || center_z < 2U || center_x >= recipe.width - 2U ||
        center_z >= recipe.height - 2U) {
        throw std::runtime_error("natural-flow recipe source footprint must keep all five source "
                                 "cells interior to the crop");
    }
    std::set<std::array<std::uint32_t, 2U>> listed_sources;
    for (const auto& cell : recipe.source_cells_xz) {
        if (cell[0] == 0U || cell[1] == 0U || cell[0] >= recipe.width - 1U ||
            cell[1] >= recipe.height - 1U) {
            throw std::runtime_error(
                "natural-flow recipe source footprint must be interior to the crop");
        }
        if (!listed_sources.insert(cell).second) {
            throw std::runtime_error("natural-flow recipe source footprint contains duplicates");
        }
    }
    const auto expected_sources = expected_source_footprint(recipe.source_center_xz);
    const std::set<std::array<std::uint32_t, 2U>> expected_set(expected_sources.begin(),
                                                               expected_sources.end());
    if (listed_sources != expected_set) {
        throw std::runtime_error("natural-flow recipe source cells must be exactly the center plus "
                                 "four cardinal neighbors");
    }

    if (recipe.has_expected_outlet) {
        const std::uint32_t min_x = recipe.expected_outlet_min_xz[0];
        const std::uint32_t min_z = recipe.expected_outlet_min_xz[1];
        const std::uint32_t max_x = recipe.expected_outlet_max_xz[0];
        const std::uint32_t max_z = recipe.expected_outlet_max_xz[1];
        if (min_x > max_x || (min_x == max_x && min_z > max_z) || max_x >= recipe.width ||
            max_z >= recipe.height) {
            throw std::runtime_error(
                "natural-flow recipe expected outlet window must be sorted and inbounds");
        }
        bool valid_window = false;
        switch (recipe.expected_outlet_edge) {
        case Fluid25DNaturalFlowEdge::West:
            valid_window = min_x == 0U && max_x == 0U && min_z > 0U && max_z + 1U < recipe.height;
            break;
        case Fluid25DNaturalFlowEdge::East:
            valid_window = min_x + 1U == recipe.width && max_x + 1U == recipe.width && min_z > 0U &&
                           max_z + 1U < recipe.height;
            break;
        case Fluid25DNaturalFlowEdge::North:
            valid_window = min_z == 0U && max_z == 0U && min_x > 0U && max_x + 1U < recipe.width;
            break;
        case Fluid25DNaturalFlowEdge::South:
            valid_window = min_z + 1U == recipe.height && max_z + 1U == recipe.height &&
                           min_x > 0U && max_x + 1U < recipe.width;
            break;
        }
        if (!valid_window) {
            throw std::runtime_error("natural-flow recipe expected outlet window must lie on one "
                                     "edge and exclude corners");
        }
    }

    std::set<std::string> gauge_names;
    if (recipe.has_expected_outlet && recipe.gauges.empty()) {
        throw std::runtime_error("natural-flow recipe must contain at least one gauge");
    }
    for (const Fluid25DNaturalFlowGauge& gauge : recipe.gauges) {
        if (!valid_gauge_name(gauge.name) || !gauge_names.insert(gauge.name).second) {
            throw std::runtime_error(
                "natural-flow recipe gauge names must be unique nonempty ASCII identifiers");
        }
        if (gauge.x_cell >= recipe.width || gauge.z_cell >= recipe.height) {
            throw std::runtime_error("natural-flow recipe gauge center is outside the crop");
        }
        const bool cardinal_tangent =
            ((gauge.tangent_dx == -1 || gauge.tangent_dx == 1) && gauge.tangent_dz == 0) ||
            (gauge.tangent_dx == 0 && (gauge.tangent_dz == -1 || gauge.tangent_dz == 1));
        if (!cardinal_tangent) {
            throw std::runtime_error(
                "natural-flow recipe gauge tangent must be one nonzero cardinal direction");
        }
        if (!std::isfinite(gauge.distance_m) || gauge.distance_m < 0.0 ||
            gauge.half_span_cells == 0U ||
            gauge.half_span_cells > kFluid25DNaturalFlowMaximumGaugeHalfSpanCells) {
            throw std::runtime_error("natural-flow recipe gauge distance must be "
                                     "finite/nonnegative and half-span in 1..10");
        }
        for (std::int32_t offset = -static_cast<std::int32_t>(gauge.half_span_cells);
             offset <= static_cast<std::int32_t>(gauge.half_span_cells); ++offset) {
            const std::int64_t x = static_cast<std::int64_t>(gauge.x_cell) -
                                   static_cast<std::int64_t>(gauge.tangent_dz) * offset;
            const std::int64_t z = static_cast<std::int64_t>(gauge.z_cell) +
                                   static_cast<std::int64_t>(gauge.tangent_dx) * offset;
            if (x < 0 || z < 0 || x >= recipe.width || z >= recipe.height) {
                throw std::runtime_error(
                    "natural-flow recipe gauge transect extends outside the crop");
            }
        }
    }
}

} // namespace

Fluid25DNaturalFlowRecipe parse_fluid_25d_natural_flow_recipe(std::string_view json_text) {
    Json document;
    try {
        document = Json::parse(json_text);
    } catch (const std::exception& error) {
        throw std::runtime_error("natural-flow recipe JSON is malformed: " +
                                 std::string(error.what()));
    }
    if (!document.is_object() || !document.contains("schema")) {
        throw std::runtime_error("natural terrain recipe must contain a schema");
    }
    const std::string schema = required_string(document.at("schema"), "schema");
    const bool hillside = schema == kFluid25DHillsideFlowRecipeSchema;
    if (hillside) {
        require_exact_fields(document,
                             {"schema", "candidate_id", "elevation_sha256",
                              "transformed_crop_sha256", "crop_xzwh", "cell_size_m",
                              "source_center_cell_xz", "source_cells_xz", "gauges"},
                             "root");
    } else {
        require_exact_fields(document,
                             {"schema", "candidate_id", "elevation_sha256",
                              "transformed_crop_sha256", "crop_xzwh", "cell_size_m",
                              "source_center_cell_xz", "source_cells_xz", "expected_outlet",
                              "gauges"},
                             "root");
        if (schema != kFluid25DNaturalFlowRecipeSchema) {
            throw std::runtime_error("natural-flow recipe schema is not supported");
        }
    }

    Fluid25DNaturalFlowRecipe recipe;
    recipe.has_expected_outlet = !hillside;
    recipe.candidate_id = required_string(document.at("candidate_id"), "candidate_id");
    recipe.elevation_sha256 = required_string(document.at("elevation_sha256"), "elevation_sha256");
    recipe.transformed_crop_sha256 =
        required_string(document.at("transformed_crop_sha256"), "transformed_crop_sha256");
    const Json& crop = document.at("crop_xzwh");
    if (!crop.is_array() || crop.size() != 4U) {
        throw std::runtime_error("natural-flow recipe crop_xzwh must be four integers");
    }
    recipe.crop_x = unsigned_u32(crop[0], "crop_xzwh");
    recipe.crop_z = unsigned_u32(crop[1], "crop_xzwh");
    recipe.width = unsigned_u32(crop[2], "crop_xzwh");
    recipe.height = unsigned_u32(crop[3], "crop_xzwh");
    const double parsed_cell_size_m = finite_number(document.at("cell_size_m"), "cell_size_m");
    if (parsed_cell_size_m != static_cast<double>(kFluid25DNaturalFlowCellSizeM)) {
        throw std::runtime_error("natural-flow recipe requires exact native 30 metre cells");
    }
    recipe.cell_size_m = static_cast<float>(parsed_cell_size_m);
    recipe.source_center_xz =
        xz_pair(document.at("source_center_cell_xz"), "source_center_cell_xz");

    const Json& sources = document.at("source_cells_xz");
    if (!sources.is_array() || sources.size() != kFluid25DNaturalFlowSourceCellCount) {
        throw std::runtime_error(
            "natural-flow recipe source_cells_xz must contain exactly five cells");
    }
    for (std::size_t index = 0U; index < sources.size(); ++index) {
        recipe.source_cells_xz[index] = xz_pair(sources[index], "source_cells_xz");
    }

    if (recipe.has_expected_outlet) {
        const Json& outlet = document.at("expected_outlet");
        require_exact_fields(outlet, {"edge", "crop_cell_xz_bounds"}, "expected_outlet");
        recipe.expected_outlet_edge = edge_from_name(required_string(outlet.at("edge"), "edge"));
        const Json& bounds = outlet.at("crop_cell_xz_bounds");
        if (!bounds.is_array() || bounds.size() != 2U) {
            throw std::runtime_error(
                "natural-flow recipe expected outlet bounds must contain two [x,z] points");
        }
        recipe.expected_outlet_min_xz = xz_pair(bounds[0], "crop_cell_xz_bounds");
        recipe.expected_outlet_max_xz = xz_pair(bounds[1], "crop_cell_xz_bounds");
    }

    const Json& gauges = document.at("gauges");
    if (!gauges.is_array()) {
        throw std::runtime_error("natural-flow recipe gauges must be an array");
    }
    recipe.gauges.reserve(gauges.size());
    for (const Json& item : gauges) {
        require_exact_fields(
            item, {"name", "cell_xz", "tangent_dx_dz", "distance_m", "half_span_cells"}, "gauge");
        const std::array<std::uint32_t, 2U> cell = xz_pair(item.at("cell_xz"), "gauge cell_xz");
        const Json& tangent = item.at("tangent_dx_dz");
        if (!tangent.is_array() || tangent.size() != 2U) {
            throw std::runtime_error(
                "natural-flow recipe gauge tangent must be a two-integer pair");
        }
        recipe.gauges.push_back({
            .name = required_string(item.at("name"), "gauge name"),
            .x_cell = cell[0],
            .z_cell = cell[1],
            .tangent_dx = signed_i32(tangent[0], "gauge tangent"),
            .tangent_dz = signed_i32(tangent[1], "gauge tangent"),
            .distance_m = finite_number(item.at("distance_m"), "gauge distance_m"),
            .half_span_cells = unsigned_u32(item.at("half_span_cells"), "gauge half_span_cells"),
        });
    }
    validate_recipe_shape(recipe, recipe.width, recipe.height, recipe.cell_size_m);
    return recipe;
}

Fluid25DNaturalFlowRecipe load_fluid_25d_natural_flow_recipe(const std::filesystem::path& path) {
    if (path.empty()) {
        throw std::runtime_error("natural-flow recipe path must be nonempty");
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("could not open natural-flow recipe: " + path.string());
    }
    const std::string text{std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>()};
    if (stream.bad()) {
        throw std::runtime_error("could not read natural-flow recipe: " + path.string());
    }
    try {
        return parse_fluid_25d_natural_flow_recipe(text);
    } catch (const std::exception& error) {
        throw std::runtime_error("invalid natural-flow recipe " + path.string() + ": " +
                                 error.what());
    }
}

void validate_fluid_25d_natural_flow_recipe(const Fluid25DNaturalFlowRecipe& recipe,
                                            std::uint32_t grid_width, std::uint32_t grid_height,
                                            float cell_size_m) {
    validate_recipe_shape(recipe, grid_width, grid_height, cell_size_m);
}

void validate_fluid_25d_natural_flow_recipe_provenance(
    const Fluid25DNaturalFlowRecipe& recipe, const Fluid25DTerrainCaseProvenance& provenance,
    std::uint32_t grid_width, std::uint32_t grid_height, float cell_size_m) {
    validate_fluid_25d_natural_flow_recipe(recipe, grid_width, grid_height, cell_size_m);
    if (provenance.elevation_sha256 != recipe.elevation_sha256) {
        throw std::runtime_error("natural-flow recipe rejected the raw elevation SHA-256");
    }
    if (provenance.transformed_crop_sha256 != recipe.transformed_crop_sha256) {
        throw std::runtime_error("natural-flow recipe rejected the transformed crop SHA-256");
    }
    if (provenance.crop_x != recipe.crop_x || provenance.crop_z != recipe.crop_z ||
        provenance.crop_width != recipe.width || provenance.crop_height != recipe.height ||
        provenance.sample_spacing_m != recipe.cell_size_m || grid_width != recipe.width ||
        grid_height != recipe.height || cell_size_m != recipe.cell_size_m) {
        throw std::runtime_error("natural-flow recipe rejected terrain crop geometry or spacing");
    }
}

Fluid25DScenarioData make_fluid_25d_natural_flow_study_scenario(
    const Fluid25DConfig& config, const cubey::asset::TerrainRasterHeightSource& source,
    const Fluid25DNaturalFlowRecipe& recipe, std::uint32_t crop_x, std::uint32_t crop_z) {
    validate_fluid_25d_config(config);
    if (!fluid_25d_is_natural_terrain_study(config.scenario) ||
        recipe.has_expected_outlet != (config.scenario == Fluid25DScenario::NaturalFlowStudy)) {
        throw std::runtime_error("natural terrain recipe schema must match the selected study");
    }
    validate_fluid_25d_natural_flow_recipe(recipe, config.grid_width, config.grid_height,
                                           config.cell_size_m);
    if (crop_x != recipe.crop_x || crop_z != recipe.crop_z) {
        throw std::runtime_error(
            "natural-flow recipe crop origin does not match the requested terrain crop");
    }

    Fluid25DScenarioData scenario = make_fluid_25d_terrain_crop(
        config.grid_width, config.grid_height, config.cell_size_m, source, crop_x, crop_z);
    if (!scenario.terrain_provenance.has_value()) {
        throw std::runtime_error("natural-flow terrain crop did not produce provenance");
    }
    validate_fluid_25d_natural_flow_recipe_provenance(recipe, scenario.terrain_provenance.value(),
                                                      config.grid_width, config.grid_height,
                                                      config.cell_size_m);

    open_fluid_25d_all_outward_boundary_faces(scenario);
    const double cell_area_m2 = static_cast<double>(config.cell_size_m) * config.cell_size_m;
    const float depth_rate_m_per_s = static_cast<float>(
        static_cast<double>(config.natural_flow_source_m3_per_s) /
        (static_cast<double>(kFluid25DNaturalFlowSourceCellCount) * cell_area_m2));
    if (!std::isfinite(depth_rate_m_per_s) || !(depth_rate_m_per_s > 0.0F)) {
        throw std::runtime_error("natural-flow recipe source depth rate is not representable");
    }

    Fluid25DNaturalFlowStudyMetadata study;
    study.has_expected_outlet = recipe.has_expected_outlet;
    study.candidate_id = recipe.candidate_id;
    for (const auto& cell : recipe.source_cells_xz) {
        const std::size_t index =
            fluid_25d_scenario_index(config.grid_width, config.grid_height, cell[0], cell[1]);
        scenario.source_depth_rate_m_per_s[index] = depth_rate_m_per_s;
        study.source_cells.push_back(index);
    }
    scenario.source_cell =
        fluid_25d_scenario_index(config.grid_width, config.grid_height, recipe.source_center_xz[0],
                                 recipe.source_center_xz[1]);
    if (recipe.has_expected_outlet) {
        study.expected_outlet_edge = recipe.expected_outlet_edge;
        study.expected_outlet_x_min = recipe.expected_outlet_min_xz[0];
        study.expected_outlet_z_min = recipe.expected_outlet_min_xz[1];
        study.expected_outlet_x_max = recipe.expected_outlet_max_xz[0];
        study.expected_outlet_z_max = recipe.expected_outlet_max_xz[1];
        const std::uint32_t outlet_x =
            (study.expected_outlet_x_min + study.expected_outlet_x_max) / 2U;
        const std::uint32_t outlet_z =
            (study.expected_outlet_z_min + study.expected_outlet_z_max) / 2U;
        scenario.outlet_cell =
            fluid_25d_scenario_index(config.grid_width, config.grid_height, outlet_x, outlet_z);
    }
    study.gauges = recipe.gauges;
    scenario.natural_flow_study = std::move(study);
    // Terrain import creates dry fields; this scenario intentionally keeps
    // that state and never adds a sink or edits imported elevations.
    return scenario;
}

} // namespace cubey::projects::fluid::fluid_25d
