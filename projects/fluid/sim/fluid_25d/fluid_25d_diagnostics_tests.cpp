#include "fluid_25d_test_support.h"

namespace cubey::projects::fluid::fluid_25d::test_detail {

void test_sustained_headwaters_station_diagnostics() {
    using namespace cubey::projects::fluid::fluid_25d;
    struct ExpectedStation {
        std::string_view name;
        std::uint32_t x_reference_cell;
        std::uint32_t y_reference_cell;
    };
    constexpr std::array<ExpectedStation, kFluid25DSustainedHeadwatersStationCount> expected{{
        {"source_a", 4U, 8U},
        {"source_b", 4U, 24U},
        {"branch_a", 32U, 14U},
        {"branch_b", 32U, 18U},
        {"confluence", 40U, 16U},
        {"trunk", 54U, 16U},
        {"outlet", 64U, 16U},
    }};
    struct Resolution {
        std::uint32_t width;
        std::uint32_t height;
        float cell_size_m;
    };
    constexpr std::array<Resolution, 3U> resolutions{{
        {kFluid25DSustainedHeadwatersGridWidth, kFluid25DSustainedHeadwatersGridHeight,
         kFluid25DSustainedHeadwatersCellSizeM},
        {kFluid25DSustainedHeadwatersGridWidth2m, kFluid25DSustainedHeadwatersGridHeight2m,
         kFluid25DSustainedHeadwatersCellSize2m},
        {kFluid25DSustainedHeadwatersGridWidth1m, kFluid25DSustainedHeadwatersGridHeight1m,
         kFluid25DSustainedHeadwatersCellSize1m},
    }};

    Fluid25DConfig config;
    std::vector<float> depth_m;
    std::vector<Fluid25DVelocityGpu> velocity;
    for (const Resolution& resolution : resolutions) {
        config.scenario = Fluid25DScenario::SustainedHeadwatersDemo;
        config.solver = Fluid25DSolver::FiniteVolume;
        config.grid_width = resolution.width;
        config.grid_height = resolution.height;
        config.cell_size_m = resolution.cell_size_m;
        validate_fluid_25d_config(config);
        const std::size_t cell_count = fluid_25d_cell_count(config);
        depth_m.resize(cell_count);
        velocity.resize(cell_count);
        for (std::size_t index = 0U; index < cell_count; ++index) {
            depth_m[index] = 0.1F + (0.0001F * static_cast<float>(index));
            velocity[index].velocity_wet = {0.01F * static_cast<float>(index),
                                            -0.02F * static_cast<float>(index), 1.0F, 0.0F};
        }

        const auto stations =
            compute_fluid_25d_sustained_headwaters_stations(config, depth_m, velocity);
        for (std::size_t station_index = 0U; station_index < stations.size(); ++station_index) {
            const auto& station = stations[station_index];
            const auto& expected_station = expected[station_index];
            const std::uint32_t expected_x_cell = static_cast<std::uint32_t>(std::lround(
                static_cast<float>(expected_station.x_reference_cell) * 4.0F / config.cell_size_m));
            const std::uint32_t expected_y_cell = static_cast<std::uint32_t>(std::lround(
                static_cast<float>(expected_station.y_reference_cell) * 4.0F / config.cell_size_m));
            require(
                station.name == expected_station.name && station.x_cell == expected_x_cell &&
                    station.y_cell == expected_y_cell,
                "headwater diagnostic stations should retain their names and physical locations");
            const std::size_t cell_index = fluid_25d_scenario_index(
                config.grid_width, config.grid_height, expected_x_cell, expected_y_cell);
            require_close(
                station.depth_m, depth_m[cell_index], kDepthToleranceM,
                "headwater stations should sample synthetic depth at the configured cell");
            require_close(
                station.velocity_x_m_per_s, velocity[cell_index].velocity_wet[0], kDepthToleranceM,
                "headwater stations should sample synthetic x velocity at the configured cell");
            require_close(
                station.velocity_y_m_per_s, velocity[cell_index].velocity_wet[1], kDepthToleranceM,
                "headwater stations should sample synthetic y velocity at the configured cell");
        }
    }

    Fluid25DConfig wrong_scenario = config;
    wrong_scenario.scenario = Fluid25DScenario::DryBed;
    require_throws(
        [&] {
            static_cast<void>(
                compute_fluid_25d_sustained_headwaters_stations(wrong_scenario, depth_m, velocity));
        },
        "headwater station diagnostics should reject a different scenario");
    std::vector<float> short_depth = depth_m;
    short_depth.pop_back();
    require_throws(
        [&] {
            static_cast<void>(
                compute_fluid_25d_sustained_headwaters_stations(config, short_depth, velocity));
        },
        "headwater station diagnostics should reject a short depth field");
    std::vector<Fluid25DVelocityGpu> short_velocity = velocity;
    short_velocity.pop_back();
    require_throws(
        [&] {
            static_cast<void>(
                compute_fluid_25d_sustained_headwaters_stations(config, depth_m, short_velocity));
        },
        "headwater station diagnostics should reject a short velocity field");
}

void test_sustained_headwaters_cross_section_diagnostics() {
    using namespace cubey::projects::fluid::fluid_25d;
    struct Resolution {
        std::uint32_t width;
        std::uint32_t height;
        float cell_size_m;
    };
    constexpr std::array<Resolution, 3U> resolutions{{
        {kFluid25DSustainedHeadwatersGridWidth, kFluid25DSustainedHeadwatersGridHeight,
         kFluid25DSustainedHeadwatersCellSizeM},
        {kFluid25DSustainedHeadwatersGridWidth2m, kFluid25DSustainedHeadwatersGridHeight2m,
         kFluid25DSustainedHeadwatersCellSize2m},
        {kFluid25DSustainedHeadwatersGridWidth1m, kFluid25DSustainedHeadwatersGridHeight1m,
         kFluid25DSustainedHeadwatersCellSize1m},
    }};

    Fluid25DConfig config;
    config.scenario = Fluid25DScenario::SustainedHeadwatersDemo;
    config.solver = Fluid25DSolver::FiniteVolume;
    for (const Resolution& resolution : resolutions) {
        config.grid_width = resolution.width;
        config.grid_height = resolution.height;
        config.cell_size_m = resolution.cell_size_m;
        validate_fluid_25d_config(config);
        const Fluid25DScenarioData scenario = make_fluid_25d_scenario(
            config.scenario, config.grid_width, config.grid_height, config.cell_size_m);
        std::vector<float> depth(fluid_25d_cell_count(config), 0.0F);
        std::vector<Fluid25DVelocityGpu> velocity(depth.size());
        const auto dry_corridor =
            compute_fluid_25d_sustained_headwaters_corridor_diagnostics(config, depth);
        require(dry_corridor.diagnostics_valid == 1.0 &&
                    dry_corridor.endpoint_regions_included == 1.0 &&
                    dry_corridor.sampled_domain_coverage_fraction == 1.0 &&
                    dry_corridor.sampled_cell_count == depth.size() &&
                    dry_corridor.outside_authored_corridor_wet_cell_count == 0U &&
                    dry_corridor.outside_authored_corridor_water_volume_m3 == 0.0,
                "dry authored-corridor diagnostic should report full-domain valid coverage");
        std::vector<float> corridor_fixture_depth(depth.size(), 0.0F);
        const std::size_t source_a_index = fluid_25d_scenario_index(
            config.grid_width, config.grid_height,
            static_cast<std::uint32_t>(std::lround(kFluid25DSustainedHeadwatersSourceAX *
                                                   kFluid25DSustainedHeadwatersCellSizeM /
                                                   config.cell_size_m)),
            static_cast<std::uint32_t>(std::lround(kFluid25DSustainedHeadwatersSourceAY *
                                                   kFluid25DSustainedHeadwatersCellSizeM /
                                                   config.cell_size_m)));
        const std::size_t far_endpoint_index = fluid_25d_scenario_index(
            config.grid_width, config.grid_height, config.grid_width - 1U, 0U);
        corridor_fixture_depth[source_a_index] = 0.20F;
        corridor_fixture_depth[far_endpoint_index] = 0.15F;
        const auto corridor_fixture = compute_fluid_25d_sustained_headwaters_corridor_diagnostics(
            config, corridor_fixture_depth);
        require(corridor_fixture.diagnostics_valid == 1.0 &&
                    corridor_fixture.endpoint_regions_included == 1.0 &&
                    corridor_fixture.outside_authored_corridor_wet_cell_count == 1U,
                "whole-grid corridor tally should retain only water beyond the authored route, "
                "including edge endpoints");
        require_close(corridor_fixture.outside_authored_corridor_water_volume_m3,
                      0.15 * config.cell_size_m * config.cell_size_m, kDepthToleranceM,
                      "whole-grid corridor water volume should be cell-area weighted");
        const auto stations = fluid_25d_sustained_headwaters_cross_section_stations(config);
        require(stations[0].name == "branch_a_interior" &&
                    stations[1].name == "branch_b_interior" &&
                    stations[2].name == "trunk_interior" && !stations[0].endpoint_section &&
                    !stations[1].endpoint_section && !stations[2].endpoint_section &&
                    stations[3].name == "source_a_endpoint" &&
                    stations[4].name == "source_b_endpoint" &&
                    stations[5].name == "outlet_near_endpoint" &&
                    stations[6].name == "outlet_edge_endpoint" && stations[3].endpoint_section &&
                    stations[4].endpoint_section && stations[5].endpoint_section &&
                    stations[6].endpoint_section,
                "headwaters cross sections should separate interior stations from source/outlet "
                "endpoints");
        require(stations[0].tangent_y > 0.0 && stations[1].tangent_y < 0.0 &&
                    std::abs(stations[2].tangent_y) < 1.0e-12,
                "headwaters branch transects should retain their authored angled directions");
        for (std::size_t station_index = 0U; station_index < stations.size(); ++station_index) {
            const auto dry = compute_fluid_25d_sustained_headwaters_cross_section_diagnostics(
                config, stations[station_index], scenario.terrain_height_m, depth, velocity);
            require(
                dry.section_geometry_valid == 1.0 && dry.bank_search_coverage_fraction >= 0.999 &&
                    dry.negative_bank_crest_valid == 1.0 && dry.positive_bank_crest_valid == 1.0 &&
                    dry.bankfull_capacity_valid == 1.0 && dry.overbank_valid == 1.0,
                "authored headwaters transects should sample both actual bank crests with full "
                "coverage");
            require(dry.wetted_cell_count == 0U && dry.overbank_wet_cell_count == 0U &&
                        dry.freeboard_valid == 0.0 && dry.froude_valid == 0.0 &&
                        dry.section_water_area_m2 == 0.0 &&
                        dry.overbank_water_volume_one_section_estimate_m3 == 0.0,
                    "dry headwaters transects should keep water metrics finite and invalid where "
                    "unwetted");
            require(dry.bankfull_capacity_area_m2 > 0.0 && dry.bankfull_fraction == 0.0,
                    "dry headwaters transects should retain terrain-derived bankfull capacity");
        }

        const auto source_endpoint =
            compute_fluid_25d_sustained_headwaters_cross_section_diagnostics(
                config, stations[3], scenario.terrain_height_m, depth, velocity);
        require(source_endpoint.endpoint_section && source_endpoint.section_geometry_valid == 1.0,
                "source-adjacent section should remain separately valid endpoint evidence");
        const auto outlet_edge = compute_fluid_25d_sustained_headwaters_cross_section_diagnostics(
            config, stations[6], scenario.terrain_height_m, depth, velocity);
        require(outlet_edge.endpoint_section && outlet_edge.section_geometry_valid == 1.0 &&
                    std::abs(outlet_edge.center_x_m - static_cast<double>(config.grid_width - 1U) *
                                                          config.cell_size_m) < 1.0e-9,
                "outlet-edge section should sample the true boundary separately from the "
                "near-outlet section");

        if (resolution.cell_size_m == kFluid25DSustainedHeadwatersCellSizeM) {
            std::vector<float> opposite_branch_depth(depth.size(), 0.0F);
            std::vector<Fluid25DVelocityGpu> opposite_branch_velocity(depth.size());
            constexpr double source_x_m =
                kFluid25DSustainedHeadwatersSourceBX * kFluid25DSustainedHeadwatersCellSizeM;
            constexpr double source_y_m =
                kFluid25DSustainedHeadwatersSourceBY * kFluid25DSustainedHeadwatersCellSizeM;
            constexpr double confluence_x_m =
                kFluid25DSustainedHeadwatersConfluenceX * kFluid25DSustainedHeadwatersCellSizeM;
            constexpr double confluence_y_m =
                kFluid25DSustainedHeadwatersConfluenceY * kFluid25DSustainedHeadwatersCellSizeM;
            const double branch_dx_m = confluence_x_m - source_x_m;
            const double branch_dy_m = confluence_y_m - source_y_m;
            const double branch_length_squared_m2 =
                branch_dx_m * branch_dx_m + branch_dy_m * branch_dy_m;
            std::uint64_t opposite_branch_cells_in_section = 0U;
            for (std::uint32_t y = 0U; y < config.grid_height; ++y) {
                for (std::uint32_t x = 0U; x < config.grid_width; ++x) {
                    const std::size_t index =
                        fluid_25d_scenario_index(config.grid_width, config.grid_height, x, y);
                    const double x_m = static_cast<double>(x) * config.cell_size_m;
                    const double y_m = static_cast<double>(y) * config.cell_size_m;
                    const double branch_t = std::clamp(
                        ((x_m - source_x_m) * branch_dx_m + (y_m - source_y_m) * branch_dy_m) /
                            branch_length_squared_m2,
                        0.0, 1.0);
                    const double branch_offset_x_m = x_m - (source_x_m + branch_t * branch_dx_m);
                    const double branch_offset_y_m = y_m - (source_y_m + branch_t * branch_dy_m);
                    if (std::hypot(branch_offset_x_m, branch_offset_y_m) <= 6.0) {
                        opposite_branch_depth[index] = 0.20F;
                    }
                    const double station_offset_x_m = x_m - stations[0].center_x_m;
                    const double station_offset_y_m = y_m - stations[0].center_y_m;
                    const double station_along_m = station_offset_x_m * stations[0].tangent_x +
                                                   station_offset_y_m * stations[0].tangent_y;
                    const double station_lateral_m = station_offset_x_m * -stations[0].tangent_y +
                                                     station_offset_y_m * stations[0].tangent_x;
                    if (std::abs(station_along_m) <= 0.5 * config.cell_size_m + 1.0e-9 &&
                        opposite_branch_depth[index] > config.minimum_wet_depth_m) {
                        ++opposite_branch_cells_in_section;
                        const double local_corridor_radius_m =
                            stations[0].half_width_m + 12.0 + (0.5 * stations[0].half_width_m);
                        require(std::abs(station_lateral_m) > local_corridor_radius_m,
                                "the opposite branch should lie outside the local spill corridor");
                    }
                }
            }
            const auto branch_a_with_opposite_branch_wet =
                compute_fluid_25d_sustained_headwaters_cross_section_diagnostics(
                    config, stations[0], scenario.terrain_height_m, opposite_branch_depth,
                    opposite_branch_velocity);
            const auto opposite_branch_corridor =
                compute_fluid_25d_sustained_headwaters_corridor_diagnostics(config,
                                                                            opposite_branch_depth);
            require(opposite_branch_cells_in_section > 0U &&
                        branch_a_with_opposite_branch_wet.overbank_valid == 1.0 &&
                        branch_a_with_opposite_branch_wet.overbank_wet_cell_count == 0U &&
                        branch_a_with_opposite_branch_wet
                                .overbank_water_volume_one_section_estimate_m3 == 0.0 &&
                        opposite_branch_corridor.outside_authored_corridor_wet_cell_count == 0U,
                    "water on the opposite branch should not be attributed as local branch spill");
        }
    }

    config.grid_width = kFluid25DSustainedHeadwatersGridWidth;
    config.grid_height = kFluid25DSustainedHeadwatersGridHeight;
    config.cell_size_m = kFluid25DSustainedHeadwatersCellSizeM;
    config.minimum_wet_depth_m = 0.01F;
    const auto stations = fluid_25d_sustained_headwaters_cross_section_stations(config);
    const auto& station = stations[0];
    const std::size_t cell_count = fluid_25d_cell_count(config);
    std::vector<float> terrain(cell_count, 0.0F);
    std::vector<float> depth(cell_count, 0.0F);
    std::vector<Fluid25DVelocityGpu> velocity(cell_count);
    const double cell_size_m = config.cell_size_m;
    const double normal_x = -station.tangent_y;
    const double normal_y = station.tangent_x;
    const double half_longitudinal_m =
        0.5 * cell_size_m * (std::abs(station.tangent_x) + std::abs(station.tangent_y));
    const double lateral_sample_width_m = cell_size_m / (std::abs(normal_x) + std::abs(normal_y));
    const auto coordinates = [&](std::uint32_t x, std::uint32_t y) {
        const double offset_x_m = static_cast<double>(x) * cell_size_m - station.center_x_m;
        const double offset_y_m = static_cast<double>(y) * cell_size_m - station.center_y_m;
        const double along_m = offset_x_m * station.tangent_x + offset_y_m * station.tangent_y;
        const double lateral_m = offset_x_m * normal_x + offset_y_m * normal_y;
        return std::array<double, 2U>{along_m, lateral_m};
    };
    for (std::uint32_t y = 0U; y < config.grid_height; ++y) {
        for (std::uint32_t x = 0U; x < config.grid_width; ++x) {
            const std::size_t index =
                fluid_25d_scenario_index(config.grid_width, config.grid_height, x, y);
            const auto [along_m, lateral_m] = coordinates(x, y);
            const double distance_m = std::abs(lateral_m);
            terrain[index] = static_cast<float>(
                distance_m <= 18.0 ? std::min(1.0, distance_m / 18.0)
                                   : std::max(0.40, 1.0 - (0.05 * (distance_m - 18.0))));
            if (std::abs(along_m) <= half_longitudinal_m + 1.0e-9 && std::abs(lateral_m) < 4.0) {
                depth[index] = 0.20F;
                velocity[index].velocity_wet = {static_cast<float>(2.0 * station.tangent_x),
                                                static_cast<float>(2.0 * station.tangent_y), 1.0F,
                                                0.0F};
            }
        }
    }

    const auto filled = compute_fluid_25d_sustained_headwaters_cross_section_diagnostics(
        config, station, terrain, depth, velocity);
    std::uint64_t expected_wet_cells = 0U;
    for (std::uint32_t y = 0U; y < config.grid_height; ++y) {
        for (std::uint32_t x = 0U; x < config.grid_width; ++x) {
            const auto [along_m, lateral_m] = coordinates(x, y);
            if (std::abs(along_m) <= half_longitudinal_m + 1.0e-9 && std::abs(lateral_m) < 4.0) {
                ++expected_wet_cells;
            }
        }
    }
    require(filled.section_geometry_valid == 1.0 &&
                filled.wetted_cell_count == expected_wet_cells && filled.wetted_cell_count > 0U &&
                filled.freeboard_valid == 1.0 && filled.bankfull_capacity_valid == 1.0 &&
                filled.froude_valid == 1.0,
            "filled angled transect should expose valid local section and flow estimates");
    require_close(filled.section_water_area_m2,
                  static_cast<double>(expected_wet_cells) * 0.20 * lateral_sample_width_m,
                  kDepthToleranceM,
                  "angled transect section area should integrate only its normal-plane wet cells");
    require_close(
        filled.depth_velocity_discharge_estimate_m3_per_s, filled.section_water_area_m2 * 2.0,
        kDepthToleranceM,
        "angled transect discharge estimate should project velocity onto the branch direction");
    require_close(filled.mean_longitudinal_velocity_m_per_s, 2.0, kDepthToleranceM,
                  "angled transect mean flow speed should use the branch tangent component");
    require_close(filled.froude_estimate,
                  2.0 / std::sqrt(static_cast<double>(config.gravity_m_per_s2) * 0.20),
                  kDepthToleranceM,
                  "angled transect Froude estimate should stay finite at shallow wet depths");
    require(filled.bankfull_fraction > 0.0 && filled.bankfull_fraction < 1.0 &&
                std::isfinite(filled.freeboard_m),
            "known in-bank water should produce a finite freeboard and partial bankfull fraction");

    std::fill(depth.begin(), depth.end(), 0.0F);
    std::uint64_t expected_overbank_cells = 0U;
    for (std::uint32_t y = 0U; y < config.grid_height; ++y) {
        for (std::uint32_t x = 0U; x < config.grid_width; ++x) {
            const std::size_t index =
                fluid_25d_scenario_index(config.grid_width, config.grid_height, x, y);
            const auto [along_m, lateral_m] = coordinates(x, y);
            const bool in_station_slab = std::abs(along_m) <= half_longitudinal_m + 1.0e-9;
            if (in_station_slab && std::abs(lateral_m) <= filled.overbank_corridor_radius_m &&
                (lateral_m < filled.negative_bank_crest_lateral_m ||
                 lateral_m > filled.positive_bank_crest_lateral_m)) {
                depth[index] = 0.15F;
                ++expected_overbank_cells;
            }
        }
    }
    const auto overbank = compute_fluid_25d_sustained_headwaters_cross_section_diagnostics(
        config, station, terrain, depth, velocity);
    require(overbank.overbank_valid == 1.0 &&
                overbank.overbank_wet_cell_count == expected_overbank_cells &&
                overbank.overbank_wet_cell_count > 0U,
            "water outside the sampled bank crests should count as local overbank wet cells");
    require_close(overbank.overbank_water_volume_one_section_estimate_m3,
                  static_cast<double>(expected_overbank_cells) * 0.15 * lateral_sample_width_m *
                      cell_size_m,
                  kDepthToleranceM,
                  "overbank volume should estimate only the sampled one-section station slab");
    require(overbank.freeboard_valid == 0.0 && overbank.froude_valid == 0.0,
            "overbank-only water should not create an in-bank freeboard or Froude estimate");

    const auto endpoint = fluid_25d_sustained_headwaters_cross_section_stations(config)[6];
    const auto endpoint_diagnostics =
        compute_fluid_25d_sustained_headwaters_cross_section_diagnostics(config, endpoint, terrain,
                                                                         depth, velocity);
    cubey::profiling::ProfileRecorder recorder({
        .output_prefix =
            std::filesystem::temp_directory_path() / "cubey-fluid-25d-headwaters-profile",
        .warmup_frames = 0U,
    });
    record_fluid_25d_sustained_headwaters_cross_section_diagnostics(recorder, 0U, filled);
    record_fluid_25d_sustained_headwaters_cross_section_diagnostics(recorder, 0U,
                                                                    endpoint_diagnostics);
    std::vector<float> corridor_record_depth(cell_count, 0.0F);
    corridor_record_depth[fluid_25d_scenario_index(config.grid_width, config.grid_height,
                                                   config.grid_width - 1U, 0U)] = 0.15F;
    const auto corridor_record =
        compute_fluid_25d_sustained_headwaters_corridor_diagnostics(config, corridor_record_depth);
    record_fluid_25d_sustained_headwaters_corridor_diagnostics(recorder, 0U, corridor_record);
    const auto metrics = recorder.metric_records();
    const auto has_metric = [&metrics](std::string_view category, std::string_view name,
                                       double expected_value) {
        return std::any_of(metrics.begin(), metrics.end(), [&](const auto& metric) {
            return metric.category == category && metric.name == name &&
                   std::abs(metric.value - expected_value) < 1.0e-9;
        });
    };
    require(
        has_metric("fluid_25d.headwaters_cross_section",
                   "station.branch_a_interior.bankfull_capacity_valid", 1.0) &&
            has_metric("fluid_25d.headwaters_endpoint_section",
                       "station.outlet_edge_endpoint.endpoint_section", 1.0) &&
            has_metric("fluid_25d.headwaters_authored_corridor",
                       "outside_authored_corridor_wet_cell_count", 1.0) &&
            has_metric("fluid_25d.headwaters_authored_corridor", "endpoint_regions_included", 1.0),
        "profile recording should separate interior sections, endpoint sections, and corridor "
        "spill");

    Fluid25DConfig unrelated = config;
    unrelated.scenario = Fluid25DScenario::DryBed;
    require_throws(
        [&] {
            static_cast<void>(fluid_25d_sustained_headwaters_cross_section_stations(unrelated));
        },
        "headwaters cross-section station selection should reject other scenarios");
    require_throws(
        [&] {
            static_cast<void>(
                compute_fluid_25d_sustained_headwaters_corridor_diagnostics(unrelated, depth));
        },
        "headwaters authored-corridor diagnostics should reject other scenarios");
    require_throws(
        [&] {
            static_cast<void>(compute_fluid_25d_sustained_headwaters_cross_section_diagnostics(
                config, station, terrain, std::span<const float>(depth).first(1U), velocity));
        },
        "headwaters cross-section diagnostics should reject mismatched field dimensions");
    require_throws(
        [&] {
            static_cast<void>(compute_fluid_25d_sustained_headwaters_corridor_diagnostics(
                config, std::span<const float>(depth).first(1U)));
        },
        "headwaters authored-corridor diagnostics should reject mismatched depth dimensions");
    auto malformed_station = station;
    malformed_station.tangent_x = 2.0;
    require_throws(
        [&] {
            static_cast<void>(compute_fluid_25d_sustained_headwaters_cross_section_diagnostics(
                config, malformed_station, terrain, depth, velocity));
        },
        "headwaters cross-section diagnostics should reject non-unit transect directions");
}

void test_hillside_spatial_diagnostics() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config = natural_flow_test_config();
    config.scenario = Fluid25DScenario::HillsideFlowStudy;
    config.grid_width = 11U;
    config.grid_height = 11U;
    auto scenario = make_fluid_25d_scenario(Fluid25DScenario::DryBed, 11U, 11U, 30.0F);
    scenario.source_cell = 60U;
    scenario.natural_flow_study = Fluid25DNaturalFlowStudyMetadata{};
    scenario.natural_flow_study->has_expected_outlet = false;
    scenario.natural_flow_study->source_cells = {60U, 59U, 61U, 49U, 71U};
    std::vector<float> depth(121U, 0.0F);
    std::vector<Fluid25DVelocityGpu> velocity(121U);
    auto empty = compute_fluid_25d_hillside_spatial(config, scenario, depth, velocity);
    require(empty.minimum_material_edge_distance_m == -1.0 &&
                empty.source_connected_material_wet_cells == 0U,
            "dry hillside spatial evidence must not fabricate an edge or connected water");
    depth[60U] = 0.1F;
    depth[61U] = 0.2F;
    depth[62U] = 0.009F; // Sub-material bridge cannot join a separate wet island.
    depth[73U] = 0.3F;   // Only diagonal to 61.
    depth[0U] = 0.4F;
    velocity[60U].velocity_wet[0] = 0.05F;
    velocity[61U].velocity_wet[0] = kFluid25DSlowPooledSpeedThresholdMPerS;
    velocity[73U].velocity_wet[1] = 0.1F;
    const auto initial_depth = depth;
    const auto initial_terrain = scenario.terrain_height_m;
    auto result = compute_fluid_25d_hillside_spatial(config, scenario, depth, velocity);
    require(result.source_connected_material_wet_cells == 2U &&
                result.material_active_flow_cells == 2U &&
                result.minimum_material_edge_distance_m == 0.0 &&
                result.material_edge_band_wet_cells == 3U,
            "spatial classifications must separate diagonal islands, slow cells and perimeter "
            "proximity");
    require_close(result.source_connected_material_water_volume_m3, 270.0, 1e-4,
                  "source-connected storage is actual depth times native cell area");
    require_close(result.material_water_volume_m3, 900.0, 1e-4,
                  "material volume excludes thin numerical tails");
    require_close(result.material_active_water_volume_m3 + result.material_slow_water_volume_m3,
                  result.material_water_volume_m3, 1e-8,
                  "moving and slow volume classifications must partition material water");
    require(depth == initial_depth && scenario.terrain_height_m == initial_terrain,
            "spatial observation must not alter water or terrain");
    velocity[73U].velocity_wet[0] = std::numeric_limits<float>::infinity();
    require_throws(
        [&] {
            static_cast<void>(
                compute_fluid_25d_hillside_spatial(config, scenario, depth, velocity));
        },
        "hillside spatial evidence rejects invalid velocity");
    velocity[73U].velocity_wet[0] = 0.0F;
    scenario.natural_flow_study->source_cells.push_back(121U);
    require_throws(
        [&] {
            static_cast<void>(
                compute_fluid_25d_hillside_spatial(config, scenario, depth, velocity));
        },
        "hillside spatial evidence rejects out-of-bounds sources");
}

void test_natural_flow_profile_diagnostics() {
    using namespace cubey::projects::fluid::fluid_25d;
    const Fluid25DConfig config = natural_flow_test_config();
    Fluid25DNaturalFlowStudyMetadata study;
    study.expected_outlet_edge = Fluid25DNaturalFlowEdge::West;
    study.expected_outlet_x_min = 0U;
    study.expected_outlet_z_min = 2U;
    study.expected_outlet_x_max = 0U;
    study.expected_outlet_z_max = 2U;
    study.gauges.push_back({.name = "upstream",
                            .x_cell = 2U,
                            .z_cell = 2U,
                            .tangent_dx = -1,
                            .tangent_dz = 0,
                            .distance_m = 0.0,
                            .half_span_cells = 1U});
    std::vector<float> depth(fluid_25d_cell_count(config), 0.0F);
    std::vector<float> tracer_q(depth.size(), 0.0F);
    std::vector<Fluid25DVelocityGpu> velocity(depth.size());
    for (const std::uint32_t z : {1U, 2U, 3U}) {
        const std::size_t index = fluid_25d_scenario_index(5U, 5U, 2U, z);
        depth[index] = z == 2U ? 2.0F : 1.0F;
        tracer_q[index] = 0.1F;
        velocity[index].velocity_wet[0] = -0.5F;
    }
    const auto gauges =
        compute_fluid_25d_natural_flow_gauge_diagnostics(config, study, depth, velocity, tracer_q);
    require(gauges.size() == 1U && gauges[0].name == "upstream" &&
                gauges[0].wetted_cell_count == 3U,
            "natural-flow gauges should sample the recipe's exact perpendicular transect");
    require_close(gauges[0].center_depth_m, 2.0, kDepthToleranceM,
                  "natural-flow gauge should report center depth from the sampled state");
    require_close(gauges[0].section_water_area_m2, 120.0, kDepthToleranceM,
                  "natural-flow section area should integrate depth across sample spacing");
    require_close(
        gauges[0].depth_velocity_discharge_estimate_m3_per_s, 60.0, kDepthToleranceM,
        "natural-flow depth-velocity discharge estimate should use signed tangent velocity");
    require_close(gauges[0].mean_longitudinal_velocity_m_per_s, 0.5, kDepthToleranceM,
                  "natural-flow mean longitudinal velocity should be depth weighted");
    require_close(gauges[0].sampled_tracer_amount_m3, 270.0, kVolumeToleranceM3,
                  "natural-flow gauge tracer amount should sum sampled q times cell area");
    for (Fluid25DVelocityGpu& state : velocity) {
        state.velocity_wet[0] = 0.5F;
    }
    const auto reversed_gauges =
        compute_fluid_25d_natural_flow_gauge_diagnostics(config, study, depth, velocity, tracer_q);
    require_close(reversed_gauges[0].depth_velocity_discharge_estimate_m3_per_s, -60.0,
                  kDepthToleranceM,
                  "natural-flow gauge discharge estimate should preserve the route-relative sign");
    require_close(reversed_gauges[0].mean_longitudinal_velocity_m_per_s, -0.5, kDepthToleranceM,
                  "natural-flow mean longitudinal velocity should preserve its tangent sign");

    std::vector<Fluid25DLedgerGpu> ledger(depth.size());
    const auto boundary_volume = [&ledger](std::uint32_t x, std::uint32_t z, float value) {
        ledger[fluid_25d_scenario_index(5U, 5U, x, z)].source_sink_boundary_reserved_m3[2] = value;
    };
    boundary_volume(0U, 2U, 2.0F);
    boundary_volume(0U, 1U, 3.0F);
    boundary_volume(4U, 2U, 4.0F);
    boundary_volume(0U, 0U, 5.0F);
    boundary_volume(2U, 2U, 6.0F);
    const Fluid25DNaturalFlowBoundaryLedgerDiagnostics boundary =
        compute_fluid_25d_natural_flow_boundary_ledger_diagnostics(config, study, ledger, 20.0);
    require_close(boundary.expected_window_outflow_m3, 2.0, kDepthToleranceM,
                  "natural-flow boundary ledger should attribute its recipe window");
    require_close(boundary.other_noncorner_edge_outflow_m3, 7.0, kDepthToleranceM,
                  "natural-flow boundary ledger should retain unrelated open edges");
    require_close(boundary.corner_outflow_m3, 5.0, kDepthToleranceM,
                  "natural-flow boundary ledger should classify corners separately");
    require_close(boundary.non_edge_outflow_m3, 6.0, kDepthToleranceM,
                  "natural-flow boundary ledger should expose non-edge ledger volume");
    require_close(boundary.all_boundary_outflow_m3, 20.0, kDepthToleranceM,
                  "natural-flow boundary category should conserve the existing all-edge ledger");
    require_throws(
        [&] {
            static_cast<void>(compute_fluid_25d_natural_flow_boundary_ledger_diagnostics(
                config, study, ledger, 21.0));
        },
        "natural-flow boundary attribution should reject mismatch with the general ledger");
}

void test_profile_frame_slot_attribution() {
    using namespace cubey::projects::fluid::fluid_25d;
    const cubey::ProjectFrame first_frame{.frame_index = 1U};
    const cubey::render::FrameSlot first_slot{.index = 0U, .count = 2U};
    require(profile_frame_index(first_frame) == 0U,
            "profile frame indexing should convert runtime frame one to profile frame zero");
    require(collected_profile_frame_index(first_frame, first_slot) == 0U,
            "first collected frame should retain profile frame zero attribution");

    const cubey::ProjectFrame delayed_frame{.frame_index = 4U};
    const cubey::render::FrameSlot delayed_slot{.index = 1U, .count = 2U};
    require(collected_profile_frame_index(delayed_frame, delayed_slot) == 1U,
            "collected GPU timing should attribute the completed slot to its delayed frame");
}

void test_profile_diagnostic_metric_math() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config = test_config(2U, 2U, Fluid25DScenario::TerrainCase);
    config.cell_size_m = 2.0F;
    config.minimum_wet_depth_m = 0.10F;
    const std::array<float, 4> depth{0.05F, 0.20F, 0.40F, 0.10F};
    const std::array<Fluid25DVelocityGpu, 4> velocity{{
        {.velocity_wet = {0.0F, 0.0F, 0.0F, 0.0F}},
        {.velocity_wet = {0.01F, 0.0F, 1.0F, 0.0F}},
        {.velocity_wet = {0.03F, 0.04F, 1.0F, 0.0F}},
        {.velocity_wet = {0.0F, 0.0F, 0.0F, 0.0F}},
    }};
    const std::array<Fluid25DLedgerGpu, 4> ledger{{
        {.source_sink_boundary_reserved_m3 = {0.25F, 0.0F, 0.0F, 0.0F}},
        {.source_sink_boundary_reserved_m3 = {0.75F, 0.25F, 0.10F, 0.0F}},
        {.source_sink_boundary_reserved_m3 = {0.0F, 0.0F, 0.15F, 0.0F}},
        {.source_sink_boundary_reserved_m3 = {0.0F, 0.0F, 0.0F, 0.0F}},
    }};
    const Fluid25DProfileDiagnostics diagnostics =
        compute_fluid_25d_profile_diagnostics(config, depth, velocity, ledger, 2.0);
    require(diagnostics.wet_cell_count == 2U && diagnostics.active_flow_cell_count == 1U,
            "profile diagnostics should classify wet and active cells from physical thresholds");
    require_close(diagnostics.total_water_volume_m3, 3.0, kDepthToleranceM,
                  "profile diagnostics should integrate stored volume by physical cell area");
    require_close(diagnostics.wet_cell_ratio, 0.5, kDepthToleranceM,
                  "profile diagnostics should compute wet ratio");
    require_close(diagnostics.wet_mean_depth_m, 0.3, kDepthToleranceM,
                  "profile diagnostics should compute wet mean depth");
    require_close(diagnostics.maximum_speed_m_per_s, 0.05, kDepthToleranceM,
                  "profile diagnostics should compute maximum wet speed");
    require_close(diagnostics.active_mean_speed_m_per_s, 0.05, kDepthToleranceM,
                  "profile diagnostics should compute active mean speed");
    require_close(diagnostics.slow_pooled_wet_fraction, 0.5, kDepthToleranceM,
                  "profile diagnostics should classify slow wet water as pooled");
    require_close(diagnostics.cumulative_source_volume_m3, 1.0, kDepthToleranceM,
                  "profile diagnostics should sum source ledger volume");
    require_close(diagnostics.cumulative_sink_volume_m3, 0.25, kDepthToleranceM,
                  "profile diagnostics should sum sink ledger volume");
    require_close(diagnostics.cumulative_boundary_outflow_volume_m3, 0.25, kDepthToleranceM,
                  "profile diagnostics should sum boundary ledger volume");
    require_close(diagnostics.conservation_residual_m3, 0.5, kDepthToleranceM,
                  "profile diagnostics should expose, not hide, conservation residual");
    require_throws(
        [&] {
            std::array<float, 4> invalid_depth = depth;
            invalid_depth[0] = -0.01F;
            static_cast<void>(compute_fluid_25d_profile_diagnostics(config, invalid_depth, velocity,
                                                                    ledger, 2.0));
        },
        "profile diagnostics should reject invalid readback depth");
}

void test_rain_boundary_outflow_attribution() {
    using namespace cubey::projects::fluid::fluid_25d;
    const Fluid25DConfig config = test_config(32U, 4U, Fluid25DScenario::TerrainCase);
    const std::size_t cells = fluid_25d_cell_count(config);
    const std::vector<float> depth(cells, 0.0F);
    const std::vector<Fluid25DVelocityGpu> velocity(cells);
    std::vector<Fluid25DLedgerGpu> ledger(cells);
    const auto set_outflow = [&](std::uint32_t x, std::uint32_t y, float volume_m3) {
        ledger[static_cast<std::size_t>(y) * config.grid_width + x]
            .source_sink_boundary_reserved_m3[2] = volume_m3;
    };
    set_outflow(2U, 0U, 2.0F);  // north bin 1
    set_outflow(30U, 0U, 6.0F); // north bin 15
    set_outflow(25U, 3U, 3.0F); // south bin 12
    set_outflow(0U, 1U, 4.0F);  // west bin 4
    set_outflow(31U, 2U, 5.0F); // east bin 8
    set_outflow(31U, 0U, 7.0F); // combined corner, not assigned to a side
    const Fluid25DProfileDiagnostics diagnostics =
        compute_fluid_25d_profile_diagnostics(config, depth, velocity, ledger, 0.0);
    require_close(diagnostics.cumulative_boundary_outflow_volume_m3, 27.0, kDepthToleranceM,
                  "rain boundary diagnostic should preserve the total ledger outflow");
    require_close(diagnostics.north_outflow_bins_m3[1], 2.0, kDepthToleranceM,
                  "rain boundary diagnostic should locate north outflow by bin");
    require_close(diagnostics.north_outflow_bins_m3[15], 6.0, kDepthToleranceM,
                  "rain boundary diagnostic should locate the far north outflow bin");
    require_close(diagnostics.south_outflow_bins_m3[12], 3.0, kDepthToleranceM,
                  "rain boundary diagnostic should locate south outflow by bin");
    require_close(diagnostics.west_outflow_bins_m3[4], 4.0, kDepthToleranceM,
                  "rain boundary diagnostic should locate west outflow by bin");
    require_close(diagnostics.east_outflow_bins_m3[8], 5.0, kDepthToleranceM,
                  "rain boundary diagnostic should locate east outflow by bin");
    require_close(diagnostics.corner_outflow_volume_m3, 7.0, kDepthToleranceM,
                  "rain boundary diagnostic should keep ambiguous corners separate");
    require_close(diagnostics.non_edge_outflow_volume_m3, 0.0, kDepthToleranceM,
                  "rain boundary diagnostic should expose unexpected interior outflow");
}

void test_hillside_rain_study_observations() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config = rain_study_test_config(512U, 512U, 36000.0F);
    config.fixed_delta_seconds = 2.0F;
    validate_fluid_25d_config(config);
    Fluid25DRainStudyControl control;
    static_cast<void>(control.prepare_fixed_step(config));
    const std::size_t cells = fluid_25d_cell_count(config);
    std::vector<float> depth(cells, static_cast<float>(control.state().cumulative_depth_m));
    std::vector<Fluid25DVelocityGpu> velocity(cells);
    const auto index = [](std::uint32_t x, std::uint32_t z) {
        return static_cast<std::size_t>(z) * 512U + x;
    };
    const auto mark = [&](std::uint32_t x, std::uint32_t z, float h, float speed) {
        depth[index(x, z)] = h;
        velocity[index(x, z)].velocity_wet = {speed, 0.0F, h, 0.0F};
    };

    for (std::uint32_t z = 203U; z < 224U; ++z) {
        for (std::uint32_t x = 203U; x < 224U; ++x)
            depth[index(x, z)] = 0.04F;
    }
    velocity[index(203U, 203U)].velocity_wet = {0.03F, 0.0F, 0.04F, 0.0F};
    velocity[index(204U, 203U)].velocity_wet = {0.03F, 0.0F, 0.04F, 0.0F};
    for (std::uint32_t x = 10U; x < 16U; ++x)
        mark(x, 10U, 0.035F, 0.03F);
    for (std::uint32_t x = 20U; x < 23U; ++x)
        mark(x, 20U, 0.031F, 0.03F);
    for (std::uint32_t x = 0U; x < 6U; ++x)
        mark(x, 300U, 0.04F, 0.03F);

    const Fluid25DRainStudyObservation observation =
        compute_fluid_25d_rain_study_observation(config, depth, velocity, control, 2.0);
    require(observation.physical_time_s == 2.0 && observation.enabled &&
                std::abs(observation.rate_mm_per_hour - 36000.0) < 0.1 &&
                observation.material_wet_cells == cells &&
                observation.material_active_cells == 17U &&
                observation.converged_moving_cells == 17U,
            "rain observations should use applied forcing and specified wet/moving thresholds");
    require(observation.largest_corridor_cells == 6U &&
                observation.largest_corridor_span_m == 150.0,
            "rain corridor should use interior D4 components and select greatest span");
    require_close(observation.converged_water_volume_m3,
                  (0.04 - observation.cumulative_depth_m) * 900.0 * (441.0 + 6.0) +
                      (0.035 - observation.cumulative_depth_m) * 900.0 * 6.0 +
                      (0.031 - observation.cumulative_depth_m) * 900.0 * 3.0,
                  0.1, "rain converged-water volume should subtract uniform cumulative rain");
    require_close(observation.maximum_depth_m, 0.04, 1.0e-7,
                  "rain maximum depth should report the field maximum");
    require(observation.maximum_depth_cell_x == 203U && observation.maximum_depth_cell_z == 203U,
            "rain observation should deterministically report a useful max-depth cell");
    require(observation.regions[0].name == "upper" && observation.regions[0].valid &&
                observation.regions[1].name == "transit" && observation.regions[1].valid &&
                observation.regions[2].name == "collection" && observation.regions[2].valid,
            "rain observations should expose the three pinned 21x21 regions on a 512 crop");
    for (const auto& region : observation.regions) {
        require_close(region.direct_rain_volume_m3, observation.cumulative_depth_m * 900.0 * 441.0,
                      0.01, "each rain ROI should report its fixed direct-rain volume");
    }
    require_close(observation.regions[0].water_volume_m3, 0.04 * 900.0 * 441.0, 0.1,
                  "upper ROI water volume should sum its fixed footprint");
    require(observation.regions[0].material_wet_cells == 441U &&
                observation.regions[0].active_cells == 2U,
            "rain ROI thresholds should count material wet and active cells independently");
}

void test_source_outlet_cross_section_diagnostic_math() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config =
        finite_volume_test_config(128U, 64U, Fluid25DScenario::SourceOutletDemo);
    config.cell_size_m = 2.0F;
    config.gravity_m_per_s2 = 10.0F;
    config.minimum_wet_depth_m = 0.10F;

    const auto stations =
        fluid_25d_source_outlet_cross_section_stations(config.grid_width, config.grid_height);
    require(stations[0].name == "near_source" && stations[0].x_cell == 14U &&
                stations[1].name == "upstream_reach" && stations[1].x_cell == 38U &&
                stations[2].name == "constriction" && stations[2].x_cell == 70U &&
                stations[3].name == "downstream_reach" && stations[3].x_cell == 91U &&
                stations[4].name == "near_outlet" && stations[4].x_cell == 110U &&
                stations[0].endpoint_pool_affected && !stations[1].endpoint_pool_affected &&
                !stations[2].endpoint_pool_affected && !stations[3].endpoint_pool_affected &&
                stations[4].endpoint_pool_affected,
            "source-outlet section stations should be fixed on the authored route and mark pools");

    const Fluid25DSourceOutletGeometry geometry(config.grid_width, config.grid_height);
    const std::uint32_t x_cell = stations[0].x_cell;
    const float center_y = geometry.channel_center_y(static_cast<float>(x_cell));
    const float half_width = geometry.channel_half_width(static_cast<float>(x_cell));
    const std::uint32_t left_crest_y =
        static_cast<std::uint32_t>(std::lround(center_y - (1.65F * half_width)));
    const std::uint32_t right_crest_y =
        static_cast<std::uint32_t>(std::lround(center_y + (1.65F * half_width)));
    const std::uint32_t centerline_y = static_cast<std::uint32_t>(std::lround(center_y));
    const std::size_t cells = fluid_25d_cell_count(config);
    std::vector<float> terrain(cells, 0.0F);
    std::vector<float> depth(cells, 0.0F);
    std::vector<Fluid25DVelocityGpu> velocity(cells);
    const auto index = [&config, x_cell](std::uint32_t y) {
        return (static_cast<std::size_t>(y) * config.grid_width) + x_cell;
    };
    terrain[index(left_crest_y)] = 5.0F;
    terrain[index(right_crest_y)] = 4.5F;
    terrain[index(centerline_y)] = 1.0F;
    for (std::uint32_t y = left_crest_y; y <= right_crest_y; ++y) {
        depth[index(y)] = 0.20F;
        velocity[index(y)].velocity_wet = {2.0F, 0.0F, 1.0F, 0.0F};
    }
    depth[index(left_crest_y - 1U)] = 0.30F;
    depth[index(right_crest_y + 2U)] = 0.05F;

    const Fluid25DSourceOutletCrossSectionDiagnostics diagnostics =
        compute_fluid_25d_source_outlet_cross_section_diagnostics(config, stations[0], terrain,
                                                                  depth, velocity);
    const double cell_count = static_cast<double>(right_crest_y - left_crest_y + 1U);
    const double expected_area_m2 = cell_count * 0.20 * 2.0;
    const double expected_bankfull_capacity_m2 = (((cell_count - 3.0) * 4.5) + 3.5) * 2.0;
    require(
        diagnostics.left_bank_crest_y_cell == left_crest_y &&
            diagnostics.right_bank_crest_y_cell == right_crest_y &&
            diagnostics.left_bank_crest_fallback == 0U &&
            diagnostics.right_bank_crest_fallback == 0U,
        "cross-section bank crests should select actual terrain maxima in the authored shoulder");
    require(diagnostics.wetted_cell_count == static_cast<std::uint64_t>(cell_count) &&
                diagnostics.overbank_wet_cell_count == 1U,
            "cross-section wet counts should separate bank-to-bank water from overbank cells");
    require_close(diagnostics.station_x_m, 29.0, kDepthToleranceM,
                  "cross-section station x should use the cell-center coordinate");
    require_close(diagnostics.centerline_bed_elevation_m, 1.0, kDepthToleranceM,
                  "cross-section should sample the authored centerline bed cell");
    require_close(diagnostics.lower_bank_crest_elevation_m, 4.5, kDepthToleranceM,
                  "cross-section lower bank should be the lower of actual side crests");
    require_close(diagnostics.wetted_cell_width_m, cell_count * 2.0, kDepthToleranceM,
                  "cross-section wetted width should use only wet in-bank cells");
    require_close(diagnostics.wetted_width_fraction, 1.0, kDepthToleranceM,
                  "cross-section wetted width fraction should divide by bank-to-bank width");
    require_close(diagnostics.section_water_area_m2, expected_area_m2, kDepthToleranceM,
                  "cross-section water area should integrate only wet in-bank cells");
    require_close(diagnostics.mean_wet_depth_m, 0.20, kDepthToleranceM,
                  "cross-section wet mean depth should divide area by wetted width");
    require_close(diagnostics.mean_section_depth_m, 0.20, kDepthToleranceM,
                  "cross-section mean depth should divide area by bank-to-bank width");
    require_close(diagnostics.representative_free_surface_elevation_m, 0.20 + (10.5 / cell_count),
                  kDepthToleranceM,
                  "cross-section free surface should be water-area weighted over wet cells");
    require(diagnostics.representative_free_surface_valid == 1.0 &&
                diagnostics.bankfull_capacity_valid == 1.0,
            "cross-section wet surface and positive bankfull capacity should be marked valid");
    require_close(diagnostics.bankfull_capacity_area_m2, expected_bankfull_capacity_m2,
                  kDepthToleranceM,
                  "bankfull capacity should integrate only between selected crest cells");
    require_close(diagnostics.bankfull_fraction, expected_area_m2 / expected_bankfull_capacity_m2,
                  kDepthToleranceM, "bankfull fraction should compare in-bank area to capacity");
    require_close(diagnostics.depth_velocity_discharge_estimate_m3_per_s, cell_count * 0.8,
                  kDepthToleranceM,
                  "cross-section discharge should estimate sum(h times x velocity times width)");
    require_close(diagnostics.mean_x_velocity_m_per_s, 2.0, kDepthToleranceM,
                  "cross-section mean x velocity should be approximate Q divided by area");
    require_close(diagnostics.froude_estimate, 2.0 / std::sqrt(2.0), kDepthToleranceM,
                  "cross-section Froude estimate should use mean wet hydraulic depth");

    std::fill(depth.begin(), depth.end(), 0.0F);
    const Fluid25DSourceOutletCrossSectionDiagnostics dry =
        compute_fluid_25d_source_outlet_cross_section_diagnostics(config, stations[0], terrain,
                                                                  depth, velocity);
    require(dry.wetted_cell_count == 0U && dry.overbank_wet_cell_count == 0U &&
                dry.representative_free_surface_valid == 0.0 &&
                dry.representative_free_surface_elevation_m == 0.0 &&
                dry.section_water_area_m2 == 0.0 && dry.mean_wet_depth_m == 0.0 &&
                dry.mean_section_depth_m == 0.0 &&
                dry.depth_velocity_discharge_estimate_m3_per_s == 0.0 &&
                dry.mean_x_velocity_m_per_s == 0.0 && dry.froude_estimate == 0.0 &&
                dry.bankfull_fraction == 0.0,
            "dry cross-sections should produce finite zero water metrics and an invalid surface");
    Fluid25DConfig unrelated = config;
    unrelated.scenario = Fluid25DScenario::RiverCatchment;
    require_throws(
        [&] {
            static_cast<void>(compute_fluid_25d_source_outlet_cross_section_diagnostics(
                unrelated, stations[0], terrain, depth, velocity));
        },
        "source-outlet cross-section metrics should reject other scenarios");
}

void test_source_outlet_spatial_diagnostic_math() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config =
        finite_volume_test_config(128U, 64U, Fluid25DScenario::SourceOutletDemo);
    config.cell_size_m = 2.0F;
    config.minimum_wet_depth_m = 0.10F;
    const Fluid25DScenarioData scenario = make_fluid_25d_scenario(
        Fluid25DScenario::SourceOutletDemo, config.grid_width, config.grid_height, 1.0F);
    const Fluid25DSourceOutletGeometry geometry(config.grid_width, config.grid_height);
    const auto cell_index = [&config](std::uint32_t x, std::uint32_t y) {
        return (static_cast<std::size_t>(y) * config.grid_width) + x;
    };

    std::vector<float> depth(fluid_25d_cell_count(config), 0.0F);
    std::vector<float> tracer_q(depth.size(), 0.0F);
    const std::uint32_t before_x = geometry.source_x - 1U;
    const std::uint32_t before_y = config.grid_height / 2U;
    const std::uint32_t off_bank_x = geometry.source_x;
    const auto left_crest =
        fluid_25d_source_outlet_bank_crest(geometry, off_bank_x, scenario.terrain_height_m, true);
    const auto right_crest =
        fluid_25d_source_outlet_bank_crest(geometry, off_bank_x, scenario.terrain_height_m, false);
    const std::uint32_t first_bank_y = std::min(left_crest.y_cell, right_crest.y_cell);
    const std::uint32_t off_bank_y = first_bank_y > 0U
                                         ? first_bank_y - 1U
                                         : std::max(left_crest.y_cell, right_crest.y_cell) + 1U;
    const std::uint32_t in_bank_x = geometry.source_x + (geometry.sink_x - geometry.source_x) / 2U;
    const std::uint32_t in_bank_y = static_cast<std::uint32_t>(
        std::lround(geometry.channel_center_y(static_cast<float>(in_bank_x))));
    const std::uint32_t after_x = geometry.sink_x + 1U;
    const std::uint32_t after_tail_x = geometry.sink_x + 2U;
    const std::uint32_t outer_y = 0U;

    const std::size_t before_index = cell_index(before_x, before_y);
    const std::size_t before_dry_index = cell_index(before_x, before_y + 1U);
    const std::size_t off_bank_index = cell_index(off_bank_x, off_bank_y);
    const std::size_t in_bank_index = cell_index(in_bank_x, in_bank_y);
    const std::size_t after_index = cell_index(after_x, outer_y);
    const std::size_t after_tail_index = cell_index(after_tail_x, outer_y);
    depth[before_index] = 0.20F;
    tracer_q[before_index] = 0.010F;
    depth[before_dry_index] = 0.05F;
    tracer_q[before_dry_index] = 0.010F;
    depth[off_bank_index] = 0.30F;
    tracer_q[off_bank_index] = 0.006F;
    depth[in_bank_index] = 0.40F;
    tracer_q[in_bank_index] = 0.002F;
    depth[after_index] = 0.50F;
    tracer_q[after_index] = 0.010F;
    depth[after_tail_index] = 0.20F;
    tracer_q[after_tail_index] = 0.0001F;

    const Fluid25DSourceOutletSpatialDiagnostics diagnostics =
        compute_fluid_25d_source_outlet_spatial_diagnostics(config, scenario.terrain_height_m,
                                                            depth, tracer_q);
    require(diagnostics.before_source.wet_cell_count == 1U &&
                diagnostics.route_off_bank.wet_cell_count == 1U &&
                diagnostics.route_in_bank.wet_cell_count == 1U &&
                diagnostics.after_outlet.wet_cell_count == 2U,
            "source-outlet spatial zones should classify wet cells into four disjoint regions");
    require(diagnostics.before_source.material_dyed_wet_cell_count == 1U &&
                diagnostics.route_off_bank.material_dyed_wet_cell_count == 1U &&
                diagnostics.route_in_bank.material_dyed_wet_cell_count == 0U &&
                diagnostics.after_outlet.material_dyed_wet_cell_count == 1U,
            "spatial material counts should use wet cells at or above one-percent concentration");
    require_close(diagnostics.before_source.water_volume_m3, 1.0, kDepthToleranceM,
                  "before-source volume should include wet and sub-threshold water cells");
    require_close(diagnostics.route_off_bank.water_volume_m3, 1.2, kDepthToleranceM,
                  "route off-bank water should remain separate from in-bank water");
    require_close(diagnostics.route_in_bank.water_volume_m3, 1.6, kDepthToleranceM,
                  "route in-bank water should integrate only crest-bounded cells");
    require_close(diagnostics.after_outlet.water_volume_m3, 2.8, kDepthToleranceM,
                  "after-outlet tail water should include all rows beyond the sink x");
    require_close(diagnostics.before_source.tracer_amount_m3, 0.08, kDepthToleranceM,
                  "before-source tracer amount should include tracer in sub-threshold water");
    require_close(diagnostics.route_off_bank.tracer_amount_m3, 0.024, kDepthToleranceM,
                  "route off-bank tracer amount should use the matching spatial partition");
    require_close(diagnostics.route_in_bank.tracer_amount_m3, 0.008, kDepthToleranceM,
                  "route in-bank tracer amount should retain sub-material tracer");
    require_close(diagnostics.after_outlet.tracer_amount_m3, 0.0404, kDepthToleranceM,
                  "after-outlet tracer should include both material and low-concentration tails");
    require_close(diagnostics.route_off_bank.maximum_wet_depth_m, 0.30, kDepthToleranceM,
                  "route off-bank maximum should use wet cells only");
    require_close(diagnostics.before_source.maximum_wet_depth_m, 0.20, kDepthToleranceM,
                  "before-source maximum should ignore sub-threshold dry cells");
    require_close(diagnostics.after_outlet.maximum_wet_depth_m, 0.50, kDepthToleranceM,
                  "after-outlet maximum should use wet cells only");
    require(diagnostics.material_dyed_after_outlet_valid &&
                diagnostics.material_dyed_max_x_after_outlet_cell_x == after_x,
            "after-outlet material front should report its furthest wet dyed cell");
    const std::uint32_t route_sections = geometry.sink_x - geometry.source_x + 1U;
    const double single_section_freeboard_m =
        std::min(
            fluid_25d_source_outlet_bank_crest(geometry, in_bank_x, scenario.terrain_height_m, true)
                .elevation_m,
            fluid_25d_source_outlet_bank_crest(geometry, in_bank_x, scenario.terrain_height_m,
                                               false)
                .elevation_m) -
        (scenario.terrain_height_m[in_bank_index] + depth[in_bank_index]);
    require(diagnostics.route_section_count == route_sections &&
                diagnostics.route_wet_section_count == 1U &&
                diagnostics.route_freeboard_above_half_m_section_count == 1U,
            "route summary should distinguish all columns from the one wet section");
    require_close(diagnostics.route_min_freeboard_m, single_section_freeboard_m, kDepthToleranceM,
                  "one wet section should set the minimum freeboard");
    require_close(diagnostics.route_mean_freeboard_m, single_section_freeboard_m, kDepthToleranceM,
                  "one wet section should set the mean freeboard");
    require_close(diagnostics.route_max_freeboard_m, single_section_freeboard_m, kDepthToleranceM,
                  "one wet section should set the maximum freeboard");
    require_close(diagnostics.route_min_centerline_depth_m, 0.0, kDepthToleranceM,
                  "dry route sections should remain visible in minimum centerline depth");
    require_close(diagnostics.route_mean_centerline_depth_m,
                  static_cast<double>(depth[in_bank_index]) / route_sections, kDepthToleranceM,
                  "route mean centerline depth should include dry sections");
    require_close(diagnostics.route_max_centerline_depth_m, depth[in_bank_index], kDepthToleranceM,
                  "the one wet centerline should set maximum centerline depth");

    const double summed_zone_water =
        diagnostics.before_source.water_volume_m3 + diagnostics.route_off_bank.water_volume_m3 +
        diagnostics.route_in_bank.water_volume_m3 + diagnostics.after_outlet.water_volume_m3;
    const double summed_zone_tracer =
        diagnostics.before_source.tracer_amount_m3 + diagnostics.route_off_bank.tracer_amount_m3 +
        diagnostics.route_in_bank.tracer_amount_m3 + diagnostics.after_outlet.tracer_amount_m3;
    require_close(summed_zone_water, fluid_25d_water_volume_m3(config, depth), kDepthToleranceM,
                  "the four spatial zones should partition total water without double counting");
    require_close(summed_zone_tracer, 0.1524, kDepthToleranceM,
                  "the four spatial zones should partition total tracer without double counting");

    const std::vector<float> initial_tracer_q(depth.size(), 0.0F);
    Fluid25DConfig initial_config =
        finite_volume_test_config(128U, 64U, Fluid25DScenario::SourceOutletDemo);
    initial_config.cell_size_m = scenario.cell_size_m;
    const Fluid25DSourceOutletSpatialDiagnostics initial =
        compute_fluid_25d_source_outlet_spatial_diagnostics(
            initial_config, scenario.terrain_height_m, scenario.initial_water_depth_m,
            initial_tracer_q);
    require(initial.before_source.water_volume_m3 == 0.0 &&
                initial.route_off_bank.water_volume_m3 == 0.0 &&
                initial.after_outlet.water_volume_m3 == 0.0 &&
                initial.route_in_bank.water_volume_m3 > 0.0,
            "the authored initial source-outlet seed should stay entirely route in-bank");
    require_close(initial.route_in_bank.water_volume_m3,
                  fluid_25d_water_volume_m3(initial_config, scenario.initial_water_depth_m),
                  kDepthToleranceM,
                  "initial in-bank water should account for the complete authored seed volume");
    require(!initial.material_dyed_after_outlet_valid &&
                initial.after_outlet.material_dyed_wet_cell_count == 0U,
            "an undyed initial seed should have no after-outlet material front");
    require(initial.route_section_count == route_sections &&
                initial.route_wet_section_count == route_sections &&
                initial.route_freeboard_above_half_m_section_count == 0U &&
                initial.route_min_centerline_depth_m > 2.60,
            "the deep initial seed should wet every route section below its crest");
    require_close(initial.route_min_freeboard_m, kFluid25DSourceOutletInitialFreeboardM,
                  kDepthToleranceM, "initial route minimum freeboard should match its seed");
    require_close(initial.route_mean_freeboard_m, kFluid25DSourceOutletInitialFreeboardM,
                  kDepthToleranceM, "initial route mean freeboard should match its seed");
    require_close(initial.route_max_freeboard_m, kFluid25DSourceOutletInitialFreeboardM,
                  kDepthToleranceM, "initial route maximum freeboard should match its seed");

    require_throws(
        [&] {
            static_cast<void>(compute_fluid_25d_source_outlet_spatial_diagnostics(
                config, scenario.terrain_height_m, std::span<const float>(depth).first(1U),
                tracer_q));
        },
        "source-outlet spatial diagnostics should reject mismatched readback dimensions");
    std::vector<float> invalid_tracer_q = tracer_q;
    invalid_tracer_q[after_tail_index] = -0.001F;
    require_throws(
        [&] {
            static_cast<void>(compute_fluid_25d_source_outlet_spatial_diagnostics(
                config, scenario.terrain_height_m, depth, invalid_tracer_q));
        },
        "source-outlet spatial diagnostics should reject negative tracer readback");
    invalid_tracer_q = tracer_q;
    invalid_tracer_q[after_tail_index] = 0.5F;
    require_throws(
        [&] {
            static_cast<void>(compute_fluid_25d_source_outlet_spatial_diagnostics(
                config, scenario.terrain_height_m, depth, invalid_tracer_q));
        },
        "source-outlet spatial diagnostics should reject tracer q above water depth");
    std::vector<float> invalid_terrain = scenario.terrain_height_m;
    invalid_terrain[before_index] = std::numeric_limits<float>::quiet_NaN();
    require_throws(
        [&] {
            static_cast<void>(compute_fluid_25d_source_outlet_spatial_diagnostics(
                config, invalid_terrain, depth, tracer_q));
        },
        "source-outlet spatial diagnostics should reject nonfinite terrain readback");
    Fluid25DConfig unrelated = config;
    unrelated.scenario = Fluid25DScenario::RiverCatchment;
    require_throws(
        [&] {
            static_cast<void>(compute_fluid_25d_source_outlet_spatial_diagnostics(
                unrelated, scenario.terrain_height_m, depth, tracer_q));
        },
        "source-outlet spatial diagnostics should reject other scenarios");
}

void test_tracer_profile_diagnostic_metric_math() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config = test_config(3U, 2U, Fluid25DScenario::TerrainCase);
    config.cell_size_m = 2.0F;
    config.minimum_wet_depth_m = 0.10F;
    const std::array<float, 6> depth{0.05F, 0.20F, 0.30F, 0.0F, 0.40F, 0.0F};
    // The below-wet sample is physically retained q and must influence the
    // amount-weighted centroid. The 0.33% concentration tail is retained too,
    // but must not make the material front jump to cell x=2.
    const std::array<float, 6> tracer_q{0.025F, 0.004F, 0.001F, 0.0F, 0.20F, 0.0F};
    const std::array<float, 6> sink_rate{0.0F, 0.0F, 0.0F, 0.0F, 0.10F, 0.0F};
    const std::array<Fluid25DTracerLedgerGpu, 6> ledger{{
        {.source_sink_boundary_reserved_m3 = {0.92F, 0.0F, 0.0F, 0.0F}},
        {},
        {},
        {},
        {},
        {},
    }};
    const Fluid25DTracerProfileDiagnostics diagnostics =
        compute_fluid_25d_tracer_profile_diagnostics(config, depth, tracer_q, sink_rate, ledger);
    require_close(diagnostics.total_tracer_amount_m3, 0.92, kDepthToleranceM,
                  "tracer diagnostics should integrate q by physical cell area");
    require_close(diagnostics.maximum_concentration, 0.5, kDepthToleranceM,
                  "tracer diagnostics should expose maximum wet concentration");
    require_close(diagnostics.mean_concentration, (0.5 + 0.02 + (1.0 / 300.0)) / 3.0,
                  kDepthToleranceM,
                  "tracer diagnostics should include retained wet tails in concentration mean");
    require(diagnostics.dyed_wet_cell_count == 2U,
            "tracer diagnostics should exclude sub-material concentration tails from dyed count");
    require_close(diagnostics.dyed_wet_cell_ratio, 2.0 / 6.0, kDepthToleranceM,
                  "tracer diagnostics should report the material dyed-cell ratio");
    require_close(diagnostics.amount_weighted_centroid_cell_x, 0.824 / 0.92, kDepthToleranceM,
                  "tracer centroid should include retained below-wet q");
    require_close(diagnostics.amount_weighted_centroid_cell_y, 0.8 / 0.92, kDepthToleranceM,
                  "tracer centroid should weight every retained q cell");
    require_close(diagnostics.downstream_extent_cell_x, 1.0, kDepthToleranceM,
                  "tracer front should exclude sub-material numerical tails");
    require_close(diagnostics.tracer_in_explicit_sink_region_m3, 0.8, kDepthToleranceM,
                  "tracer diagnostics should report amount inside explicit sink cells");
    require_close(diagnostics.cumulative_source_amount_m3, 0.92, kDepthToleranceM,
                  "tracer diagnostics should retain a separate source ledger");
    require_close(diagnostics.conservation_residual_m3, 0.0, kDepthToleranceM,
                  "tracer diagnostics should reconcile amount and separate ledger");
}

void test_mass_audit_diagnostics() {
    using namespace cubey::projects::fluid::fluid_25d;
    static_assert(kFluid25DMassAuditTerms == 12U);
    static_assert(sizeof(Fluid25DMassAuditGpu) == 192U);

    std::array<Fluid25DMassAuditGpu, 3U> cells{};
    const auto set_water = [](Fluid25DMassAuditGpu& cell, std::size_t term, float high, float low) {
        cell.water_hi[term] = high;
        cell.water_lo[term] = low;
    };
    set_water(cells[0], 0U, 2.0F, 0.0F);
    set_water(cells[1], 0U, 3.0F, 0.0F);
    set_water(cells[0], 1U, 0.25F, 0.0F);
    set_water(cells[1], 1U, 0.25F, 0.0F);
    set_water(cells[0], 2U, 0.4F, 0.0F);
    set_water(cells[1], 2U, 0.6F, 0.0F);
    set_water(cells[0], 3U, 2.0F, 0.0F);
    set_water(cells[1], 3U, 3.0F, 0.0F);
    set_water(cells[0], 4U, 0.2F, 0.0F);
    set_water(cells[1], 4U, 0.3F, 0.0F);
    set_water(cells[0], 5U, 0.125F, 0.0F);
    set_water(cells[1], 5U, 0.125F, 0.0F);
    set_water(cells[0], 6U, 0.4F, 0.0F);
    set_water(cells[1], 6U, 0.5F, 0.0F);
    set_water(cells[0], 7U, 0.02F, 0.0F);
    set_water(cells[1], 7U, 0.03F, 0.0F);
    set_water(cells[0], 8U, 0.04F, 0.0F);
    set_water(cells[1], 8U, 0.06F, 0.0F);
    for (Fluid25DMassAuditGpu& cell : cells) {
        cell.water_hi[9U] = 4.0F;
    }

    const auto original_cells = cells;
    const Fluid25DMassAuditDiagnostics water =
        compute_fluid_25d_mass_audit(cells, 106.0, 100.0, 5.25, 0.75, 1.1);
    require_close(water.totals_m3[0U], 5.0, 1.0e-6,
                  "mass audit should sum independent nominal source deltas");
    require_close(water.totals_m3[1U], 0.5, 1.0e-6,
                  "mass audit should sum independent nominal sink deltas");
    require_close(water.totals_m3[2U], 1.0, 1.0e-6,
                  "mass audit should sum independent nominal boundary deltas");
    require_close(water.totals_m3[3U], 5.0, 1.0e-6,
                  "mass audit should sum actual source field additions");
    require_close(water.totals_m3[4U], 0.5, 1.0e-6,
                  "mass audit should sum actual sink field removals");
    require_close(water.totals_m3[5U], 0.25, 1.0e-6,
                  "mass audit should preserve signed internal transport");
    require_close(water.totals_m3[6U], 0.9, 1.0e-6,
                  "mass audit should sum positive boundary transport out");
    require_close(water.totals_m3[7U], 0.05, 1.0e-6,
                  "mass audit should sum update arithmetic corrections");
    require_close(water.totals_m3[8U], 0.1, 1.0e-6, "mass audit should sum clamp corrections");
    require(water.committed_substeps == 4U && water.totals_m3[9U] == 4.0,
            "mass audit should retain the common per-cell committed-step count once");
    require(water.totals_m3[10U] == 0.0 && water.totals_m3[11U] == 0.0,
            "mass audit reserved totals should remain zero");
    require_close(water.field_budget_residual_m3, 2.0, 1.0e-6,
                  "mass audit field budget should include transport, arithmetic, and clamp terms");
    require_close(water.cumulative_ledger_rounding_m3, 0.1, 1.0e-6,
                  "mass audit should separate cumulative-ledger rounding");
    require_close(water.source_representation_difference_m3, 0.0, 1.0e-6,
                  "mass audit should compare actual source change with nominal source delta");
    require_close(water.sink_representation_difference_m3, 0.0, 1.0e-6,
                  "mass audit should compare nominal sink delta with actual sink removal");
    require_close(water.boundary_definition_difference_m3, 0.1, 1.0e-6,
                  "mass audit should compare nominal boundary delta with boundary transport");
    require_close(water.observed_conservation_residual_m3, 2.6, 1.0e-6,
                  "mass audit should reproduce the existing observed conservation residual");
    require_close(water.field_budget_residual_m3 + water.source_representation_difference_m3 +
                      water.sink_representation_difference_m3 +
                      water.boundary_definition_difference_m3 +
                      water.cumulative_ledger_rounding_m3 + water.totals_m3[5U] +
                      water.totals_m3[7U] + water.totals_m3[8U],
                  water.observed_conservation_residual_m3, 1.0e-6,
                  "mass audit terms should recompose to the observed conservation residual");
    for (std::size_t index = 0U; index < cells.size(); ++index) {
        require(cells[index].water_hi == original_cells[index].water_hi &&
                    cells[index].water_lo == original_cells[index].water_lo &&
                    cells[index].tracer_hi == original_cells[index].tracer_hi &&
                    cells[index].tracer_lo == original_cells[index].tracer_lo,
                "mass audit reduction should not mutate its input buffer");
    }

    std::array<Fluid25DMassAuditGpu, 2U> expansions{};
    expansions[0].water_hi[0U] = 2.5F;
    expansions[1].water_hi[0U] = 2.5F;
    expansions[0].water_hi[3U] = 2.5F;
    expansions[1].water_hi[3U] = 2.5F;
    expansions[0].water_hi[5U] = 1.0e20F;
    expansions[0].water_lo[5U] = 0.125F;
    expansions[1].water_hi[5U] = -1.0e20F;
    expansions[1].water_lo[5U] = 0.125F;
    expansions[0].tracer_hi[0U] = 2.5F;
    expansions[1].tracer_hi[0U] = 2.5F;
    expansions[0].tracer_hi[3U] = 2.5F;
    expansions[1].tracer_hi[3U] = 2.5F;
    expansions[0].tracer_hi[5U] = 1.0e20F;
    expansions[0].tracer_lo[5U] = 0.25F;
    expansions[1].tracer_hi[5U] = -1.0e20F;
    expansions[1].tracer_lo[5U] = 0.25F;
    for (Fluid25DMassAuditGpu& cell : expansions) {
        cell.water_hi[9U] = 3.0F;
        cell.tracer_hi[9U] = 3.0F;
    }
    const Fluid25DMassAuditDiagnostics water_expansion =
        compute_fluid_25d_mass_audit(expansions, 5.25, 0.0, 5.0, 0.0, 0.0);
    require_close(water_expansion.totals_m3[5U], 0.25, 1.0e-7,
                  "mass audit should retain low-part residuals after large-term cancellation");
    require_close(water_expansion.field_budget_residual_m3, 0.0, 1.0e-7,
                  "water expansion audit should reconcile its selected channel");
    const Fluid25DMassAuditDiagnostics tracer_expansion =
        compute_fluid_25d_mass_audit(expansions, 5.5, 0.0, 5.0, 0.0, 0.0, true);
    require_close(tracer_expansion.totals_m3[5U], 0.5, 1.0e-7,
                  "mass audit should select the tracer high/low channel when requested");
    require_close(tracer_expansion.field_budget_residual_m3, 0.0, 1.0e-7,
                  "tracer expansion audit should reconcile its selected channel");

    const std::array<Fluid25DMassAuditGpu, 1U> reset_cells{};
    const Fluid25DMassAuditDiagnostics reset =
        compute_fluid_25d_mass_audit(reset_cells, 0.0, 0.0, 0.0, 0.0, 0.0);
    require(reset.committed_substeps == 0U && reset.observed_conservation_residual_m3 == 0.0 &&
                std::all_of(reset.totals_m3.begin(), reset.totals_m3.end(),
                            [](double value) { return value == 0.0; }),
            "zeroed audit fields should represent a clean reset without accumulated totals");

    require_throws(
        [] {
            static_cast<void>(compute_fluid_25d_mass_audit(std::span<const Fluid25DMassAuditGpu>{},
                                                           0.0, 0.0, 0.0, 0.0, 0.0));
        },
        "mass audit should reject an empty cell span");
    require_throws(
        [&] {
            auto invalid = cells;
            invalid[0].water_hi[0U] = std::numeric_limits<float>::quiet_NaN();
            static_cast<void>(compute_fluid_25d_mass_audit(invalid, 106.0, 100.0, 5.25, 0.75, 1.1));
        },
        "mass audit should reject nonfinite high or low expansion data");
    require_throws(
        [&] {
            auto invalid = cells;
            invalid[1].water_lo[10U] = 1.0F;
            static_cast<void>(compute_fluid_25d_mass_audit(invalid, 106.0, 100.0, 5.25, 0.75, 1.1));
        },
        "mass audit should reject nonzero reserved expansion terms");
    require_throws(
        [&] {
            auto invalid = cells;
            invalid[2].water_hi[9U] = 5.0F;
            static_cast<void>(compute_fluid_25d_mass_audit(invalid, 106.0, 100.0, 5.25, 0.75, 1.1));
        },
        "mass audit should reject inconsistent per-cell committed-step counts");
    require_throws(
        [&] {
            auto invalid = cells;
            invalid[0].water_hi[9U] = 4.5F;
            static_cast<void>(compute_fluid_25d_mass_audit(invalid, 106.0, 100.0, 5.25, 0.75, 1.1));
        },
        "mass audit should reject noninteger committed-step counts");
    require_throws(
        [&] { static_cast<void>(compute_fluid_25d_mass_audit(cells, -1.0, 0.0, 0.0, 0.0, 0.0)); },
        "mass audit should reject negative diagnostic volumes");
    require_throws(
        [&] {
            static_cast<void>(compute_fluid_25d_mass_audit(
                cells, 106.0, 100.0, std::numeric_limits<double>::infinity(), 0.75, 1.1));
        },
        "mass audit should reject nonfinite cumulative ledger totals");
}

} // namespace cubey::projects::fluid::fluid_25d::test_detail
