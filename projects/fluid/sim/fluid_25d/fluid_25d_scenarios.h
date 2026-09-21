#pragma once

#include "fluid_25d_config.h"

#include <cubey/asset/file_digest.h>
#include <cubey/asset/terrain_raster_height_source.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {

inline constexpr std::size_t kFluid25DNoCell = std::numeric_limits<std::size_t>::max();
inline constexpr std::uint32_t kFluid25DMountainSourceOutletCropX = 1536U;
inline constexpr std::uint32_t kFluid25DMountainSourceOutletCropZ = 1664U;
inline constexpr std::string_view kFluid25DMountainSourceOutletElevationSha256 =
    "2a919b516d8ae4fb8c193cdd8db1a8ba055ba702e5cbd1ff50ad2b7fc6ab3c48";
inline constexpr std::string_view kFluid25DMountainSourceOutletCropSha256 =
    "9bfebfe229886ded533556acaf11de541caddfc4cf8104d864da1232fa8b24c6";
inline constexpr std::uint32_t kFluid25DMountainSourceOutletSourceX = 8U;
inline constexpr std::uint32_t kFluid25DMountainSourceOutletSourceZ = 60U;
inline constexpr std::uint32_t kFluid25DMountainSourceOutletSinkX = 232U;
inline constexpr std::uint32_t kFluid25DMountainSourceOutletSinkZ = 122U;
inline constexpr std::uint32_t kFluid25DMountainSourceOutletEndpointRadiusCells = 5U;
inline constexpr float kFluid25DMountainSourceOutletEndpointTotalVolumeRateM3PerS = 0.75F;
inline constexpr std::array<std::array<std::uint32_t, 2U>, 3U>
    kFluid25DMountainSourceOutletDrainCells{{
        {232U, 127U},
        {229U, 126U},
        {231U, 126U},
    }};

[[nodiscard]] inline constexpr bool
fluid_25d_mountain_source_outlet_is_drain_cell(std::uint32_t x, std::uint32_t z) {
    for (const auto& cell : kFluid25DMountainSourceOutletDrainCells) {
        if (cell[0] == x && cell[1] == z) {
            return true;
        }
    }
    return false;
}

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
        const std::uint32_t source_x = 1U;
        const std::uint32_t sink_x = width - 2U;
        const std::uint32_t channel_y = height / 2U;
        const float channel_center = 0.5F * static_cast<float>(height - 1U);
        for (std::uint32_t y = 0U; y < height; ++y) {
            for (std::uint32_t x = 0U; x < width; ++x) {
                const float x_value = static_cast<float>(x);
                const float y_offset = static_cast<float>(y) - channel_center;
                data.terrain_height_m[fluid_25d_scenario_index(width, height, x, y)] =
                    3.20F - (0.085F * x_value) + (0.085F * y_offset * y_offset);
            }
        }
        data.source_cell = fluid_25d_scenario_index(width, height, source_x, channel_y);
        data.sink_cell = fluid_25d_scenario_index(width, height, sink_x, channel_y);
        // Keep River V0's narrow, initially dry-outlet oracle fixture stable.
        for (std::uint32_t x = source_x; x < sink_x; ++x) {
            data.initial_water_depth_m[fluid_25d_scenario_index(width, height, x, channel_y)] =
                0.05F;
        }
        data.source_depth_rate_m_per_s[data.source_cell] = 0.040F;
        data.sink_depth_rate_m_per_s[data.sink_cell] = 0.055F;
        break;
    }
    case Fluid25DScenario::SourceOutletDemo: {
        if (width < 6 || height < 3) {
            throw std::runtime_error("fluid 2.5D source-outlet demo requires at least a 6x3 grid");
        }
        // This opt-in explanatory scene is authored rather than discovered
        // from imported terrain. Its endpoint regions stay inside the closed
        // domain, but the route spans most of its width so the flow story has
        // enough room for broad bends, changing valley width, and one visible
        // constriction without changing the River V0 fixture.
        const std::uint32_t source_x = std::max(1U, (width * 11U) / 100U);
        const std::uint32_t sink_x =
            std::min(width - 2U, std::max(source_x + 2U, (width * 86U) / 100U));
        const float channel_center = 0.5F * static_cast<float>(height - 1U);
        const float half_grid_height = std::max(1.0F, 0.5F * static_cast<float>(height - 1U));
        const float endpoint_radius_x =
            std::max(0.75F, std::min(3.0F, 0.20F * static_cast<float>(sink_x - source_x)));
        constexpr float endpoint_radius_y = 3.25F;
        // The outlet is deliberately the terminal low basin, rather than a
        // label placed part-way down a still-descending valley. Once the
        // approach reaches its centroid, a broad longitudinal shoulder rises
        // across the remainder of the closed domain. This keeps the demo's
        // water in the explicit OUTLET region instead of letting it pool at
        // the far edge.
        constexpr float downstream_containment_rise_m = 1.30F;
        const auto route_progress = [source_x, sink_x](std::uint32_t x) {
            const float span = static_cast<float>(sink_x - source_x);
            return std::clamp((static_cast<float>(x) - static_cast<float>(source_x)) / span, 0.0F,
                              1.0F);
        };
        const auto channel_center_y = [channel_center, half_grid_height,
                                       route_progress](std::uint32_t x) {
            const float progress = route_progress(x);
            // Two smooth S-bends give a visibly terrain-guided route while
            // returning the source and outlet to the same readable latitude.
            return channel_center +
                   half_grid_height *
                       (0.18F * std::sin(2.0F * std::numbers::pi_v<float> * progress) +
                        0.10F * std::sin(4.0F * std::numbers::pi_v<float> * progress));
        };
        const auto channel_half_width = [route_progress](std::uint32_t x) {
            const float progress = route_progress(x);
            const float constriction_offset = (progress - 0.58F) / 0.075F;
            const float constriction = std::exp(-0.5F * constriction_offset * constriction_offset);
            const float broad_width_variation =
                (0.90F * std::sin(2.0F * std::numbers::pi_v<float> * progress + 0.80F)) +
                (0.50F * std::sin(4.0F * std::numbers::pi_v<float> * progress - 0.50F));
            return std::clamp(4.60F + broad_width_variation - (1.65F * constriction), 3.25F, 6.50F);
        };
        const std::uint32_t source_y = static_cast<std::uint32_t>(std::clamp(
            std::round(channel_center_y(source_x)), 0.0F, static_cast<float>(height - 1U)));
        const std::uint32_t sink_y = static_cast<std::uint32_t>(std::clamp(
            std::round(channel_center_y(sink_x)), 0.0F, static_cast<float>(height - 1U)));
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const float progress = route_progress(x);
                const float upstream_progress =
                    x < source_x ? static_cast<float>(source_x - x) / static_cast<float>(source_x)
                                 : 0.0F;
                const float downstream_progress =
                    x <= sink_x ? 0.0F
                                : static_cast<float>(x - sink_x) /
                                      static_cast<float>((width - 1U) - sink_x);
                const float y_offset = static_cast<float>(y) - channel_center_y(x);
                const float local_half_width = channel_half_width(x);
                const float normalized_cross_section = y_offset / local_half_width;
                const float cross_section_squared =
                    normalized_cross_section * normalized_cross_section;
                // Keep the hydraulic floor broad and smooth through the wet
                // corridor, then rise decisively only beyond it. This makes
                // the dry valley form readable at the explanatory camera
                // without steepening the channel-floor grade that carries the
                // finite-volume flow.
                const float outer_bank_t =
                    std::clamp((std::abs(normalized_cross_section) - 0.65F) / 1.35F, 0.0F, 1.0F);
                const float outer_bank =
                    outer_bank_t * outer_bank_t * (3.0F - (2.0F * outer_bank_t));
                const float valley_bank =
                    (0.65F * cross_section_squared / (1.0F + cross_section_squared)) +
                    (1.90F * outer_bank);
                const float ridge_distance = 1.65F * local_half_width;
                const float ridge_sigma = std::max(1.50F, 0.60F * local_half_width);
                const float ridge_offset = (std::abs(y_offset) - ridge_distance) / ridge_sigma;
                const float side_ridge = 0.85F * std::exp(-0.5F * ridge_offset * ridge_offset);
                const float basin_x_offset =
                    (static_cast<float>(x) - static_cast<float>(sink_x)) / 5.0F;
                const float basin_y_offset = y_offset / 8.0F;
                const float terminal_basin =
                    0.65F * std::exp(-0.5F * (basin_x_offset * basin_x_offset +
                                              basin_y_offset * basin_y_offset));
                // A smooth longitudinal fall drives the channel, variable
                // side ridges keep the water in its broad bends, and one
                // narrow reach makes the terrain guidance visible. The short
                // post-outlet shoulder is deliberately modest: it only seals
                // the terminal basin against the closed-domain tail.
                data.terrain_height_m[fluid_25d_scenario_index(width, height, x, y)] =
                    4.40F - (2.60F * progress) + (0.50F * upstream_progress * upstream_progress) +
                    (downstream_containment_rise_m * downstream_progress) + valley_bank +
                    side_ridge - terminal_basin;
            }
        }
        data.source_cell = fluid_25d_scenario_index(width, height, source_x, source_y);
        data.sink_cell = fluid_25d_scenario_index(width, height, sink_x, sink_y);
        const auto endpoint_weight = [endpoint_radius_x](float x_offset, float y_offset) {
            const float normalized_distance_squared =
                (x_offset * x_offset) / (endpoint_radius_x * endpoint_radius_x) +
                (y_offset * y_offset) / (endpoint_radius_y * endpoint_radius_y);
            return std::sqrt(std::max(0.0F, 1.0F - normalized_distance_squared));
        };
        std::vector<std::size_t> source_region;
        std::vector<std::size_t> sink_region;
        // The outlet mask excludes the vanishing-weight ellipse fringe. Those
        // shoulder cells can be dry even while the compact terminal basin is
        // visibly supplied, which would make a nominal outlet capacity look
        // like an unexplained loss of removal. The remaining bounded core
        // still receives the same total configured 0.03 m3/s capacity.
        constexpr float outlet_region_minimum_weight = 0.70F;
        // Seed a shallow, connected ribbon from the source through the outlet.
        // Compact endpoint pools make the bounded SOURCE and OUTLET regions
        // visibly active for the first practical observation interval. The
        // route is still an authored initial condition, not a claim that water
        // first injected at the source reaches the outlet within that interval.
        for (std::uint32_t x = source_x; x <= sink_x; ++x) {
            const float center_y = channel_center_y(x);
            const float local_half_width = channel_half_width(x);
            for (std::uint32_t y = 0U; y < height; ++y) {
                const float normalized_offset =
                    (static_cast<float>(y) - center_y) / local_half_width;
                const float ribbon = std::max(0.0F, 1.0F - normalized_offset * normalized_offset);
                const float source_weight =
                    endpoint_weight(static_cast<float>(x) - static_cast<float>(source_x),
                                    static_cast<float>(y) - static_cast<float>(source_y));
                const float sink_weight =
                    endpoint_weight(static_cast<float>(x) - static_cast<float>(sink_x),
                                    static_cast<float>(y) - static_cast<float>(sink_y));
                const std::size_t index = fluid_25d_scenario_index(width, height, x, y);
                data.initial_water_depth_m[index] =
                    std::max({0.028F * ribbon, 0.45F * source_weight, 0.70F * sink_weight});
                if (source_weight > 0.0F) {
                    source_region.push_back(index);
                }
                if (sink_weight >= outlet_region_minimum_weight) {
                    sink_region.push_back(index);
                }
            }
        }
        if (source_region.empty() || sink_region.empty()) {
            throw std::runtime_error("fluid 2.5D river endpoint region is empty");
        }
        // These are one bounded source region and one bounded outlet region,
        // expressed as depth rates so their total physical throughput is
        // stable across the region's discrete rasterization. source_cell and
        // sink_cell remain their representative marker centroids.
        constexpr float endpoint_total_volume_rate_m3_per_s = 0.03F;
        const float source_depth_rate =
            endpoint_total_volume_rate_m3_per_s /
            (static_cast<float>(source_region.size()) * cell_size_m * cell_size_m);
        const float sink_depth_rate =
            endpoint_total_volume_rate_m3_per_s /
            (static_cast<float>(sink_region.size()) * cell_size_m * cell_size_m);
        for (const std::size_t index : source_region) {
            data.source_depth_rate_m_per_s[index] = source_depth_rate;
        }
        for (const std::size_t index : sink_region) {
            data.sink_depth_rate_m_per_s[index] = sink_depth_rate;
        }
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
    case Fluid25DScenario::MountainSourceOutletDemo:
        throw std::runtime_error(
            "fluid 2.5D mountain-source-outlet-demo requires a pinned terrain heightfield");
    default:
        throw std::runtime_error("fluid 2.5D scenario value is invalid");
    }

    return data;
}

inline void
validate_fluid_25d_mountain_source_outlet_terrain_identity(const Fluid25DScenarioData& scenario) {
    if (!scenario.terrain_provenance.has_value()) {
        throw std::runtime_error(
            "fluid 2.5D mountain-source-outlet-demo requires terrain provenance");
    }
    const Fluid25DTerrainCaseProvenance& provenance = scenario.terrain_provenance.value();
    if (provenance.elevation_sha256 != kFluid25DMountainSourceOutletElevationSha256) {
        throw std::runtime_error(
            "fluid 2.5D mountain-source-outlet-demo rejected the terrain elevation SHA-256");
    }
    if (provenance.transformed_crop_sha256 != kFluid25DMountainSourceOutletCropSha256) {
        throw std::runtime_error(
            "fluid 2.5D mountain-source-outlet-demo rejected the transformed crop SHA-256");
    }
    if (provenance.crop_x != kFluid25DMountainSourceOutletCropX ||
        provenance.crop_z != kFluid25DMountainSourceOutletCropZ ||
        provenance.crop_width != kFluid25DMountainSourceOutletGridWidth ||
        provenance.crop_height != kFluid25DMountainSourceOutletGridHeight ||
        provenance.sample_spacing_m != kFluid25DMountainSourceOutletCellSizeM ||
        scenario.width != kFluid25DMountainSourceOutletGridWidth ||
        scenario.height != kFluid25DMountainSourceOutletGridHeight ||
        scenario.cell_size_m != kFluid25DMountainSourceOutletCellSizeM) {
        throw std::runtime_error(
            "fluid 2.5D mountain-source-outlet-demo rejected terrain crop geometry or spacing");
    }
    const std::string expected_identity = fluid_25d_terrain_case_identity(
        provenance.elevation_sha256, provenance.transformed_crop_sha256, provenance.crop_x,
        provenance.crop_z, provenance.crop_width, provenance.crop_height,
        provenance.sample_spacing_m);
    if (provenance.identity != expected_identity) {
        throw std::runtime_error(
            "fluid 2.5D mountain-source-outlet-demo rejected inconsistent terrain provenance");
    }
}

[[nodiscard]] inline float fluid_25d_mountain_source_outlet_smooth_falloff(float distance_cells,
                                                                           float radius_cells) {
    if (!(radius_cells > 0.0F) || !std::isfinite(radius_cells) || !std::isfinite(distance_cells)) {
        throw std::runtime_error(
            "fluid 2.5D mountain-source-outlet-demo falloff inputs are invalid");
    }
    const float t = std::clamp(1.0F - (distance_cells / radius_cells), 0.0F, 1.0F);
    return t * t * (3.0F - (2.0F * t));
}

// This is deliberately product-only field authoring, separated from raster
// ingestion so its endpoint and route contract can be tested without the
// external terrain cache. The fixed control polyline affects initial water
// only; it never modifies the imported elevation or solver forcing fields.
inline void author_fluid_25d_mountain_source_outlet_fields(Fluid25DScenarioData& scenario) {
    const std::size_t cell_count = fluid_25d_scenario_cell_count(scenario.width, scenario.height);
    if (scenario.width != kFluid25DMountainSourceOutletGridWidth ||
        scenario.height != kFluid25DMountainSourceOutletGridHeight ||
        scenario.cell_size_m != kFluid25DMountainSourceOutletCellSizeM ||
        scenario.terrain_height_m.size() != cell_count ||
        scenario.initial_water_depth_m.size() != cell_count ||
        scenario.source_depth_rate_m_per_s.size() != cell_count ||
        scenario.sink_depth_rate_m_per_s.size() != cell_count ||
        scenario.boundary_outflow_face_mask.size() != cell_count) {
        throw std::runtime_error(
            "fluid 2.5D mountain-source-outlet-demo fields have invalid dimensions");
    }

    std::fill(scenario.initial_water_depth_m.begin(), scenario.initial_water_depth_m.end(), 0.0F);
    std::fill(scenario.source_depth_rate_m_per_s.begin(), scenario.source_depth_rate_m_per_s.end(),
              0.0F);
    std::fill(scenario.sink_depth_rate_m_per_s.begin(), scenario.sink_depth_rate_m_per_s.end(),
              0.0F);
    std::fill(scenario.boundary_outflow_face_mask.begin(),
              scenario.boundary_outflow_face_mask.end(), 0U);

    using ControlPoint = std::array<float, 2>;
    constexpr std::array<ControlPoint, 15U> kRoute{
        ControlPoint{8.0F, 60.0F},    ControlPoint{24.0F, 60.0F},   ControlPoint{40.0F, 62.0F},
        ControlPoint{56.0F, 65.0F},   ControlPoint{72.0F, 69.0F},   ControlPoint{88.0F, 78.0F},
        ControlPoint{104.0F, 89.0F},  ControlPoint{120.0F, 91.0F},  ControlPoint{136.0F, 92.0F},
        ControlPoint{152.0F, 98.0F},  ControlPoint{168.0F, 108.0F}, ControlPoint{184.0F, 122.0F},
        ControlPoint{200.0F, 123.0F}, ControlPoint{216.0F, 123.0F}, ControlPoint{232.0F, 122.0F},
    };
    constexpr float kCorridorRadiusCells = 4.0F;
    constexpr float kCorridorMaximumDepthM = 0.08F;
    constexpr float kSourcePoolMaximumDepthM = 0.30F;
    constexpr float kSinkPoolMaximumDepthM = 0.50F;

    const auto point_to_segment_distance_cells = [](float x, float z, const ControlPoint& a,
                                                    const ControlPoint& b) {
        const float segment_x = b[0] - a[0];
        const float segment_z = b[1] - a[1];
        const float offset_x = x - a[0];
        const float offset_z = z - a[1];
        const float segment_length_squared = (segment_x * segment_x) + (segment_z * segment_z);
        const float projection = std::clamp(
            ((offset_x * segment_x) + (offset_z * segment_z)) / segment_length_squared, 0.0F, 1.0F);
        const float closest_x = a[0] + (projection * segment_x);
        const float closest_z = a[1] + (projection * segment_z);
        return std::hypot(x - closest_x, z - closest_z);
    };
    const auto radial_distance_cells = [](float x, float z, std::uint32_t center_x,
                                          std::uint32_t center_z) {
        return std::hypot(x - static_cast<float>(center_x), z - static_cast<float>(center_z));
    };
    for (std::uint32_t z = 0U; z < scenario.height; ++z) {
        for (std::uint32_t x = 0U; x < scenario.width; ++x) {
            const float x_cells = static_cast<float>(x);
            const float z_cells = static_cast<float>(z);
            float nearest_route_distance_cells = std::numeric_limits<float>::infinity();
            for (std::size_t point = 0U; point + 1U < kRoute.size(); ++point) {
                nearest_route_distance_cells =
                    std::min(nearest_route_distance_cells,
                             point_to_segment_distance_cells(x_cells, z_cells, kRoute[point],
                                                             kRoute[point + 1U]));
            }
            const float corridor_depth_m =
                kCorridorMaximumDepthM * fluid_25d_mountain_source_outlet_smooth_falloff(
                                             nearest_route_distance_cells, kCorridorRadiusCells);
            const float source_pool_depth_m =
                kSourcePoolMaximumDepthM *
                fluid_25d_mountain_source_outlet_smooth_falloff(
                    radial_distance_cells(x_cells, z_cells, kFluid25DMountainSourceOutletSourceX,
                                          kFluid25DMountainSourceOutletSourceZ),
                    static_cast<float>(kFluid25DMountainSourceOutletEndpointRadiusCells));
            const float sink_pool_depth_m =
                kSinkPoolMaximumDepthM *
                fluid_25d_mountain_source_outlet_smooth_falloff(
                    radial_distance_cells(x_cells, z_cells, kFluid25DMountainSourceOutletSinkX,
                                          kFluid25DMountainSourceOutletSinkZ),
                    static_cast<float>(kFluid25DMountainSourceOutletEndpointRadiusCells));
            scenario.initial_water_depth_m[fluid_25d_scenario_index(scenario.width, scenario.height,
                                                                    x, z)] =
                std::max({corridor_depth_m, source_pool_depth_m, sink_pool_depth_m});
        }
    }

    const auto in_endpoint_disk = [](std::uint32_t x, std::uint32_t z, std::uint32_t center_x,
                                     std::uint32_t center_z) {
        const std::int32_t dx = static_cast<std::int32_t>(x) - static_cast<std::int32_t>(center_x);
        const std::int32_t dz = static_cast<std::int32_t>(z) - static_cast<std::int32_t>(center_z);
        return (dx * dx) + (dz * dz) <=
               static_cast<std::int32_t>(kFluid25DMountainSourceOutletEndpointRadiusCells *
                                         kFluid25DMountainSourceOutletEndpointRadiusCells);
    };
    std::size_t source_region_count = 0U;
    std::size_t visible_sink_region_count = 0U;
    std::size_t sink_rate_region_count = 0U;
    for (std::uint32_t z = 0U; z < scenario.height; ++z) {
        for (std::uint32_t x = 0U; x < scenario.width; ++x) {
            const bool source = in_endpoint_disk(x, z, kFluid25DMountainSourceOutletSourceX,
                                                 kFluid25DMountainSourceOutletSourceZ);
            const bool visible_sink = in_endpoint_disk(x, z, kFluid25DMountainSourceOutletSinkX,
                                                       kFluid25DMountainSourceOutletSinkZ);
            const bool sink_rate = fluid_25d_mountain_source_outlet_is_drain_cell(x, z);
            if (source && visible_sink) {
                throw std::runtime_error(
                    "fluid 2.5D mountain-source-outlet-demo endpoint regions overlap");
            }
            source_region_count += source ? 1U : 0U;
            visible_sink_region_count += visible_sink ? 1U : 0U;
            sink_rate_region_count += sink_rate ? 1U : 0U;
        }
    }
    constexpr std::size_t kExpectedEndpointRegionCellCount = 81U;
    constexpr std::size_t kExpectedSinkRateRegionCellCount =
        kFluid25DMountainSourceOutletDrainCells.size();
    if (source_region_count != kExpectedEndpointRegionCellCount ||
        visible_sink_region_count != kExpectedEndpointRegionCellCount ||
        sink_rate_region_count != kExpectedSinkRateRegionCellCount) {
        throw std::runtime_error(
            "fluid 2.5D mountain-source-outlet-demo endpoint rasterization is invalid");
    }
    const float cell_area_m2 = scenario.cell_size_m * scenario.cell_size_m;
    const float source_depth_rate_m_per_s =
        kFluid25DMountainSourceOutletEndpointTotalVolumeRateM3PerS /
        (static_cast<float>(source_region_count) * cell_area_m2);
    const float sink_depth_rate_m_per_s =
        kFluid25DMountainSourceOutletEndpointTotalVolumeRateM3PerS /
        (static_cast<float>(sink_rate_region_count) * cell_area_m2);
    for (std::uint32_t z = 0U; z < scenario.height; ++z) {
        for (std::uint32_t x = 0U; x < scenario.width; ++x) {
            const std::size_t index =
                fluid_25d_scenario_index(scenario.width, scenario.height, x, z);
            if (in_endpoint_disk(x, z, kFluid25DMountainSourceOutletSourceX,
                                 kFluid25DMountainSourceOutletSourceZ)) {
                scenario.source_depth_rate_m_per_s[index] = source_depth_rate_m_per_s;
            }
            const bool sink_rate = fluid_25d_mountain_source_outlet_is_drain_cell(x, z);
            if (sink_rate) {
                // This fixed terminal reserve is the actual removal mask. Prime
                // it for immediate established-flow readability; it is not
                // parcel-transit proof, terrain modification, or hidden solver
                // guidance.
                scenario.initial_water_depth_m[index] =
                    std::max(scenario.initial_water_depth_m[index], 2.0F);
                scenario.sink_depth_rate_m_per_s[index] = sink_depth_rate_m_per_s;
            }
        }
    }
    scenario.source_cell = fluid_25d_scenario_index(scenario.width, scenario.height,
                                                    kFluid25DMountainSourceOutletSourceX,
                                                    kFluid25DMountainSourceOutletSourceZ);
    scenario.sink_cell = fluid_25d_scenario_index(scenario.width, scenario.height,
                                                  kFluid25DMountainSourceOutletSinkX,
                                                  kFluid25DMountainSourceOutletSinkZ);
}

// Import one native, row-major crop from the shared immutable raster source.
// This construction is intentionally scenario-neutral: it creates only the
// dry, closed raster-backed fields and their provenance. Product or audition
// protocols are applied by the owning scenario wrapper after this boundary.
[[nodiscard]] inline Fluid25DScenarioData make_fluid_25d_terrain_crop(
    std::uint32_t width, std::uint32_t height, float expected_cell_size_m,
    const cubey::asset::TerrainRasterHeightSource& source, std::uint32_t crop_x = 0U,
    std::uint32_t crop_z = 0U) {
    const std::size_t cell_count = fluid_25d_scenario_cell_count(width, height);
    if (!std::isfinite(expected_cell_size_m) || expected_cell_size_m <= 0.0F) {
        throw std::runtime_error(
            "fluid 2.5D terrain crop cell size must be finite and positive");
    }
    const float spacing_m = source.sample_spacing_m();
    if (!std::isfinite(spacing_m) || spacing_m <= 0.0F) {
        throw std::runtime_error("fluid 2.5D terrain source has invalid sample spacing");
    }
    if (expected_cell_size_m != spacing_m) {
        throw std::runtime_error(
            "fluid 2.5D terrain case cell size must equal the source sample spacing");
    }
    if (crop_x > source.width() || width > source.width() - crop_x || crop_z > source.height() ||
        height > source.height() - crop_z) {
        throw std::runtime_error("fluid 2.5D terrain case crop is outside the heightfield bounds");
    }

    Fluid25DScenarioData data;
    data.width = width;
    data.height = height;
    data.cell_size_m = spacing_m;
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
    return data;
}

[[nodiscard]] inline Fluid25DScenarioData make_fluid_25d_mountain_source_outlet_scenario(
    const Fluid25DConfig& config, const cubey::asset::TerrainRasterHeightSource& source) {
    if (config.scenario != Fluid25DScenario::MountainSourceOutletDemo) {
        throw std::runtime_error(
            "fluid 2.5D mountain scenario builder requires scenario mountain-source-outlet-demo");
    }
    validate_fluid_25d_config(config);
    if (source.sample_spacing_m() != kFluid25DMountainSourceOutletCellSizeM) {
        throw std::runtime_error("fluid 2.5D mountain-source-outlet-demo requires a terrain source "
                                 "with 30 metre spacing");
    }
    Fluid25DScenarioData scenario = make_fluid_25d_terrain_crop(
        config.grid_width, config.grid_height, config.cell_size_m, source,
        kFluid25DMountainSourceOutletCropX, kFluid25DMountainSourceOutletCropZ);
    validate_fluid_25d_mountain_source_outlet_terrain_identity(scenario);
    author_fluid_25d_mountain_source_outlet_fields(scenario);
    return scenario;
}

// The neutral terrain audition entrypoint validates TerrainCase and applies
// only its existing complete-field water protocols after importing the
// immutable crop. Terrain-backed product scenarios should use the crop helper
// above and own their separate endpoint/mask construction.
[[nodiscard]] inline Fluid25DScenarioData
make_fluid_25d_terrain_scenario(const Fluid25DConfig& config,
                                const cubey::asset::TerrainRasterHeightSource& source,
                                std::uint32_t crop_x = 0U, std::uint32_t crop_z = 0U) {
    if (config.scenario != Fluid25DScenario::TerrainCase) {
        throw std::runtime_error(
            "fluid 2.5D terrain scenario builder requires scenario terrain-case");
    }
    validate_fluid_25d_config(config);
    Fluid25DScenarioData data = make_fluid_25d_terrain_crop(
        config.grid_width, config.grid_height, config.cell_size_m, source, crop_x, crop_z);
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
