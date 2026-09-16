#pragma once

#include "fluid_25d_config.h"

#include <cubey/asset/file_digest.h>
#include <cubey/asset/terrain_raster_height_source.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {

inline constexpr std::size_t kFluid25DNoCell = std::numeric_limits<std::size_t>::max();

// Geometric face order shared by all solvers: left, right, down, up. The
// virtual-pipes implementation additionally stores directed discharge in
// this order; finite volume uses the bits only for geometric boundaries.
enum class Fluid25DFace : std::uint32_t {
    Left = 0,
    Right = 1,
    Down = 2,
    Up = 3,
};

inline constexpr std::uint32_t kFluid25DBoundaryOutflowLeft = 1U << 0U;
inline constexpr std::uint32_t kFluid25DBoundaryOutflowRight = 1U << 1U;
inline constexpr std::uint32_t kFluid25DBoundaryOutflowDown = 1U << 2U;
inline constexpr std::uint32_t kFluid25DBoundaryOutflowUp = 1U << 3U;
inline constexpr std::uint32_t kFluid25DBoundaryOutflowFaceMask =
    kFluid25DBoundaryOutflowLeft | kFluid25DBoundaryOutflowRight | kFluid25DBoundaryOutflowDown |
    kFluid25DBoundaryOutflowUp;

[[nodiscard]] inline constexpr std::uint32_t fluid_25d_boundary_outflow_bit(Fluid25DFace face) {
    return 1U << static_cast<std::uint32_t>(face);
}

struct Fluid25DTerrainCaseProvenance {
    std::filesystem::path manifest_path{};
    std::string source_id{};
    std::string elevation_sha256{};
    std::string transformed_crop_sha256{};
    std::uint32_t crop_x = 0U;
    std::uint32_t crop_z = 0U;
    std::uint32_t crop_width = 0U;
    std::uint32_t crop_height = 0U;
    float sample_spacing_m = 0.0F;
    std::string identity{};
};

// Scenario fields use one value per cell in row-major order.  Source and sink
// fields are depth rates (m/s), not volume rates; the oracle multiplies by
// cell area before recording the m3 ledger.
struct Fluid25DScenarioData {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    float cell_size_m = 0.0F;
    std::vector<float> terrain_height_m{};
    std::vector<float> initial_water_depth_m{};
    std::vector<float> source_depth_rate_m_per_s{};
    std::vector<float> sink_depth_rate_m_per_s{};
    // Opened faces see a dry exterior at the edge cell's bed elevation. This
    // is intentionally outflow-only, not a general transmissive boundary.
    std::vector<std::uint32_t> boundary_outflow_face_mask{};
    std::size_t source_cell = kFluid25DNoCell;
    std::size_t sink_cell = kFluid25DNoCell;
    std::optional<Fluid25DTerrainCaseProvenance> terrain_provenance{};
};

[[nodiscard]] inline std::string
fluid_25d_terrain_case_identity(std::string_view elevation_sha256,
                                std::string_view transformed_crop_sha256, std::uint32_t crop_x,
                                std::uint32_t crop_z, std::uint32_t crop_width,
                                std::uint32_t crop_height, float sample_spacing_m) {
    if (!cubey::asset::is_sha256_hex(elevation_sha256) ||
        !cubey::asset::is_sha256_hex(transformed_crop_sha256) || !std::isfinite(sample_spacing_m) ||
        sample_spacing_m <= 0.0F) {
        throw std::runtime_error("fluid 2.5D terrain case identity inputs are invalid");
    }
    return "terrain-v1:elevation-sha256=" + std::string(elevation_sha256) +
           ":crop-sha256=" + std::string(transformed_crop_sha256) +
           ":crop=" + std::to_string(crop_x) + "," + std::to_string(crop_z) + "," +
           std::to_string(crop_width) + "x" + std::to_string(crop_height) +
           ":spacing-m=" + std::to_string(sample_spacing_m);
}

[[nodiscard]] inline std::size_t fluid_25d_scenario_cell_count(std::uint32_t width,
                                                               std::uint32_t height) {
    if (width == 0 || height == 0) {
        throw std::runtime_error("fluid 2.5D scenario dimensions must be positive");
    }
    const std::size_t width_value = static_cast<std::size_t>(width);
    const std::size_t height_value = static_cast<std::size_t>(height);
    if (width_value > std::numeric_limits<std::size_t>::max() / height_value) {
        throw std::runtime_error("fluid 2.5D scenario dimensions are too large");
    }
    return width_value * height_value;
}

[[nodiscard]] inline std::size_t fluid_25d_scenario_index(std::uint32_t width, std::uint32_t height,
                                                          std::uint32_t x, std::uint32_t y) {
    if (x >= width || y >= height) {
        throw std::runtime_error("fluid 2.5D scenario coordinate is out of bounds");
    }
    return (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) +
           static_cast<std::size_t>(x);
}

[[nodiscard]] inline std::vector<std::uint32_t>
make_fluid_25d_all_outward_boundary_outflow_mask(std::uint32_t width, std::uint32_t height) {
    std::vector<std::uint32_t> mask(fluid_25d_scenario_cell_count(width, height), 0U);
    for (std::uint32_t y = 0U; y < height; ++y) {
        for (std::uint32_t x = 0U; x < width; ++x) {
            std::uint32_t outward = 0U;
            if (x == 0U) {
                outward |= kFluid25DBoundaryOutflowLeft;
            }
            if (x + 1U == width) {
                outward |= kFluid25DBoundaryOutflowRight;
            }
            if (y == 0U) {
                outward |= kFluid25DBoundaryOutflowDown;
            }
            if (y + 1U == height) {
                outward |= kFluid25DBoundaryOutflowUp;
            }
            mask[fluid_25d_scenario_index(width, height, x, y)] = outward;
        }
    }
    return mask;
}

inline void open_fluid_25d_all_outward_boundary_faces(Fluid25DScenarioData& scenario) {
    scenario.boundary_outflow_face_mask =
        make_fluid_25d_all_outward_boundary_outflow_mask(scenario.width, scenario.height);
}

// Applies only complete, neutral terrain-case field constructions. These
// protocols deliberately operate over the full crop and the complete outward
// perimeter: terrain is immutable input, not something shaped around a desired
// source, channel, sink, or basin result.
inline void apply_fluid_25d_terrain_water_protocol(const Fluid25DConfig& config,
                                                   Fluid25DScenarioData& scenario) {
    if (config.scenario != Fluid25DScenario::TerrainCase) {
        throw std::runtime_error(
            "fluid 2.5D terrain-water protocol requires scenario terrain-case");
    }
    const std::size_t cell_count = fluid_25d_scenario_cell_count(scenario.width, scenario.height);
    if (scenario.initial_water_depth_m.size() != cell_count ||
        scenario.source_depth_rate_m_per_s.size() != cell_count ||
        scenario.sink_depth_rate_m_per_s.size() != cell_count) {
        throw std::runtime_error("fluid 2.5D terrain protocol fields have invalid dimensions");
    }
    switch (config.terrain_water_protocol) {
    case Fluid25DTerrainWaterProtocol::None:
        return;
    case Fluid25DTerrainWaterProtocol::RainPulse:
        if (!(config.rainfall_depth_rate_m_per_s > 0.0F)) {
            throw std::runtime_error("fluid 2.5D rain-pulse requires a positive rainfall rate");
        }
        std::fill(scenario.source_depth_rate_m_per_s.begin(),
                  scenario.source_depth_rate_m_per_s.end(), config.rainfall_depth_rate_m_per_s);
        open_fluid_25d_all_outward_boundary_faces(scenario);
        return;
    case Fluid25DTerrainWaterProtocol::SheetRelease:
        if (!(config.sheet_initial_depth_m > 0.0F)) {
            throw std::runtime_error("fluid 2.5D sheet-release requires a positive sheet depth");
        }
        std::fill(scenario.initial_water_depth_m.begin(), scenario.initial_water_depth_m.end(),
                  config.sheet_initial_depth_m);
        open_fluid_25d_all_outward_boundary_faces(scenario);
        return;
    }
    throw std::runtime_error("fluid 2.5D terrain-water protocol value is invalid");
}

inline void
validate_fluid_25d_boundary_outflow_face_mask(std::uint32_t width, std::uint32_t height,
                                              std::span<const std::uint32_t> face_mask) {
    const std::size_t cell_count = fluid_25d_scenario_cell_count(width, height);
    if (face_mask.size() != cell_count) {
        throw std::runtime_error("fluid 2.5D boundary outflow mask size does not match scenario");
    }
    for (std::uint32_t y = 0U; y < height; ++y) {
        for (std::uint32_t x = 0U; x < width; ++x) {
            std::uint32_t outward_faces = 0U;
            if (x == 0U) {
                outward_faces |= kFluid25DBoundaryOutflowLeft;
            }
            if (x + 1U == width) {
                outward_faces |= kFluid25DBoundaryOutflowRight;
            }
            if (y == 0U) {
                outward_faces |= kFluid25DBoundaryOutflowDown;
            }
            if (y + 1U == height) {
                outward_faces |= kFluid25DBoundaryOutflowUp;
            }
            const std::uint32_t mask = face_mask[fluid_25d_scenario_index(width, height, x, y)];
            if ((mask & ~kFluid25DBoundaryOutflowFaceMask) != 0U || (mask & ~outward_faces) != 0U) {
                throw std::runtime_error(
                    "fluid 2.5D boundary outflow mask marks a non-outward face");
            }
        }
    }
}

[[nodiscard]] inline Fluid25DScenarioData make_fluid_25d_scenario(Fluid25DScenario scenario,
                                                                  std::uint32_t width,
                                                                  std::uint32_t height,
                                                                  float cell_size_m) {
    if (!(cell_size_m > 0.0F) || !std::isfinite(cell_size_m)) {
        throw std::runtime_error("fluid 2.5D scenario cell size must be finite and positive");
    }
    const std::size_t cell_count = fluid_25d_scenario_cell_count(width, height);
    Fluid25DScenarioData data;
    data.width = width;
    data.height = height;
    data.cell_size_m = cell_size_m;
    data.terrain_height_m.resize(cell_count);
    data.initial_water_depth_m.assign(cell_count, 0.0F);
    data.source_depth_rate_m_per_s.assign(cell_count, 0.0F);
    data.sink_depth_rate_m_per_s.assign(cell_count, 0.0F);
    data.boundary_outflow_face_mask.assign(cell_count, 0U);

    switch (scenario) {
    case Fluid25DScenario::DryBed: {
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const float x_value = static_cast<float>(x);
                const float y_value = static_cast<float>(y);
                data.terrain_height_m[fluid_25d_scenario_index(width, height, x, y)] =
                    0.20F + (0.035F * x_value) + (0.017F * y_value) + (0.001F * x_value * y_value);
            }
        }
        break;
    }
    case Fluid25DScenario::LakeAtRest: {
        float maximum_terrain_height_m = -std::numeric_limits<float>::infinity();
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                // Binary-friendly coefficients make eta = terrain + depth
                // exactly constant in the float oracle, so this fixture tests
                // the solver's lake-at-rest behavior rather than cancellation
                // noise from decimal literals.
                const float terrain_height_m = 0.5F + (0.125F * static_cast<float>(x)) +
                                               (0.0625F * static_cast<float>(y)) +
                                               (0.015625F * static_cast<float>((x + y) % 3U));
                data.terrain_height_m[fluid_25d_scenario_index(width, height, x, y)] =
                    terrain_height_m;
                maximum_terrain_height_m = std::max(maximum_terrain_height_m, terrain_height_m);
            }
        }
        const float surface_height_m = maximum_terrain_height_m + 0.375F;
        for (std::size_t index = 0; index < cell_count; ++index) {
            data.initial_water_depth_m[index] = surface_height_m - data.terrain_height_m[index];
        }
        break;
    }
    case Fluid25DScenario::RiverCatchment: {
        if (width < 6 || height < 3) {
            throw std::runtime_error("fluid 2.5D river scenario requires at least a 6x3 grid");
        }
        const std::uint32_t source_x = 1;
        const std::uint32_t sink_x = width - 2U;
        const std::uint32_t channel_y = height / 2U;
        const float channel_center = 0.5F * static_cast<float>(height - 1U);
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const float x_value = static_cast<float>(x);
                const float y_offset = static_cast<float>(y) - channel_center;
                // The longitudinal fall creates the source-to-sink direction;
                // the quadratic cross-section keeps water in a shallow channel.
                data.terrain_height_m[fluid_25d_scenario_index(width, height, x, y)] =
                    3.20F - (0.085F * x_value) + (0.085F * y_offset * y_offset);
            }
        }
        data.source_cell = fluid_25d_scenario_index(width, height, source_x, channel_y);
        data.sink_cell = fluid_25d_scenario_index(width, height, sink_x, channel_y);
        // Seed a thin, deterministic channel so the first CPU/GPU comparison
        // exercises the complete source-to-sink route without waiting for a
        // numerically vanishing wetting front to cross every dry cell.
        for (std::uint32_t x = source_x; x < sink_x; ++x) {
            data.initial_water_depth_m[fluid_25d_scenario_index(width, height, x, channel_y)] =
                0.05F;
        }
        data.source_depth_rate_m_per_s[data.source_cell] = 0.040F;
        data.sink_depth_rate_m_per_s[data.sink_cell] = 0.055F;
        break;
    }
    case Fluid25DScenario::BoundaryDrainFixture: {
        // Compact numerical fixture for the outflow-only contract. It is not
        // a product terrain: only right-edge faces are opened, and the small
        // source lets GPU validation exercise the scheduling primitive too.
        if (width < 3U || height < 2U) {
            throw std::runtime_error(
                "fluid 2.5D boundary-drain fixture requires at least a 3x2 grid");
        }
        const std::uint32_t channel_y = height / 2U;
        for (std::uint32_t y = 0U; y < height; ++y) {
            for (std::uint32_t x = 0U; x < width; ++x) {
                const std::size_t index = fluid_25d_scenario_index(width, height, x, y);
                data.terrain_height_m[index] =
                    0.08F * static_cast<float>(width - 1U - x) +
                    0.02F * std::abs(static_cast<float>(y) - static_cast<float>(channel_y));
                if (x + 1U == width) {
                    data.boundary_outflow_face_mask[index] = kFluid25DBoundaryOutflowRight;
                }
            }
        }
        data.source_cell = fluid_25d_scenario_index(width, height, 0U, channel_y);
        data.source_depth_rate_m_per_s[data.source_cell] = 0.025F;
        data.initial_water_depth_m[fluid_25d_scenario_index(width, height, width - 1U, channel_y)] =
            0.20F;
        break;
    }
    default:
        throw std::runtime_error("fluid 2.5D scenario value is invalid");
    }

    return data;
}

// Import one native, row-major crop from the shared immutable raster source.
// A terrain case is dry and closed by default. Its only supported audition
// overrides are complete-field, candidate-independent protocols applied after
// the immutable crop and its provenance have been constructed.
[[nodiscard]] inline Fluid25DScenarioData
make_fluid_25d_terrain_scenario(const Fluid25DConfig& config,
                                const cubey::asset::TerrainRasterHeightSource& source,
                                std::uint32_t crop_x = 0U, std::uint32_t crop_z = 0U) {
    if (config.scenario != Fluid25DScenario::TerrainCase) {
        throw std::runtime_error(
            "fluid 2.5D terrain scenario builder requires scenario terrain-case");
    }
    validate_fluid_25d_config(config);
    const float spacing_m = source.sample_spacing_m();
    if (!std::isfinite(spacing_m) || spacing_m <= 0.0F) {
        throw std::runtime_error("fluid 2.5D terrain source has invalid sample spacing");
    }
    if (config.cell_size_m != spacing_m) {
        throw std::runtime_error(
            "fluid 2.5D terrain case cell size must equal the source sample spacing");
    }
    if (crop_x > source.width() || config.grid_width > source.width() - crop_x ||
        crop_z > source.height() || config.grid_height > source.height() - crop_z) {
        throw std::runtime_error("fluid 2.5D terrain case crop is outside the heightfield bounds");
    }

    Fluid25DScenarioData data;
    data.width = config.grid_width;
    data.height = config.grid_height;
    data.cell_size_m = spacing_m;
    const std::size_t cell_count = fluid_25d_scenario_cell_count(data.width, data.height);
    data.terrain_height_m.resize(cell_count);
    data.initial_water_depth_m.assign(cell_count, 0.0F);
    data.source_depth_rate_m_per_s.assign(cell_count, 0.0F);
    data.sink_depth_rate_m_per_s.assign(cell_count, 0.0F);
    data.boundary_outflow_face_mask.assign(cell_count, 0U);

    const cubey::asset::TerrainHeightSourceBounds bounds = source.bounds();
    for (std::uint32_t z = 0U; z < data.height; ++z) {
        for (std::uint32_t x = 0U; x < data.width; ++x) {
            const cubey::math::Vec2 world_xz{
                bounds.minimum_xz.x + static_cast<float>(crop_x + x) * spacing_m,
                bounds.minimum_xz.y + static_cast<float>(crop_z + z) * spacing_m,
            };
            const float height_m =
                source.sample_height({.world_xz = world_xz, .footprint_m = 0.0F});
            if (!std::isfinite(height_m)) {
                throw std::runtime_error("fluid 2.5D terrain case contains a non-finite height");
            }
            data.terrain_height_m[fluid_25d_scenario_index(data.width, data.height, x, z)] =
                height_m;
        }
    }

    const std::string transformed_crop_sha256 =
        cubey::asset::sha256_hex(std::as_bytes(std::span{data.terrain_height_m}));
    const cubey::asset::TerrainRasterProvenance& source_provenance = source.provenance();
    const cubey::asset::TerrainHeightSourceMetadata source_metadata = source.metadata();
    data.terrain_provenance = Fluid25DTerrainCaseProvenance{
        .manifest_path = source_provenance.manifest_path,
        .source_id = std::string(source_metadata.id),
        .elevation_sha256 = source_provenance.elevation_sha256,
        .transformed_crop_sha256 = transformed_crop_sha256,
        .crop_x = crop_x,
        .crop_z = crop_z,
        .crop_width = data.width,
        .crop_height = data.height,
        .sample_spacing_m = spacing_m,
        .identity = fluid_25d_terrain_case_identity(source_provenance.elevation_sha256,
                                                    transformed_crop_sha256, crop_x, crop_z,
                                                    data.width, data.height, spacing_m),
    };
    apply_fluid_25d_terrain_water_protocol(config, data);
    return data;
}

[[nodiscard]] inline Fluid25DScenarioData
load_fluid_25d_terrain_scenario(const Fluid25DConfig& config,
                                const std::filesystem::path& field_path, std::uint32_t crop_x = 0U,
                                std::uint32_t crop_z = 0U) {
    const cubey::asset::TerrainRasterHeightSource source(field_path);
    return make_fluid_25d_terrain_scenario(config, source, crop_x, crop_z);
}

} // namespace cubey::projects::fluid::fluid_25d
