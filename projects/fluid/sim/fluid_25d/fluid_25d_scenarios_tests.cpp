#include "fluid_25d_test_support.h"

namespace cubey::projects::fluid::fluid_25d::test_detail {

void test_deterministic_scenarios() {
    using namespace cubey::projects::fluid::fluid_25d;
    require(fluid_25d_scenario_index(4, 3, 3, 2) == 11U,
            "scenario indexing should use checked row-major coordinates");
    require_throws([] { static_cast<void>(fluid_25d_scenario_index(4, 3, 4, 0)); },
                   "scenario indexing should reject an out-of-bounds x coordinate");
    require_throws([] { static_cast<void>(fluid_25d_scenario_index(4, 3, 0, 3)); },
                   "scenario indexing should reject an out-of-bounds y coordinate");
    const Fluid25DScenarioData dry_a =
        make_fluid_25d_scenario(Fluid25DScenario::DryBed, 10, 6, 1.0F);
    const Fluid25DScenarioData dry_b =
        make_fluid_25d_scenario(Fluid25DScenario::DryBed, 10, 6, 1.0F);
    require(dry_a.terrain_height_m == dry_b.terrain_height_m,
            "dry-bed terrain generation should be deterministic");
    require(dry_a.initial_water_depth_m == dry_b.initial_water_depth_m,
            "dry-bed water generation should be deterministic");
    require(std::all_of(dry_a.initial_water_depth_m.begin(), dry_a.initial_water_depth_m.end(),
                        [](float value) { return value == 0.0F; }),
            "dry-bed scenario should start without water");

    const Fluid25DScenarioData lake =
        make_fluid_25d_scenario(Fluid25DScenario::LakeAtRest, 10, 6, 1.0F);
    const auto [terrain_min, terrain_max] =
        std::minmax_element(lake.terrain_height_m.begin(), lake.terrain_height_m.end());
    require(*terrain_max - *terrain_min > 0.1F,
            "lake-at-rest fixture should contain uneven terrain");
    const float reference_surface = lake.terrain_height_m[0] + lake.initial_water_depth_m[0];
    for (std::size_t index = 0; index < lake.terrain_height_m.size(); ++index) {
        require_close(
            static_cast<double>(lake.terrain_height_m[index] + lake.initial_water_depth_m[index]),
            reference_surface, kDepthToleranceM,
            "lake-at-rest fixture should have a constant free surface");
    }

    const Fluid25DScenarioData river =
        make_fluid_25d_scenario(Fluid25DScenario::RiverCatchment, 20, 8, 1.0F);
    require(river.source_cell != kFluid25DNoCell && river.sink_cell != kFluid25DNoCell,
            "river fixture should identify one source and one sink");
    require(river.source_cell != river.sink_cell,
            "river fixture source and sink should be distinct cells");
    require(river.source_depth_rate_m_per_s[river.source_cell] > 0.0F,
            "river fixture should carry one positive source rate");
    require(river.sink_depth_rate_m_per_s[river.sink_cell] > 0.0F,
            "river fixture should carry one positive sink rate");
    require(river.initial_water_depth_m[river.source_cell] > 0.0F &&
                river.initial_water_depth_m[river.sink_cell] == 0.0F,
            "River V0 should retain its narrow initially dry-outlet fixture");

    const Fluid25DScenarioData readable_river =
        make_fluid_25d_scenario(Fluid25DScenario::SourceOutletDemo, 128U, 64U, 1.0F);
    require(readable_river.source_cell != kFluid25DNoCell &&
                readable_river.sink_cell != kFluid25DNoCell &&
                readable_river.source_cell != readable_river.sink_cell,
            "source-outlet demo should identify distinct endpoint centroids");
    require(readable_river.initial_water_depth_m[readable_river.source_cell] > 0.0F &&
                readable_river.initial_water_depth_m[readable_river.sink_cell] > 0.0F,
            "source-outlet demo should seed a visible route through both endpoint regions");
    const std::uint32_t readable_source_x =
        static_cast<std::uint32_t>(readable_river.source_cell % readable_river.width);
    const std::uint32_t readable_sink_x =
        static_cast<std::uint32_t>(readable_river.sink_cell % readable_river.width);
    require(readable_source_x == 14U && readable_sink_x == 110U &&
                readable_sink_x - readable_source_x == 96U,
            "source-outlet demo should retain its long 128 by 64 endpoint route");
    const Fluid25DSourceOutletGeometry readable_geometry(128U, 64U);
    const std::uint32_t expected_strip_length =
        std::min(kFluid25DSourceOutletForcingStripLengthCells,
                 std::max(1U, (readable_sink_x - readable_source_x) / 3U));
    const float cell_area_m2 = readable_river.cell_size_m * readable_river.cell_size_m;
    std::vector<std::uint8_t> expected_source_mask(readable_river.source_depth_rate_m_per_s.size(),
                                                   0U);
    std::vector<std::uint8_t> expected_sink_mask(readable_river.sink_depth_rate_m_per_s.size(), 0U);
    std::array<std::uint32_t, kFluid25DSourceOutletForcingStripLengthCells> source_column_cells{};
    std::array<std::uint32_t, kFluid25DSourceOutletForcingStripLengthCells> sink_column_cells{};
    for (std::uint32_t x = readable_source_x; x <= readable_sink_x; ++x) {
        const auto left_crest = fluid_25d_source_outlet_bank_crest(
            readable_geometry, x, readable_river.terrain_height_m, true);
        const auto right_crest = fluid_25d_source_outlet_bank_crest(
            readable_geometry, x, readable_river.terrain_height_m, false);
        const std::uint32_t first_bank_y = std::min(left_crest.y_cell, right_crest.y_cell);
        const std::uint32_t last_bank_y = std::max(left_crest.y_cell, right_crest.y_cell);
        const float center_y = readable_geometry.channel_center_y(static_cast<float>(x));
        const float half_width = readable_geometry.channel_half_width(static_cast<float>(x));
        const bool source_column =
            x >= readable_source_x && x < readable_source_x + expected_strip_length;
        const bool sink_column =
            x <= readable_sink_x && x + expected_strip_length > readable_sink_x;
        for (std::uint32_t y = first_bank_y; y <= last_bank_y; ++y) {
            const std::size_t index =
                fluid_25d_scenario_index(readable_river.width, readable_river.height, x, y);
            const float normalized_offset = std::abs(static_cast<float>(y) - center_y) / half_width;
            const bool expected_wetted_cell =
                normalized_offset <= kFluid25DSourceOutletForcingHalfWidthNormalized &&
                readable_river.initial_water_depth_m[index] > 0.10F;
            if (expected_wetted_cell && source_column) {
                expected_source_mask[index] = 1U;
                ++source_column_cells[x - readable_source_x];
            }
            if (expected_wetted_cell && sink_column) {
                expected_sink_mask[index] = 1U;
                ++sink_column_cells[x - (readable_sink_x - expected_strip_length + 1U)];
            }
        }
    }
    std::uint32_t source_region_cells = 0U;
    std::uint32_t sink_region_cells = 0U;
    double source_rate_sum_m3_per_s = 0.0;
    double sink_rate_sum_m3_per_s = 0.0;
    double source_rate_per_cell = 0.0;
    double sink_rate_per_cell = 0.0;
    bool overlapping_endpoint_rates = false;
    for (std::size_t index = 0U; index < readable_river.source_depth_rate_m_per_s.size(); ++index) {
        const float source_rate = readable_river.source_depth_rate_m_per_s[index];
        const float sink_rate = readable_river.sink_depth_rate_m_per_s[index];
        const bool source_cell = source_rate > 0.0F;
        const bool sink_cell = sink_rate > 0.0F;
        require(source_cell == (expected_source_mask[index] != 0U) &&
                    sink_cell == (expected_sink_mask[index] != 0U),
                "source-outlet forcing should match the bounded, wetted endpoint strip geometry");
        source_region_cells += source_rate > 0.0F ? 1U : 0U;
        sink_region_cells += sink_rate > 0.0F ? 1U : 0U;
        source_rate_sum_m3_per_s += static_cast<double>(source_rate) * cell_area_m2;
        sink_rate_sum_m3_per_s += static_cast<double>(sink_rate) * cell_area_m2;
        if (source_cell) {
            if (source_rate_per_cell == 0.0) {
                source_rate_per_cell = source_rate;
            }
            require_close(source_rate, source_rate_per_cell, kDepthToleranceM,
                          "source strip should distribute its normalized throughput uniformly");
        }
        if (sink_cell) {
            if (sink_rate_per_cell == 0.0) {
                sink_rate_per_cell = sink_rate;
            }
            require_close(sink_rate, sink_rate_per_cell, kDepthToleranceM,
                          "sink strip should distribute its normalized throughput uniformly");
        }
        overlapping_endpoint_rates =
            overlapping_endpoint_rates || (source_rate > 0.0F && sink_rate > 0.0F);
    }
    require(source_region_cells > 1U && sink_region_cells > 1U && !overlapping_endpoint_rates &&
                std::all_of(source_column_cells.begin(), source_column_cells.end(),
                            [](std::uint32_t count) { return count > 0U; }) &&
                std::all_of(sink_column_cells.begin(), sink_column_cells.end(),
                            [](std::uint32_t count) { return count > 0U; }),
            "source-outlet demo should retain separate, populated four-column endpoint strips");
    require(readable_river.source_depth_rate_m_per_s[readable_river.source_cell] > 0.0F &&
                readable_river.sink_depth_rate_m_per_s[readable_river.sink_cell] > 0.0F,
            "source-outlet markers should remain inside their forcing strips");
    require_close(source_rate_sum_m3_per_s, kFluid25DSourceOutletEndpointTotalVolumeRateM3PerS,
                  kDepthToleranceM,
                  "source-outlet source strip should retain its normalized physical rate");
    require_close(sink_rate_sum_m3_per_s, kFluid25DSourceOutletEndpointTotalVolumeRateM3PerS,
                  kDepthToleranceM,
                  "source-outlet sink strip should match the normalized source rate");

    const std::size_t river_source_count = static_cast<std::size_t>(std::count_if(
        river.source_depth_rate_m_per_s.begin(), river.source_depth_rate_m_per_s.end(),
        [](float rate) { return rate > 0.0F; }));
    const std::size_t river_sink_count = static_cast<std::size_t>(
        std::count_if(river.sink_depth_rate_m_per_s.begin(), river.sink_depth_rate_m_per_s.end(),
                      [](float rate) { return rate > 0.0F; }));
    require(river_source_count == 1U && river_sink_count == 1U,
            "River V0 should retain its single-cell source and sink masks");
    require_close(river.source_depth_rate_m_per_s[river.source_cell], 0.040, kDepthToleranceM,
                  "River V0 source rate should remain unchanged");
    require_close(river.sink_depth_rate_m_per_s[river.sink_cell], 0.055, kDepthToleranceM,
                  "River V0 sink rate should remain unchanged");

    const std::array<std::uint32_t, 5U> station_x_cells{14U, 38U, 70U, 91U, 110U};
    float previous_route_surface_m = std::numeric_limits<float>::infinity();
    for (std::uint32_t x = readable_source_x; x <= readable_sink_x; ++x) {
        const float center_y = readable_geometry.channel_center_y(static_cast<float>(x));
        const std::uint32_t center_row = static_cast<std::uint32_t>(std::lround(center_y));
        const std::size_t center_index =
            fluid_25d_scenario_index(readable_river.width, readable_river.height, x, center_row);
        const auto left_crest = fluid_25d_source_outlet_bank_crest(
            readable_geometry, x, readable_river.terrain_height_m, true);
        const auto right_crest = fluid_25d_source_outlet_bank_crest(
            readable_geometry, x, readable_river.terrain_height_m, false);
        const std::uint32_t first_bank_y = std::min(left_crest.y_cell, right_crest.y_cell);
        const std::uint32_t last_bank_y = std::max(left_crest.y_cell, right_crest.y_cell);
        const float centerline_bed_m = readable_river.terrain_height_m[center_index];
        const float lower_crest_m = std::min(left_crest.elevation_m, right_crest.elevation_m);
        const float crest_rise_m = lower_crest_m - centerline_bed_m;
        require(!left_crest.fallback && !right_crest.fallback && crest_rise_m >= 2.80F &&
                    crest_rise_m <= 3.05F,
                "source-outlet lower bank crest should stay about 3.0 m above the channel bed");
        const float target_surface_m = lower_crest_m - kFluid25DSourceOutletInitialFreeboardM;
        const float absolute_freeboard_m = lower_crest_m - target_surface_m;
        require(absolute_freeboard_m >= 0.25F && absolute_freeboard_m <= 0.35F,
                "source-outlet initial surface should stay 0.25-0.35 m below its discrete crest");
        require_close(absolute_freeboard_m, 0.30, kDepthToleranceM,
                      "source-outlet initial freeboard should be an absolute 0.30 m");
        float maximum_cell_rise_m = 0.0F;
        const float half_width = readable_geometry.channel_half_width(static_cast<float>(x));
        for (std::uint32_t y = 0U; y + 1U < readable_river.height; ++y) {
            const float lower_relief = readable_geometry.cross_sectional_relief_m(
                (static_cast<float>(y) - center_y) / half_width);
            const float upper_relief = readable_geometry.cross_sectional_relief_m(
                (static_cast<float>(y + 1U) - center_y) / half_width);
            maximum_cell_rise_m =
                std::max(maximum_cell_rise_m, std::abs(upper_relief - lower_relief));
        }
        require(maximum_cell_rise_m < 1.05F,
                "source-outlet bank ramp should not form a near-vertical cell-scale wall");
        require(target_surface_m <= previous_route_surface_m + 1.0e-5F,
                "source-outlet initial free surface should have no local reverse slope along the "
                "route");
        previous_route_surface_m = target_surface_m;
        require_close(static_cast<double>(readable_river.terrain_height_m[center_index] +
                                          readable_river.initial_water_depth_m[center_index]),
                      target_surface_m, 0.01,
                      "source-outlet centerline seed should retain its absolute lower-crest "
                      "free-surface target");

        const bool is_station =
            std::find(station_x_cells.begin(), station_x_cells.end(), x) != station_x_cells.end();
        if (is_station) {
            const std::uint32_t bank_span_cells = last_bank_y - first_bank_y + 1U;
            std::uint32_t wet_bank_cells = 0U;
            for (std::uint32_t y = first_bank_y; y <= last_bank_y; ++y) {
                const std::size_t index =
                    fluid_25d_scenario_index(readable_river.width, readable_river.height, x, y);
                wet_bank_cells += readable_river.initial_water_depth_m[index] > 0.01F ? 1U : 0U;
            }
            const float wet_width_fraction =
                static_cast<float>(wet_bank_cells) / static_cast<float>(bank_span_cells);
            if (wet_width_fraction < 0.60F) {
                throw std::runtime_error(
                    "source-outlet initial wet width at x=" + std::to_string(x) + " is " +
                    std::to_string(wet_width_fraction));
            }
        }

        for (std::uint32_t y = 0U; y < readable_river.height; ++y) {
            if (y >= first_bank_y && y <= last_bank_y) {
                continue;
            }
            const std::size_t index =
                fluid_25d_scenario_index(readable_river.width, readable_river.height, x, y);
            require(readable_river.initial_water_depth_m[index] == 0.0F,
                    "source-outlet initial water should remain inside the selected bank span");
        }
    }
    const auto nominal_bed_at = [&](std::uint32_t x) {
        const float center_y = readable_geometry.channel_center_y(static_cast<float>(x));
        const std::uint32_t center_row = static_cast<std::uint32_t>(std::lround(center_y));
        const float half_width = readable_geometry.channel_half_width(static_cast<float>(x));
        const float normalized_offset = (static_cast<float>(center_row) - center_y) / half_width;
        return readable_river.terrain_height_m[fluid_25d_scenario_index(
                   readable_river.width, readable_river.height, x, center_row)] -
               readable_geometry.cross_sectional_relief_m(normalized_offset);
    };
    require_close(nominal_bed_at(readable_sink_x) - nominal_bed_at(readable_source_x),
                  -kFluid25DSourceOutletLongitudinalFallM, kDepthToleranceM,
                  "source-outlet route should retain its 0.45 m longitudinal fall");
    require_close(fluid_25d_catchment_height_scale(Fluid25DScenario::RiverCatchment), 0.08,
                  kDepthToleranceM,
                  "River V0 should retain the shared restrained render height scale");
    require_close(fluid_25d_catchment_height_scale(Fluid25DScenario::SourceOutletDemo), 0.65,
                  kDepthToleranceM,
                  "source-outlet demo should select its render-only relief scale");
    require_close(fluid_25d_catchment_height_scale(Fluid25DScenario::MountainSourceOutletDemo),
                  0.60, kDepthToleranceM,
                  "mountain demo should select its own render-only relief scale");
    require_close(fluid_25d_catchment_height_scale(Fluid25DScenario::SustainedHeadwatersDemo),
                  kFluid25DSustainedHeadwatersHeightScale, kDepthToleranceM,
                  "sustained-headwaters terrain should select render-only bank relief");
    require_close(
        fluid_25d_catchment_home_horizontal_extent(Fluid25DScenario::RiverCatchment, 127.0F, 96.0F),
        127.0, kDepthToleranceM,
        "River V0 home camera should retain its full-domain framing contract");
    const auto hillside_overview_limits =
        fluid_25d_catchment_orbit_limits(Fluid25DScenario::HillsideFlowStudy, 7650.0F, 7650.0F);
    const auto hillside_close_limits =
        fluid_25d_catchment_orbit_limits(Fluid25DScenario::HillsideFlowStudy, 7650.0F, 3200.0F);
    require(hillside_overview_limits.minimum_distance_m ==
                    hillside_close_limits.minimum_distance_m &&
                hillside_overview_limits.maximum_distance_m ==
                    hillside_close_limits.maximum_distance_m &&
                hillside_overview_limits.minimum_distance_m <= 1000.0F &&
                hillside_overview_limits.maximum_distance_m >= 25000.0F,
            "hillside framing switches must retain accepted close and distant home overrides");
    const auto river_limits =
        fluid_25d_catchment_orbit_limits(Fluid25DScenario::RiverCatchment, 127.0F, 96.0F);
    require(river_limits.minimum_distance_m == std::max(8.0F, 96.0F * 0.30F) &&
                river_limits.maximum_distance_m == std::max(48.0F, 96.0F * 4.0F),
            "other scenario orbit limits must retain the exact original framing formula");
    require_close(fluid_25d_catchment_home_horizontal_extent(Fluid25DScenario::SourceOutletDemo,
                                                             127.0F, 96.0F),
                  120.0, kDepthToleranceM,
                  "source-outlet demo home camera should fit its long route without empty margins");
    require_close(fluid_25d_catchment_home_horizontal_extent(
                      Fluid25DScenario::MountainSourceOutletDemo, 7650.0F, 6720.0F),
                  7929.6, 0.001,
                  "mountain demo home camera should frame its full 6.97 kilometre route");
    require_close(fluid_25d_catchment_home_pitch(-0.92F, Fluid25DScenario::RiverCatchment), -0.92,
                  kDepthToleranceM,
                  "River V0 home camera should retain its original oblique pitch");
    require_close(fluid_25d_catchment_home_pitch(-0.92F, Fluid25DScenario::SourceOutletDemo), -0.72,
                  kDepthToleranceM,
                  "source-outlet demo home camera should expose its render-only relief");
    require_close(
        fluid_25d_catchment_home_pitch(-0.92F, Fluid25DScenario::MountainSourceOutletDemo), -0.55,
        kDepthToleranceM, "mountain demo home camera should expose its kilometre-scale relief");
    require_close(
        fluid_25d_catchment_home_pitch(-0.92F, Fluid25DScenario::SustainedHeadwatersDemo), -0.70,
        kDepthToleranceM,
        "sustained-headwaters home camera should expose the authored banks and tributaries");
    require_close(fluid_25d_catchment_home_distance_scale(Fluid25DScenario::RiverCatchment), 1.05,
                  kDepthToleranceM, "River V0 should retain its original home camera distance");
    require_close(
        fluid_25d_catchment_home_distance_scale(Fluid25DScenario::MountainSourceOutletDemo), 0.85,
        kDepthToleranceM, "mountain demo home camera should fill an overview with its full route");
    require_close(
        fluid_25d_catchment_home_distance_scale(Fluid25DScenario::SustainedHeadwatersDemo), 0.80,
        kDepthToleranceM,
        "sustained-headwaters home camera should frame the compact Y-shaped control");
    require_close(
        fluid_25d_catchment_home_fovy_radians(1.0471975512F, Fluid25DScenario::RiverCatchment),
        1.0471975512, kDepthToleranceM,
        "River V0 should retain its original home camera field of view");
    require_close(fluid_25d_catchment_home_fovy_radians(1.0471975512F,
                                                        Fluid25DScenario::MountainSourceOutletDemo),
                  0.84, kDepthToleranceM,
                  "mountain demo home camera should give the full route useful capture scale");
    require_close(fluid_25d_catchment_terrain_material_cue(Fluid25DScenario::RiverCatchment), 0.0,
                  kDepthToleranceM, "River V0 should retain the shared terrain material");
    require_close(fluid_25d_catchment_terrain_material_cue(Fluid25DScenario::SourceOutletDemo), 1.0,
                  kDepthToleranceM,
                  "source-outlet demo should select its render-only terrain height and slope cue");
    require_close(
        fluid_25d_catchment_terrain_material_cue(Fluid25DScenario::SustainedHeadwatersDemo), 3.0,
        kDepthToleranceM,
        "sustained-headwaters terrain should select its authored-bank render-only palette");
    require_close(
        fluid_25d_catchment_terrain_material_cue(Fluid25DScenario::MountainSourceOutletDemo), 2.0,
        kDepthToleranceM,
        "mountain demo should select its separate immutable-crop terrain material cue");
    const float outlet_bed_height_m = readable_river.terrain_height_m[readable_river.sink_cell];
    const float source_bed_height_m = readable_river.terrain_height_m[readable_river.source_cell];
    float minimum_ribbon_center_y = std::numeric_limits<float>::infinity();
    float maximum_ribbon_center_y = -std::numeric_limits<float>::infinity();
    float maximum_transverse_terrain_relief_m = 0.0F;
    std::uint32_t narrowest_ribbon_cells = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t widest_ribbon_cells = 0U;
    for (std::uint32_t x = readable_source_x; x <= readable_sink_x; ++x) {
        std::uint32_t wet_cells = 0U;
        float weighted_y_sum = 0.0F;
        float depth_sum = 0.0F;
        for (std::uint32_t y = 0U; y < readable_river.height; ++y) {
            const float depth = readable_river.initial_water_depth_m[fluid_25d_scenario_index(
                readable_river.width, readable_river.height, x, y)];
            if (depth > 0.0F) {
                ++wet_cells;
                weighted_y_sum += static_cast<float>(y) * depth;
                depth_sum += depth;
            }
        }
        require(wet_cells >= 3U && depth_sum > 0.0F,
                "river fixture should seed a broad connected water ribbon in every route column");
        narrowest_ribbon_cells = std::min(narrowest_ribbon_cells, wet_cells);
        widest_ribbon_cells = std::max(widest_ribbon_cells, wet_cells);
        const float ribbon_center_y = weighted_y_sum / depth_sum;
        minimum_ribbon_center_y = std::min(minimum_ribbon_center_y, ribbon_center_y);
        maximum_ribbon_center_y = std::max(maximum_ribbon_center_y, ribbon_center_y);
        float transverse_minimum = std::numeric_limits<float>::infinity();
        float transverse_maximum = -std::numeric_limits<float>::infinity();
        for (std::uint32_t y = 0U; y < readable_river.height; ++y) {
            const float terrain = readable_river.terrain_height_m[fluid_25d_scenario_index(
                readable_river.width, readable_river.height, x, y)];
            transverse_minimum = std::min(transverse_minimum, terrain);
            transverse_maximum = std::max(transverse_maximum, terrain);
        }
        maximum_transverse_terrain_relief_m =
            std::max(maximum_transverse_terrain_relief_m, transverse_maximum - transverse_minimum);
    }
    require(maximum_ribbon_center_y - minimum_ribbon_center_y > 12.0F,
            "source-outlet demo should retain multiple broad terrain-guided bends");
    require(widest_ribbon_cells >= narrowest_ribbon_cells + 4U,
            "source-outlet demo should retain visible width variation and one constriction");
    require(maximum_transverse_terrain_relief_m > 3.2F &&
                maximum_transverse_terrain_relief_m < 3.5F,
            "source-outlet demo should retain its 3.0 m bank crest and gentle outer valley wall");
    const float route_fall_m = source_bed_height_m - outlet_bed_height_m;
    require(route_fall_m >= 0.35F && route_fall_m <= 0.55F,
            "source-outlet demo should keep a smooth 0.35-0.55 m fall over its 96 m route");
    for (std::uint32_t x = readable_sink_x + 1U; x < readable_river.width; ++x) {
        float downstream_valley_floor_m = std::numeric_limits<float>::infinity();
        for (std::uint32_t y = 0U; y < readable_river.height; ++y) {
            const std::size_t index =
                fluid_25d_scenario_index(readable_river.width, readable_river.height, x, y);
            downstream_valley_floor_m =
                std::min(downstream_valley_floor_m, readable_river.terrain_height_m[index]);
            require(readable_river.initial_water_depth_m[index] == 0.0F,
                    "source-outlet demo should not seed water beyond its terminal outlet");
        }
        require(downstream_valley_floor_m > outlet_bed_height_m + 0.05F,
                "source-outlet demo should raise the closed downstream shoulder above the outlet");
    }
    require(!dry_a.terrain_provenance.has_value() && !lake.terrain_provenance.has_value() &&
                !river.terrain_provenance.has_value(),
            "analytic fixtures should not acquire terrain-case provenance");
    require(std::all_of(dry_a.boundary_outflow_face_mask.begin(),
                        dry_a.boundary_outflow_face_mask.end(),
                        [](std::uint32_t mask) { return mask == 0U; }) &&
                std::all_of(lake.boundary_outflow_face_mask.begin(),
                            lake.boundary_outflow_face_mask.end(),
                            [](std::uint32_t mask) { return mask == 0U; }) &&
                std::all_of(river.boundary_outflow_face_mask.begin(),
                            river.boundary_outflow_face_mask.end(),
                            [](std::uint32_t mask) { return mask == 0U; }),
            "analytic fixtures should retain closed outer boundaries by default");
}

void test_sustained_headwaters_scenario_construction() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config;
    config.scenario = Fluid25DScenario::SustainedHeadwatersDemo;
    config.solver = Fluid25DSolver::FiniteVolume;
    config.grid_width = kFluid25DSustainedHeadwatersGridWidth;
    config.grid_height = kFluid25DSustainedHeadwatersGridHeight;
    config.cell_size_m = kFluid25DSustainedHeadwatersCellSizeM;
    validate_fluid_25d_config(config);

    const Fluid25DScenarioData scenario = make_fluid_25d_scenario(
        config.scenario, config.grid_width, config.grid_height, config.cell_size_m);
    // Canonical FNV-1a over each 32-bit field word, captured from the original
    // 65x33@4m builder before refined-grid support was added. This protects the
    // baseline terrain, forcing, dry state, and boundary mask byte-for-byte.
    require(
        headwaters_field_hash(scenario.terrain_height_m) == 15469740960098613791ULL &&
            headwaters_field_hash(scenario.initial_water_depth_m) == 3895755185517356639ULL &&
            headwaters_field_hash(scenario.source_depth_rate_m_per_s) == 549231792614289439ULL &&
            headwaters_field_hash(scenario.sink_depth_rate_m_per_s) == 3895755185517356639ULL &&
            headwaters_mask_hash(scenario.boundary_outflow_face_mask) == 12659419801437752869ULL,
        "4 m sustained-headwaters fields should remain bit-identical to the original control");
    const Fluid25DScenarioData doubled_scenario = make_fluid_25d_scenario(
        config.scenario, config.grid_width, config.grid_height, config.cell_size_m, 2.0F);
    require(
        doubled_scenario.terrain_height_m == scenario.terrain_height_m &&
            doubled_scenario.initial_water_depth_m == scenario.initial_water_depth_m &&
            doubled_scenario.sink_depth_rate_m_per_s == scenario.sink_depth_rate_m_per_s &&
            doubled_scenario.boundary_outflow_face_mask == scenario.boundary_outflow_face_mask &&
            doubled_scenario.outlet_cell == scenario.outlet_cell &&
            doubled_scenario.sink_cell == scenario.sink_cell,
        "4 m headwaters source scaling should leave terrain, state, sink, and outlet unchanged");
    double doubled_total_source_rate_m3_per_s = 0.0;
    for (std::size_t index = 0U; index < scenario.source_depth_rate_m_per_s.size(); ++index) {
        const float baseline_rate = scenario.source_depth_rate_m_per_s[index];
        const float doubled_rate = doubled_scenario.source_depth_rate_m_per_s[index];
        require((baseline_rate > 0.0F) == (doubled_rate > 0.0F),
                "4 m headwaters scaling should preserve both authored source footprints");
        doubled_total_source_rate_m3_per_s +=
            static_cast<double>(doubled_rate) * config.cell_size_m * config.cell_size_m;
    }
    require_close(doubled_total_source_rate_m3_per_s, 1.0, kDepthToleranceM,
                  "4 m headwaters multiplier should double total source discharge");
    require(scenario.terrain_height_m.size() == fluid_25d_cell_count(config) &&
                scenario.cell_size_m == 4.0F,
            "sustained-headwaters geometry should use the fixed 65x33 four-metre grid");
    require(std::all_of(scenario.initial_water_depth_m.begin(),
                        scenario.initial_water_depth_m.end(),
                        [](float depth_m) { return depth_m == 0.0F; }),
            "sustained-headwaters control should start completely dry");
    require(std::all_of(scenario.sink_depth_rate_m_per_s.begin(),
                        scenario.sink_depth_rate_m_per_s.end(),
                        [](float rate_m_per_s) { return rate_m_per_s == 0.0F; }) &&
                scenario.sink_cell == kFluid25DNoCell,
            "sustained-headwaters control should have no explicit sink");
    require(!scenario.terrain_provenance.has_value(),
            "the authored control terrain should not claim imported-terrain provenance");

    const std::size_t source_a = fluid_25d_scenario_index(config.grid_width, config.grid_height,
                                                          kFluid25DSustainedHeadwatersSourceAX,
                                                          kFluid25DSustainedHeadwatersSourceAY);
    const std::size_t source_b = fluid_25d_scenario_index(config.grid_width, config.grid_height,
                                                          kFluid25DSustainedHeadwatersSourceBX,
                                                          kFluid25DSustainedHeadwatersSourceBY);
    const std::size_t confluence = fluid_25d_scenario_index(
        config.grid_width, config.grid_height, kFluid25DSustainedHeadwatersConfluenceX,
        kFluid25DSustainedHeadwatersConfluenceY);
    const std::size_t outlet =
        fluid_25d_scenario_index(config.grid_width, config.grid_height, config.grid_width - 1U,
                                 kFluid25DSustainedHeadwatersConfluenceY);
    require(
        scenario.source_cell == source_a && scenario.secondary_source_cell == source_b &&
            scenario.outlet_cell == outlet && scenario.sink_cell == kFluid25DNoCell,
        "headwater marker metadata should identify both inputs and the aperture without a sink");
    require_close(scenario.terrain_height_m[source_a], 1.45, kDepthToleranceM,
                  "first headwater bed should start at its authored elevation");
    require_close(scenario.terrain_height_m[source_b], 1.45, kDepthToleranceM,
                  "second headwater bed should start at its authored elevation");
    require_close(scenario.terrain_height_m[confluence], 0.57, kDepthToleranceM,
                  "both tributary beds should meet at the shared confluence elevation");
    require_close(scenario.terrain_height_m[outlet], 0.0, kDepthToleranceM,
                  "the trunk bed should fall to the downstream aperture");
    const std::size_t bank =
        fluid_25d_scenario_index(config.grid_width, config.grid_height, 40U, 12U);
    require(scenario.terrain_height_m[bank] > scenario.terrain_height_m[confluence] + 0.25F,
            "confluence terrain should include a raised bank outside the channel");

    const float cell_area_m2 = config.cell_size_m * config.cell_size_m;
    const float expected_source_rate_m_per_s =
        kFluid25DSustainedHeadwatersDischargePerSourceM3PerS / (9.0F * cell_area_m2);
    std::size_t source_cell_count = 0U;
    double total_source_rate_m3_per_s = 0.0;
    for (std::uint32_t y = 0U; y < config.grid_height; ++y) {
        for (std::uint32_t x = 0U; x < config.grid_width; ++x) {
            const std::size_t index =
                fluid_25d_scenario_index(config.grid_width, config.grid_height, x, y);
            const bool in_source_a = x >= kFluid25DSustainedHeadwatersSourceAX - 1U &&
                                     x <= kFluid25DSustainedHeadwatersSourceAX + 1U &&
                                     y >= kFluid25DSustainedHeadwatersSourceAY - 1U &&
                                     y <= kFluid25DSustainedHeadwatersSourceAY + 1U;
            const bool in_source_b = x >= kFluid25DSustainedHeadwatersSourceBX - 1U &&
                                     x <= kFluid25DSustainedHeadwatersSourceBX + 1U &&
                                     y >= kFluid25DSustainedHeadwatersSourceBY - 1U &&
                                     y <= kFluid25DSustainedHeadwatersSourceBY + 1U;
            const bool is_source = in_source_a || in_source_b;
            const float rate_m_per_s = scenario.source_depth_rate_m_per_s[index];
            require(is_source == (rate_m_per_s > 0.0F),
                    "headwater forcing should occupy exactly the two authored 3x3 patches");
            if (is_source) {
                ++source_cell_count;
                require_close(
                    rate_m_per_s, expected_source_rate_m_per_s, kDepthToleranceM,
                    "each source patch should be normalized to 0.25 cubic metres per second");
            }
            total_source_rate_m3_per_s +=
                static_cast<double>(rate_m_per_s) * static_cast<double>(cell_area_m2);

            const bool aperture_face = x == config.grid_width - 1U &&
                                       y >= kFluid25DSustainedHeadwatersOutletYFirst &&
                                       y <= kFluid25DSustainedHeadwatersOutletYLast;
            const std::uint32_t expected_mask = aperture_face ? kFluid25DBoundaryOutflowRight : 0U;
            require(scenario.boundary_outflow_face_mask[index] == expected_mask,
                    "only the three east-facing aperture cells should be open");
        }
    }
    require(source_cell_count == 18U,
            "sustained-headwaters control should contain two disjoint nine-cell sources");
    require_close(total_source_rate_m3_per_s, 0.50, kDepthToleranceM,
                  "two normalized headwaters should combine to 0.50 cubic metres per second");
    validate_fluid_25d_boundary_outflow_face_mask(config.grid_width, config.grid_height,
                                                  scenario.boundary_outflow_face_mask);

    const Fluid25DEndpointMarkersGpu markers = fluid_25d_endpoint_markers(config, scenario);
    require(sizeof(markers) == sizeof(float) * 8U &&
                offsetof(Fluid25DEndpointMarkersGpu, secondary_source_xy_reserved) ==
                    sizeof(float) * 4U,
            "endpoint-marker host layout should match two std430 vec4 values");
    require(markers.source_xy_outlet_xy == std::array<float, 4U>{4.0F, 8.0F, 64.0F, 16.0F} &&
                markers.secondary_source_xy_reserved ==
                    std::array<float, 4U>{4.0F, 24.0F, -1.0F, -1.0F},
            "headwater markers should map source A, source B, and the east aperture exactly");

    Fluid25DConfig legacy_config = config;
    legacy_config.scenario = Fluid25DScenario::SourceOutletDemo;
    legacy_config.grid_width = 10U;
    Fluid25DScenarioData legacy_scenario;
    legacy_scenario.source_cell = 13U;
    legacy_scenario.sink_cell = 27U;
    const Fluid25DEndpointMarkersGpu legacy_markers =
        fluid_25d_endpoint_markers(legacy_config, legacy_scenario);
    require(legacy_markers.source_xy_outlet_xy == std::array<float, 4U>{3.0F, 1.0F, 7.0F, 2.0F} &&
                legacy_markers.secondary_source_xy_reserved ==
                    std::array<float, 4U>{-1.0F, -1.0F, -1.0F, -1.0F},
            "extended endpoint references should preserve source/outlet coordinates");

    const auto read_shader = [](const std::filesystem::path& path) {
        const auto shared = std::filesystem::path(__FILE__)
                                .parent_path()
                                .parent_path()
                                .parent_path()
                                .parent_path()
                                .parent_path() /
                            "shaders/cubey/terrain/surface_patch" / path.filename();
        std::ifstream stream(
            path.parent_path().filename() == "shaders" && std::filesystem::exists(shared) ? shared
                                                                                          : path);
        if (!stream) {
            throw std::runtime_error("failed to read endpoint-marker shader layout");
        }
        return std::string(std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>());
    };
    const std::filesystem::path shader_directory =
        std::filesystem::path(__FILE__).parent_path() / "shaders";
    const std::string terrain_shader = read_shader(shader_directory / "fluid_25d_terrain.frag");
    const std::string water_shader = read_shader(shader_directory / "fluid_25d_water.frag");
    require(terrain_shader.find("endpoint_annulus") == std::string::npos &&
                water_shader.find("endpoint_annulus") == std::string::npos,
            "terrain and water should no longer draw oversized point-reference rings");
    const auto cube_shader = read_shader(shader_directory / "fluid_25d_forcing_cubes.vert");
    require(cube_shader.find("readonly buffer ForcingCubes") != std::string::npos &&
                cube_shader.find("cubes.values[gl_InstanceIndex]") != std::string::npos &&
                cube_shader.find("float half_side = 0.5 * params.grid_cell.z;") !=
                    std::string::npos &&
                cube_shader.find("center.y *= params.grid_cell.w;") != std::string::npos &&
                cube_shader.find("high = clamp(neighbor_offset - half_side") != std::string::npos &&
                cube_shader.find("low = clamp(neighbor_offset + half_side") != std::string::npos,
            "forcing cubes must use read-only actual cell instances and omit only shared overlap");
}

void test_sustained_headwaters_refined_scenario_construction() {
    using namespace cubey::projects::fluid::fluid_25d;
    struct Resolution {
        std::uint32_t width;
        std::uint32_t height;
        float cell_size_m;
        std::uint32_t expected_source_cells_per_patch;
        std::uint32_t outlet_first_row;
        std::uint32_t outlet_face_count;
        double expected_outlet_center_y_m;
    };
    constexpr std::array<Resolution, 2U> resolutions{{
        {kFluid25DSustainedHeadwatersGridWidth2m, kFluid25DSustainedHeadwatersGridHeight2m,
         kFluid25DSustainedHeadwatersCellSize2m, 49U, 29U, 6U, 63.0},
        {kFluid25DSustainedHeadwatersGridWidth1m, kFluid25DSustainedHeadwatersGridHeight1m,
         kFluid25DSustainedHeadwatersCellSize1m, 169U, 58U, 12U, 63.5},
    }};

    for (const Resolution& resolution : resolutions) {
        Fluid25DConfig config;
        config.scenario = Fluid25DScenario::SustainedHeadwatersDemo;
        config.solver = Fluid25DSolver::FiniteVolume;
        config.grid_width = resolution.width;
        config.grid_height = resolution.height;
        config.cell_size_m = resolution.cell_size_m;
        validate_fluid_25d_config(config);

        const Fluid25DScenarioData scenario = make_fluid_25d_scenario(
            config.scenario, config.grid_width, config.grid_height, config.cell_size_m);
        require(scenario.width == resolution.width && scenario.height == resolution.height &&
                    scenario.cell_size_m == resolution.cell_size_m &&
                    scenario.terrain_height_m.size() == fluid_25d_cell_count(config),
                "refined sustained-headwaters fields should match the selected resolution");
        require(std::all_of(scenario.initial_water_depth_m.begin(),
                            scenario.initial_water_depth_m.end(),
                            [](float depth_m) { return depth_m == 0.0F; }) &&
                    std::all_of(scenario.sink_depth_rate_m_per_s.begin(),
                                scenario.sink_depth_rate_m_per_s.end(),
                                [](float rate_m_per_s) { return rate_m_per_s == 0.0F; }) &&
                    scenario.sink_cell == kFluid25DNoCell &&
                    !scenario.terrain_provenance.has_value(),
                "refined headwaters should remain dry-started, sink-free, authored controls");

        const auto scaled_cell = [cell_size_m = config.cell_size_m](std::uint32_t reference_cell) {
            return static_cast<std::uint32_t>(
                std::lround(static_cast<float>(reference_cell) * 4.0F / cell_size_m));
        };
        const std::uint32_t source_ax = scaled_cell(kFluid25DSustainedHeadwatersSourceAX);
        const std::uint32_t source_ay = scaled_cell(kFluid25DSustainedHeadwatersSourceAY);
        const std::uint32_t source_bx = scaled_cell(kFluid25DSustainedHeadwatersSourceBX);
        const std::uint32_t source_by = scaled_cell(kFluid25DSustainedHeadwatersSourceBY);
        const std::uint32_t confluence_x = scaled_cell(kFluid25DSustainedHeadwatersConfluenceX);
        const std::uint32_t confluence_y = scaled_cell(kFluid25DSustainedHeadwatersConfluenceY);
        const std::uint32_t outlet_x = config.grid_width - 1U;
        const std::size_t source_a =
            fluid_25d_scenario_index(config.grid_width, config.grid_height, source_ax, source_ay);
        const std::size_t source_b =
            fluid_25d_scenario_index(config.grid_width, config.grid_height, source_bx, source_by);
        const std::size_t confluence = fluid_25d_scenario_index(
            config.grid_width, config.grid_height, confluence_x, confluence_y);
        const std::size_t outlet =
            fluid_25d_scenario_index(config.grid_width, config.grid_height, outlet_x, confluence_y);
        require(scenario.source_cell == source_a && scenario.secondary_source_cell == source_b &&
                    scenario.outlet_cell == outlet,
                "refined endpoint metadata should preserve the authored physical locations");
        require_close(scenario.terrain_height_m[source_a], 1.45, kDepthToleranceM,
                      "refined source A should retain its authored bed elevation");
        require_close(scenario.terrain_height_m[source_b], 1.45, kDepthToleranceM,
                      "refined source B should retain its authored bed elevation");
        require_close(scenario.terrain_height_m[confluence], 0.57, kDepthToleranceM,
                      "refined tributaries should retain the authored confluence elevation");
        require_close(scenario.terrain_height_m[outlet], 0.0, kDepthToleranceM,
                      "refined trunk should retain its authored outlet elevation");
        const std::uint32_t bank_x = scaled_cell(40U);
        const std::uint32_t bank_y = scaled_cell(12U);
        require(scenario.terrain_height_m[fluid_25d_scenario_index(
                    config.grid_width, config.grid_height, bank_x, bank_y)] >
                    scenario.terrain_height_m[confluence] + 0.25F,
                "refined channel should retain the raised bank at the same physical location");

        const double cell_area_m2 = static_cast<double>(config.cell_size_m) * config.cell_size_m;
        std::size_t source_a_cell_count = 0U;
        std::size_t source_b_cell_count = 0U;
        double source_a_rate_m3_per_s = 0.0;
        double source_b_rate_m3_per_s = 0.0;
        for (std::uint32_t y = 0U; y < config.grid_height; ++y) {
            for (std::uint32_t x = 0U; x < config.grid_width; ++x) {
                const std::size_t index =
                    fluid_25d_scenario_index(config.grid_width, config.grid_height, x, y);
                const float rate_m_per_s = scenario.source_depth_rate_m_per_s[index];
                require(std::isfinite(rate_m_per_s) && rate_m_per_s >= 0.0F,
                        "refined source rates should remain finite and nonnegative");
                if (rate_m_per_s > 0.0F) {
                    const double cell_center_y_m =
                        static_cast<double>(y) * static_cast<double>(config.cell_size_m);
                    if (cell_center_y_m < 64.0) {
                        ++source_a_cell_count;
                        source_a_rate_m3_per_s += static_cast<double>(rate_m_per_s) * cell_area_m2;
                    } else {
                        ++source_b_cell_count;
                        source_b_rate_m3_per_s += static_cast<double>(rate_m_per_s) * cell_area_m2;
                    }
                }
            }
        }
        require(source_a_cell_count == resolution.expected_source_cells_per_patch &&
                    source_b_cell_count == resolution.expected_source_cells_per_patch,
                "refined source patches should rasterize the complete physical footprint");
        require_close(source_a_rate_m3_per_s, kFluid25DSustainedHeadwatersDischargePerSourceM3PerS,
                      1.0e-7,
                      "refined source A should integrate to exactly 0.25 cubic metres per second");
        require_close(source_b_rate_m3_per_s, kFluid25DSustainedHeadwatersDischargePerSourceM3PerS,
                      1.0e-7,
                      "refined source B should integrate to exactly 0.25 cubic metres per second");
        const std::size_t full_cell_source =
            fluid_25d_scenario_index(config.grid_width, config.grid_height, source_ax, source_ay);
        require_close(scenario.source_depth_rate_m_per_s[full_cell_source], 0.25 / 144.0, 1.0e-8,
                      "the full-overlap source cells should preserve the 12x12 metre flux density");

        std::size_t open_face_count = 0U;
        double open_face_center_y_sum_m = 0.0;
        for (std::uint32_t y = 0U; y < config.grid_height; ++y) {
            for (std::uint32_t x = 0U; x < config.grid_width; ++x) {
                const std::size_t index =
                    fluid_25d_scenario_index(config.grid_width, config.grid_height, x, y);
                const bool expected_open =
                    x == outlet_x && y >= resolution.outlet_first_row &&
                    y < resolution.outlet_first_row + resolution.outlet_face_count;
                const std::uint32_t expected_mask =
                    expected_open ? kFluid25DBoundaryOutflowRight : 0U;
                require(scenario.boundary_outflow_face_mask[index] == expected_mask,
                        "refined headwaters should open only the chosen east aperture faces");
                if (expected_open) {
                    ++open_face_count;
                    open_face_center_y_sum_m +=
                        static_cast<double>(y) * static_cast<double>(config.cell_size_m);
                }
            }
        }
        require(open_face_count == resolution.outlet_face_count,
                "refined outlet should preserve the twelve metre full-face aperture width");
        require_close(open_face_center_y_sum_m / static_cast<double>(open_face_count),
                      resolution.expected_outlet_center_y_m, kDepthToleranceM,
                      "binary aperture rows should use the documented lower-y centering tie");
        validate_fluid_25d_boundary_outflow_face_mask(config.grid_width, config.grid_height,
                                                      scenario.boundary_outflow_face_mask);

        const Fluid25DEndpointMarkersGpu markers = fluid_25d_endpoint_markers(config, scenario);
        require(markers.source_xy_outlet_xy ==
                        std::array<float, 4U>{
                            static_cast<float>(source_ax), static_cast<float>(source_ay),
                            static_cast<float>(outlet_x), static_cast<float>(confluence_y)} &&
                    markers.secondary_source_xy_reserved ==
                        std::array<float, 4U>{static_cast<float>(source_bx),
                                              static_cast<float>(source_by), -1.0F, -1.0F},
                "refined render markers should use the corresponding physical endpoint cells");

        if (config.cell_size_m == kFluid25DSustainedHeadwatersCellSize1m) {
            constexpr std::array<float, 4U> source_scales{{1.0F, 2.0F, 4.0F, 8.0F}};
            for (const float source_scale : source_scales) {
                const Fluid25DScenarioData scaled =
                    make_fluid_25d_scenario(config.scenario, config.grid_width, config.grid_height,
                                            config.cell_size_m, source_scale);
                require(scaled.terrain_height_m == scenario.terrain_height_m &&
                            scaled.initial_water_depth_m == scenario.initial_water_depth_m &&
                            scaled.sink_depth_rate_m_per_s == scenario.sink_depth_rate_m_per_s &&
                            scaled.boundary_outflow_face_mask ==
                                scenario.boundary_outflow_face_mask &&
                            scaled.source_cell == scenario.source_cell &&
                            scaled.secondary_source_cell == scenario.secondary_source_cell &&
                            scaled.outlet_cell == scenario.outlet_cell &&
                            scaled.sink_cell == scenario.sink_cell,
                        "headwaters source scaling should leave terrain, sinks, outlet, and clocks "
                        "unchanged");

                std::size_t source_a_cells = 0U;
                std::size_t source_b_cells = 0U;
                double scaled_source_a_rate_m3_per_s = 0.0;
                double scaled_source_b_rate_m3_per_s = 0.0;
                for (std::uint32_t y = 0U; y < config.grid_height; ++y) {
                    for (std::uint32_t x = 0U; x < config.grid_width; ++x) {
                        const std::size_t index =
                            fluid_25d_scenario_index(config.grid_width, config.grid_height, x, y);
                        const float baseline_rate = scenario.source_depth_rate_m_per_s[index];
                        const float scaled_rate = scaled.source_depth_rate_m_per_s[index];
                        require(
                            (baseline_rate > 0.0F) == (scaled_rate > 0.0F),
                            "headwaters source scaling should preserve the 1 m source footprint");
                        if (scaled_rate > 0.0F) {
                            const double cell_center_y_m =
                                static_cast<double>(y) * config.cell_size_m;
                            if (cell_center_y_m < 64.0) {
                                ++source_a_cells;
                                scaled_source_a_rate_m3_per_s +=
                                    static_cast<double>(scaled_rate) * cell_area_m2;
                            } else {
                                ++source_b_cells;
                                scaled_source_b_rate_m3_per_s +=
                                    static_cast<double>(scaled_rate) * cell_area_m2;
                            }
                        }
                    }
                }
                require(source_a_cells == resolution.expected_source_cells_per_patch &&
                            source_b_cells == resolution.expected_source_cells_per_patch,
                        "scaled headwaters should retain both authored 1 m source footprints");
                const double expected_rate_m3_per_s =
                    static_cast<double>(kFluid25DSustainedHeadwatersDischargePerSourceM3PerS) *
                    source_scale;
                const double rate_tolerance_m3_per_s = 2.0e-7 * source_scale;
                require_close(scaled_source_a_rate_m3_per_s, expected_rate_m3_per_s,
                              rate_tolerance_m3_per_s,
                              "scaled 1 m source A should integrate to its requested discharge");
                require_close(scaled_source_b_rate_m3_per_s, expected_rate_m3_per_s,
                              rate_tolerance_m3_per_s,
                              "scaled 1 m source B should integrate to its requested discharge");
            }
        }
    }
}

void test_terrain_case_ingestion() {
    using namespace cubey::projects::fluid::fluid_25d;
    TerrainFixture fixture;
    Fluid25DConfig config = test_config(3U, 2U, Fluid25DScenario::TerrainCase);
    config.cell_size_m = 3.5F;
    validate_fluid_25d_config(config);

    Fluid25DProjectConfig near_equal_project{};
    near_equal_project.simulation = config;
    near_equal_project.fluid.cell_size_m = std::nextafter(3.5F, 4.0F);
    resolve_fluid_25d_terrain_cell_size(near_equal_project, 3.5F);
    require(near_equal_project.simulation.cell_size_m == 3.5F,
            "terrain startup should canonicalize an explicitly near-equal cell size");

    const Fluid25DScenarioData terrain =
        load_fluid_25d_terrain_scenario(config, fixture.root, 1U, 1U);
    require(terrain.width == 3U && terrain.height == 2U,
            "terrain case should retain the configured crop dimensions");
    require_close(terrain.cell_size_m, 3.5, kDepthToleranceM,
                  "terrain case should propagate native sample spacing");
    const std::vector<float> expected{24.0F, 25.5F, 27.0F, 31.5F, 33.0F, 34.5F};
    require(terrain.terrain_height_m == expected,
            "terrain case should apply the manifest height transform at native samples");
    require(std::all_of(terrain.initial_water_depth_m.begin(), terrain.initial_water_depth_m.end(),
                        [](float value) { return value == 0.0F; }) &&
                std::all_of(terrain.source_depth_rate_m_per_s.begin(),
                            terrain.source_depth_rate_m_per_s.end(),
                            [](float value) { return value == 0.0F; }) &&
                std::all_of(terrain.sink_depth_rate_m_per_s.begin(),
                            terrain.sink_depth_rate_m_per_s.end(),
                            [](float value) { return value == 0.0F; }),
            "terrain case should begin with zero water and no source or sink rates");
    require(terrain.source_cell == kFluid25DNoCell && terrain.sink_cell == kFluid25DNoCell,
            "terrain case should not invent source or sink cells");

    const cubey::asset::TerrainRasterHeightSource source(fixture.root);
    const Fluid25DScenarioData crop = make_fluid_25d_terrain_crop(3U, 2U, 3.5F, source, 1U, 1U);
    require(crop.width == terrain.width && crop.height == terrain.height &&
                crop.cell_size_m == terrain.cell_size_m &&
                crop.terrain_height_m == terrain.terrain_height_m &&
                crop.initial_water_depth_m == terrain.initial_water_depth_m &&
                crop.source_depth_rate_m_per_s == terrain.source_depth_rate_m_per_s &&
                crop.sink_depth_rate_m_per_s == terrain.sink_depth_rate_m_per_s &&
                crop.boundary_outflow_face_mask == terrain.boundary_outflow_face_mask,
            "scenario-neutral terrain crop helper should preserve the neutral terrain fields");
    require_throws(
        [&] { static_cast<void>(make_fluid_25d_terrain_crop(0U, 2U, 3.5F, source, 1U, 1U)); },
        "scenario-neutral terrain crop helper should reject zero dimensions");
    require_throws(
        [&] { static_cast<void>(make_fluid_25d_terrain_crop(3U, 2U, 0.0F, source, 1U, 1U)); },
        "scenario-neutral terrain crop helper should reject non-positive spacing");
    require(terrain.terrain_provenance.has_value(),
            "terrain case should expose provenance for inspection");
    const Fluid25DTerrainCaseProvenance& provenance = terrain.terrain_provenance.value();
    require(provenance.source_id == "fluid-25d-test-terrain" && provenance.crop_x == 1U &&
                provenance.crop_z == 1U && provenance.crop_width == 3U &&
                provenance.crop_height == 2U,
            "terrain case provenance should retain source and crop identity fields");
    require(provenance.elevation_sha256 ==
                fixture.manifest["files"]["elevation"]["sha256"].get<std::string>(),
            "terrain case provenance should retain the elevation SHA-256");
    require(provenance.transformed_crop_sha256.size() == 64U &&
                provenance.identity.find("elevation-sha256=" + provenance.elevation_sha256) !=
                    std::string::npos &&
                provenance.identity.find("crop-sha256=" + provenance.transformed_crop_sha256) !=
                    std::string::npos &&
                provenance.identity.find("crop=1,1,3x2") != std::string::npos &&
                provenance.identity.find("spacing-m=3.500000") != std::string::npos,
            "terrain case identity should include source/crop digests, crop, and spacing");
    require(provenance.manifest_path ==
                std::filesystem::absolute(fixture.root / "heightfield.json").lexically_normal(),
            "terrain case provenance should expose the normalized manifest path");

    const Fluid25DTerrainCaseProvenance baseline_provenance = provenance;
    fixture.manifest["height"]["offset_m"] = 11.0F;
    fixture.write();
    const Fluid25DScenarioData transformed =
        load_fluid_25d_terrain_scenario(config, fixture.root, 1U, 1U);
    require(transformed.terrain_provenance.has_value(),
            "terrain case should retain provenance after a source transform change");
    require(transformed.terrain_provenance->elevation_sha256 ==
                baseline_provenance.elevation_sha256,
            "terrain transform changes should retain the original elevation SHA-256");
    require(transformed.terrain_provenance->transformed_crop_sha256 !=
                    baseline_provenance.transformed_crop_sha256 &&
                transformed.terrain_provenance->identity != baseline_provenance.identity,
            "terrain transform changes should change the transformed crop digest and identity");

    require_throws(
        [&] { static_cast<void>(load_fluid_25d_terrain_scenario(config, fixture.root, 3U, 1U)); },
        "terrain case should reject an x crop that exceeds the source bounds");
    require_throws(
        [&] { static_cast<void>(load_fluid_25d_terrain_scenario(config, fixture.root, 1U, 3U)); },
        "terrain case should reject a z crop that exceeds the source bounds");
    Fluid25DConfig conflicting = config;
    conflicting.cell_size_m = 1.0F;
    require_throws(
        [&] {
            static_cast<void>(load_fluid_25d_terrain_scenario(conflicting, fixture.root, 1U, 1U));
        },
        "terrain case should reject an explicit cell size that conflicts with native spacing");
    require_throws(
        [&] {
            static_cast<void>(load_fluid_25d_terrain_scenario(
                config, fixture.root / "missing-heightfield", 0U, 0U));
        },
        "terrain case should reject a missing heightfield path");
    require_throws(
        [&] {
            static_cast<void>(
                load_fluid_25d_terrain_scenario(config, fixture.root / "invalid.txt", 0U, 0U));
        },
        "terrain case should reject an invalid heightfield path");
}

void test_hillside_rain_study_scenario_construction() {
    using namespace cubey::projects::fluid::fluid_25d;
    TerrainFixture fixture(30.0F, 10U, 10U);
    const Fluid25DConfig config = rain_study_test_config(8U, 8U, 100.0F);
    const cubey::asset::TerrainRasterHeightSource source(fixture.root);
    const Fluid25DScenarioData scenario =
        make_fluid_25d_hillside_rain_study_scenario(config, source, 1U, 1U);

    const std::size_t cells = fluid_25d_cell_count(config);
    require(scenario.width == config.grid_width && scenario.height == config.grid_height &&
                scenario.cell_size_m == 30.0F && scenario.terrain_provenance.has_value() &&
                scenario.terrain_provenance->crop_x == 1U &&
                scenario.terrain_provenance->crop_z == 1U,
            "rain crop builder should reuse native terrain crop provenance unchanged");
    require(scenario.source_cell == kFluid25DNoCell &&
                scenario.secondary_source_cell == kFluid25DNoCell &&
                scenario.outlet_cell == kFluid25DNoCell && scenario.sink_cell == kFluid25DNoCell,
            "rain crop should carry no point-source, outlet, or sink sentinels");
    require(std::all_of(scenario.initial_water_depth_m.begin(),
                        scenario.initial_water_depth_m.end(),
                        [](float depth) { return depth == 0.0F; }) &&
                std::all_of(scenario.sink_depth_rate_m_per_s.begin(),
                            scenario.sink_depth_rate_m_per_s.end(),
                            [](float rate) { return rate == 0.0F; }),
            "rain crop should begin dry with no sink field");
    require(std::all_of(scenario.source_depth_rate_m_per_s.begin(),
                        scenario.source_depth_rate_m_per_s.end(),
                        [&](float rate) { return rate == config.rainfall_depth_rate_m_per_s; }),
            "rain should distribute its one configured depth rate uniformly over the crop");
    require(scenario.boundary_outflow_face_mask == make_fluid_25d_all_outward_boundary_outflow_mask(
                                                       config.grid_width, config.grid_height),
            "rain should use the existing outward-only full perimeter mask");
    require(!fluid_25d_uses_forcing_cubes(config.scenario) &&
                fluid_25d_forcing_cubes(config, scenario).empty(),
            "rain must not display source or outlet forcing cubes");

    Fluid25DScenarioData invalid = scenario;
    invalid.source_cell = 0U;
    require_throws([&] { apply_fluid_25d_hillside_rain_study_forcing(config, invalid); },
                   "rain forcing should reject a point-source sentinel");
    invalid = scenario;
    invalid.sink_depth_rate_m_per_s[0] = 1.0F;
    require_throws([&] { apply_fluid_25d_hillside_rain_study_forcing(config, invalid); },
                   "rain forcing should reject a pre-existing sink field");
    require(cells == scenario.source_depth_rate_m_per_s.size(),
            "rain crop fields should retain the configured full grid size");
}

void test_natural_flow_recipe_import_and_validation() {
    using namespace cubey::projects::fluid::fluid_25d;
    TerrainFixture fixture(30.0F, 7U, 7U);
    const nlohmann::json document = make_synthetic_natural_flow_recipe_document(fixture);
    const Fluid25DNaturalFlowRecipe recipe = parse_fluid_25d_natural_flow_recipe(document.dump());
    const Fluid25DConfig config = natural_flow_test_config();
    const cubey::asset::TerrainRasterHeightSource source(fixture.root);
    const Fluid25DScenarioData raw_crop =
        make_fluid_25d_terrain_crop(5U, 5U, 30.0F, source, 1U, 1U);
    const Fluid25DScenarioData scenario = make_fluid_25d_natural_flow_study_scenario(
        config, source, recipe, recipe.crop_x, recipe.crop_z);

    require(scenario.terrain_height_m == raw_crop.terrain_height_m &&
                scenario.terrain_provenance->transformed_crop_sha256 ==
                    raw_crop.terrain_provenance->transformed_crop_sha256,
            "natural-flow study must preserve the exact transformed native terrain crop");
    require(std::all_of(scenario.initial_water_depth_m.begin(),
                        scenario.initial_water_depth_m.end(),
                        [](float value) { return value == 0.0F; }) &&
                std::all_of(scenario.sink_depth_rate_m_per_s.begin(),
                            scenario.sink_depth_rate_m_per_s.end(),
                            [](float value) { return value == 0.0F; }) &&
                scenario.sink_cell == kFluid25DNoCell,
            "natural-flow study must keep the imported terrain dry with no sink field or marker");
    require(
        scenario.source_cell == fluid_25d_scenario_index(5U, 5U, 2U, 2U) &&
            scenario.outlet_cell == fluid_25d_scenario_index(5U, 5U, 0U, 2U) &&
            scenario.natural_flow_study.has_value() &&
            scenario.natural_flow_study->source_cells.size() == 5U,
        "natural-flow study should retain five source cells and an observation-only exit marker");
    const float expected_depth_rate = 30.0F / (5.0F * 30.0F * 30.0F);
    double source_volume_rate_m3_per_s = 0.0;
    for (const float depth_rate : scenario.source_depth_rate_m_per_s) {
        require(depth_rate == 0.0F || depth_rate == expected_depth_rate,
                "natural-flow source depth rate should be equal across the exact five cells");
        source_volume_rate_m3_per_s += static_cast<double>(depth_rate) * 30.0 * 30.0;
    }
    require(std::count_if(scenario.source_depth_rate_m_per_s.begin(),
                          scenario.source_depth_rate_m_per_s.end(),
                          [](float value) { return value > 0.0F; }) == 5,
            "natural-flow source footprint should contain exactly five nonzero cells");
    require_close(source_volume_rate_m3_per_s, config.natural_flow_source_m3_per_s, 1.0e-6,
                  "five-cell source volume rate should sum to the explicit total Q exactly once");
    require(scenario.boundary_outflow_face_mask ==
                make_fluid_25d_all_outward_boundary_outflow_mask(5U, 5U),
            "natural-flow study should open outward-only faces on every crop perimeter");
    const Fluid25DEndpointMarkersGpu markers = fluid_25d_endpoint_markers(config, scenario);
    require(markers.source_xy_outlet_xy == std::array<float, 4>{2.0F, 2.0F, 0.0F, 2.0F} &&
                markers.secondary_source_xy_reserved[0] < 0.0F &&
                markers.secondary_source_xy_reserved[1] < 0.0F,
            "natural-flow endpoint references should retain source and expected-exit coordinates");

    const auto reject_json = [](nlohmann::json invalid, const char* message) {
        require_throws(
            [&invalid] { static_cast<void>(parse_fluid_25d_natural_flow_recipe(invalid.dump())); },
            message);
    };
    require_throws([] { static_cast<void>(parse_fluid_25d_natural_flow_recipe("{malformed")); },
                   "natural-flow recipe parser should reject malformed JSON");
    nlohmann::json invalid = document;
    invalid["extra"] = true;
    reject_json(invalid, "natural-flow recipe parser should reject unknown top-level fields");
    invalid = document;
    invalid["elevation_sha256"] = "not-a-sha";
    reject_json(invalid, "natural-flow recipe parser should require well-formed SHA-256 hashes");
    invalid = document;
    invalid["candidate_id"] = "";
    reject_json(invalid, "natural-flow recipe parser should require a nonempty candidate id");
    invalid = document;
    invalid["cell_size_m"] = 30.0000001;
    reject_json(invalid,
                "natural-flow recipe parser should check native spacing before float rounding");
    invalid = document;
    invalid["source_center_cell_xz"] = {1U, 2U};
    reject_json(invalid,
                "natural-flow recipe parser should keep every source footprint cell interior");
    invalid = document;
    invalid["source_cells_xz"][4] = invalid["source_cells_xz"][0];
    reject_json(invalid, "natural-flow recipe parser should reject duplicate source cells");
    invalid = document;
    invalid["source_cells_xz"][4] = {0U, 2U};
    reject_json(invalid, "natural-flow recipe parser should reject edge-source cells");
    invalid = document;
    invalid["expected_outlet"]["crop_cell_xz_bounds"] = {{0U, 0U}, {0U, 2U}};
    reject_json(invalid,
                "natural-flow recipe parser should reject outlet windows that include corners");
    invalid = document;
    invalid["expected_outlet"]["crop_cell_xz_bounds"] = {{0U, 3U}, {0U, 2U}};
    reject_json(invalid, "natural-flow recipe parser should reject reversed outlet bounds");
    invalid = document;
    invalid["gauges"][0]["tangent_dx_dz"] = {std::numeric_limits<std::uint64_t>::max(), 0U};
    reject_json(invalid, "natural-flow recipe parser should reject unsigned tangent wraparound");
    invalid = document;
    invalid["gauges"][0]["tangent_dx_dz"] = {std::numeric_limits<std::int32_t>::min(), 0};
    reject_json(invalid, "natural-flow recipe parser should safely reject extreme tangents");
    invalid = document;
    invalid["gauges"][0]["tangent_dx_dz"] = {1, 1};
    reject_json(invalid, "natural-flow recipe parser should reject diagonal gauge tangents");
    invalid = document;
    invalid["gauges"][0]["half_span_cells"] = 0U;
    reject_json(invalid, "natural-flow recipe parser should require a positive gauge half-span");
    invalid = document;
    invalid["gauges"][0]["distance_m"] = "nan";
    reject_json(invalid, "natural-flow recipe parser should reject nonnumeric gauge distances");

    Fluid25DTerrainCaseProvenance mismatched_provenance = scenario.terrain_provenance.value();
    mismatched_provenance.elevation_sha256 = std::string(64U, '0');
    require_throws(
        [&] {
            validate_fluid_25d_natural_flow_recipe_provenance(recipe, mismatched_provenance, 5U, 5U,
                                                              30.0F);
        },
        "natural-flow recipe provenance gate should reject raw hash mismatches");
    mismatched_provenance = scenario.terrain_provenance.value();
    ++mismatched_provenance.crop_x;
    require_throws(
        [&] {
            validate_fluid_25d_natural_flow_recipe_provenance(recipe, mismatched_provenance, 5U, 5U,
                                                              30.0F);
        },
        "natural-flow recipe provenance gate should reject crop bbox mismatches");

    Fluid25DNaturalFlowRecipe crop_mismatch = recipe;
    ++crop_mismatch.crop_x;
    require_throws(
        [&] {
            static_cast<void>(make_fluid_25d_natural_flow_study_scenario(
                config, source, crop_mismatch, recipe.crop_x, recipe.crop_z));
        },
        "natural-flow scenario builder should reject crop-origin mismatches");
    Fluid25DNaturalFlowRecipe hash_mismatch = recipe;
    hash_mismatch.transformed_crop_sha256 = std::string(64U, '0');
    require_throws(
        [&] {
            static_cast<void>(make_fluid_25d_natural_flow_study_scenario(
                config, source, hash_mismatch, recipe.crop_x, recipe.crop_z));
        },
        "natural-flow scenario builder should reject transformed crop hash mismatches");

    Fluid25DConfig invalid_config = config;
    invalid_config.solver = Fluid25DSolver::VirtualPipes;
    require_throws([&] { validate_fluid_25d_config(invalid_config); },
                   "natural-flow study should require finite-volume");
    invalid_config = config;
    invalid_config.cell_size_m = 29.0F;
    require_throws([&] { validate_fluid_25d_config(invalid_config); },
                   "natural-flow study should require exact 30 m native cells");
    invalid_config = config;
    invalid_config.natural_flow_source_m3_per_s = 0.0F;
    require_throws([&] { validate_fluid_25d_config(invalid_config); },
                   "natural-flow study should require positive total Q");
    invalid_config = config;
    invalid_config.source_active_duration_seconds = 0.0F;
    require_throws([&] { validate_fluid_25d_config(invalid_config); },
                   "natural-flow study should reject source-duration scheduling");
    invalid_config = config;
    invalid_config.headwaters_source_scale = 2.0F;
    invalid_config.source_active_duration_seconds.reset();
    require_throws([&] { validate_fluid_25d_config(invalid_config); },
                   "natural-flow study should reject non-default headwaters source scaling");
    invalid_config = config;
    invalid_config.terrain_water_protocol = Fluid25DTerrainWaterProtocol::RainPulse;
    invalid_config.rainfall_depth_rate_m_per_s = 0.001F;
    require_throws([&] { validate_fluid_25d_config(invalid_config); },
                   "natural-flow study should reject rain forcing");
    invalid_config = config;
    invalid_config.terrain_water_protocol = Fluid25DTerrainWaterProtocol::SheetRelease;
    invalid_config.sheet_initial_depth_m = 0.1F;
    require_throws([&] { validate_fluid_25d_config(invalid_config); },
                   "natural-flow study should reject sheet forcing");
    Fluid25DConfig with_dye = config;
    with_dye.dye_pulse_start_seconds = 10.0F;
    with_dye.dye_pulse_duration_seconds = 5.0F;
    validate_fluid_25d_config(with_dye);
    require(fluid_25d_transport_inspection_available(with_dye),
            "natural-flow study should support the existing conservative dye pulse path");

    const std::vector<std::string> valid_cli{"fluid_25d",
                                             "--fluid25d-scenario",
                                             "natural-flow-study",
                                             "--fluid25d-solver",
                                             "finite-volume",
                                             "--grid-width",
                                             "5",
                                             "--grid-height",
                                             "5",
                                             "--fluid25d-cell-size-m",
                                             "30",
                                             "--terrain-heightfield",
                                             "synthetic-terrain",
                                             "--fluid25d-terrain-crop-x",
                                             "1",
                                             "--fluid25d-terrain-crop-z",
                                             "1",
                                             "--fluid25d-natural-flow-recipe",
                                             "synthetic-recipe.json",
                                             "--fluid25d-natural-flow-source-m3-per-s",
                                             "30",
                                             "--fluid25d-natural-flow-home-pitch-radians",
                                             "-1.55",
                                             "--fluid25d-terrain-palette-low-m",
                                             "100",
                                             "--fluid25d-terrain-palette-high-m",
                                             "150"};
    const Fluid25DProjectConfig parsed = parse_project(valid_cli);
    require(parsed.simulation.scenario == Fluid25DScenario::NaturalFlowStudy &&
                parsed.simulation.natural_flow_source_m3_per_s == 30.0F &&
                parsed.natural_flow_recipe_path == "synthetic-recipe.json" &&
                parsed.natural_flow_home_pitch_radians == -1.55F &&
                parsed.catchment_render.terrain_palette_low_m == 100.0F &&
                parsed.catchment_render.terrain_palette_high_m == 150.0F,
            "natural-flow CLI should bind recipe, explicit total Q, render pitch, and palette");
    const auto reject_cli = [](std::vector<std::string> arguments, const char* message) {
        require_throws([&arguments] { static_cast<void>(parse_project(arguments)); }, message);
    };
    reject_cli({"fluid_25d", "--fluid25d-scenario", "natural-flow-study"},
               "natural-flow CLI should require explicit recipe and total source rate");
    reject_cli({"fluid_25d", "--fluid25d-natural-flow-recipe", "recipe.json"},
               "natural-flow recipe path should be rejected for unrelated scenarios");
    reject_cli({"fluid_25d", "--fluid25d-natural-flow-source-m3-per-s", "0"},
               "explicit neutral natural-flow Q should be rejected outside its scenario");
    reject_cli({"fluid_25d", "--fluid25d-scenario", "natural-flow-study", "--fluid25d-solver",
                "finite-volume", "--grid-width", "5", "--grid-height", "5",
                "--fluid25d-cell-size-m", "30", "--terrain-heightfield", "terrain",
                "--fluid25d-terrain-crop-x", "1", "--fluid25d-terrain-crop-z", "1",
                "--fluid25d-natural-flow-source-m3-per-s", "30"},
               "natural-flow CLI should require its recipe path");
    for (const std::vector<std::string>& forcing :
         {std::vector<std::string>{"--fluid25d-terrain-water-protocol", "none"},
          std::vector<std::string>{"--fluid25d-rainfall-rate-mm-per-hour", "0"},
          std::vector<std::string>{"--fluid25d-sheet-depth-m", "0"},
          std::vector<std::string>{"--fluid25d-source-active-duration-seconds", "0"},
          std::vector<std::string>{"--fluid25d-headwaters-source-scale", "1"}}) {
        std::vector<std::string> invalid_cli = valid_cli;
        invalid_cli.insert(invalid_cli.end(), forcing.begin(), forcing.end());
        reject_cli(std::move(invalid_cli),
                   "natural-flow CLI should reject every explicit terrain/headwaters forcing flag");
    }
    std::vector<std::string> unrelated_pitch{"fluid_25d",
                                             "--fluid25d-natural-flow-home-pitch-radians", "-0.72"};
    reject_cli(unrelated_pitch,
               "natural-flow camera pitch should be rejected outside natural-flow-study");
    std::vector<std::string> bad_pitch = valid_cli;
    bad_pitch[bad_pitch.size() - 5U] = "-1.56";
    reject_cli(bad_pitch, "natural-flow camera pitch should enforce its declared range");
    std::vector<std::string> dye_cli = valid_cli;
    dye_cli.insert(dye_cli.end(), {"--fluid25d-catchment-view", "transport-inspection",
                                   "--fluid25d-dye-pulse-start-seconds", "10",
                                   "--fluid25d-dye-pulse-duration-seconds", "5"});
    require(parse_project(std::move(dye_cli)).simulation.dye_pulse_duration_seconds == 5.0F,
            "natural-flow CLI should retain finite-volume transport-inspection dye timing");
}

void test_hillside_flow_study_contract() {
    using namespace cubey::projects::fluid::fluid_25d;
    TerrainFixture fixture(30.0F, 7U, 7U);
    nlohmann::json document = make_synthetic_natural_flow_recipe_document(fixture);
    document["schema"] = kFluid25DHillsideFlowRecipeSchema;
    document.erase("expected_outlet");
    document["gauges"] = nlohmann::json::array();
    const auto recipe = parse_fluid_25d_natural_flow_recipe(document.dump());
    Fluid25DConfig config = natural_flow_test_config();
    config.scenario = Fluid25DScenario::HillsideFlowStudy;
    validate_fluid_25d_config(config);
    require(!fluid_25d_transport_inspection_available(config),
            "water-only hillside must not advertise conserved dye");
    auto dyed_config = config;
    dyed_config.dye_pulse_start_seconds = 3600.0F;
    dyed_config.dye_pulse_duration_seconds = 120.0F;
    validate_fluid_25d_config(dyed_config);
    require(fluid_25d_transport_inspection_available(dyed_config),
            "explicit finite-volume hillside pulse should enable conserved transport inspection");
    const cubey::asset::TerrainRasterHeightSource source(fixture.root);
    auto scenario = make_fluid_25d_natural_flow_study_scenario(config, source, recipe, 1U, 1U);
    const auto raw = make_fluid_25d_terrain_crop(5U, 5U, 30.0F, source, 1U, 1U);
    require(
        !recipe.has_expected_outlet && !scenario.natural_flow_study->has_expected_outlet &&
            scenario.outlet_cell == kFluid25DNoCell && scenario.sink_cell == kFluid25DNoCell &&
            scenario.terrain_height_m == raw.terrain_height_m &&
            scenario.initial_water_depth_m == raw.initial_water_depth_m &&
            scenario.sink_depth_rate_m_per_s == raw.sink_depth_rate_m_per_s,
        "hillside source-only study must preserve dry raw terrain without sink or chosen outlet");
    const auto markers = fluid_25d_endpoint_markers(config, scenario);
    require(markers.source_xy_outlet_xy[0] >= 0.0F && markers.source_xy_outlet_xy[2] < 0.0F &&
                markers.source_xy_outlet_xy[3] < 0.0F,
            "hillside markers must not invent an amber destination");
    require_throws(
        [&] {
            auto invalid = document;
            invalid["expected_outlet"] = nullptr;
            static_cast<void>(parse_fluid_25d_natural_flow_recipe(invalid.dump()));
        },
        "hillside schema must reject outlet fields rather than silently ignore them");
    require_throws(
        [&] {
            static_cast<void>(make_fluid_25d_natural_flow_study_scenario(natural_flow_test_config(),
                                                                         source, recipe, 1U, 1U));
        },
        "hillside recipe cannot enter the old expected-outlet study");
    const auto old_recipe = parse_fluid_25d_natural_flow_recipe(
        make_synthetic_natural_flow_recipe_document(fixture).dump());
    require_throws(
        [&] {
            static_cast<void>(
                make_fluid_25d_natural_flow_study_scenario(config, source, old_recipe, 1U, 1U));
        },
        "expected-outlet recipe cannot enter the hillside study");

    // Uneven source patch: reference is its LOWEST bed, not the centre or mean.
    std::fill(scenario.terrain_height_m.begin(), scenario.terrain_height_m.end(), 1000.0F);
    scenario.terrain_height_m[scenario.source_cell] = 1100.0F;
    std::vector<float> depth(25U, 0.0F);
    depth[scenario.source_cell] = 1.0F;
    depth[scenario.natural_flow_study->source_cells[1]] = 1.0F;
    scenario.terrain_height_m[0U] = 900.0F;
    depth[0U] = 0.009F;
    auto progress = compute_fluid_25d_hillside_progress(config, scenario, depth);
    require(progress.source_minimum_bed_m == 1000.0 && progress.maximum_wetted_bed_drop_m == 0.0 &&
                progress.lower_band_wet_cells[0] == 0U && progress.materially_wet_cell_count == 2U,
            "uneven source and sub-threshold tails cannot falsely count downhill progress");
    depth[0U] = 0.1F;
    progress = compute_fluid_25d_hillside_progress(config, scenario, depth);
    require(progress.maximum_wetted_bed_drop_m == 100.0 && progress.lower_band_wet_cells[0] == 1U &&
                progress.lower_band_wet_cells[2] == 1U && progress.lower_band_wet_cells[3] == 0U,
            "hillside diagnostics must count nested elevation bands independent of exit");
    require_close(progress.lower_band_water_volume_m3[2], 90.0, 1e-5,
                  "hillside lower-band volume uses actual water depth and cell area");
    depth[0U] = -1.0F;
    require_throws(
        [&] { static_cast<void>(compute_fluid_25d_hillside_progress(config, scenario, depth)); },
        "hillside diagnostic must reject corrupt depth");

    Fluid25DInspectionAdvance advance;
    advance.request(600.0F, 2.0F);
    require_close(advance.progress(), 0.0, 0.0, "inspection progress begins at zero");
    std::uint32_t total = 0U;
    while (advance.remaining_steps() > 0U) {
        const auto batch = advance.take_batch();
        require(batch > 0U && batch <= 16U, "inspection advance must bound per-frame work");
        total += batch;
    }
    require(total == 300U && advance.take_batch() == 0U,
            "inspection advance must execute every fixed step exactly once");
    require_close(advance.progress(), 1.0, 0.0,
                  "inspection progress reaches one after all fixed steps");
    advance.request(5.0F, 2.0F);
    require(advance.remaining_steps() == 3U, "inspection advance rounds to full fixed steps");
    advance.reset();
    require(advance.remaining_steps() == 0U, "reset must cancel pending inspection advance");
    require_throws([&] { advance.request(600.0F, 0.0F); }, "advance rejects invalid time step");
    require_throws([&] { advance.request(7201.0F, 2.0F); }, "advance enforces bounded horizon");

    std::vector<std::string> args{"fluid_25d",
                                  "--fluid25d-scenario",
                                  "hillside-flow-study",
                                  "--fluid25d-solver",
                                  "finite-volume",
                                  "--grid-width",
                                  "5",
                                  "--grid-height",
                                  "5",
                                  "--fluid25d-cell-size-m",
                                  "30",
                                  "--terrain-heightfield",
                                  "synthetic-terrain",
                                  "--fluid25d-terrain-crop-x",
                                  "1",
                                  "--fluid25d-terrain-crop-z",
                                  "1",
                                  "--fluid25d-natural-flow-recipe",
                                  "synthetic-recipe.json",
                                  "--fluid25d-natural-flow-source-m3-per-s",
                                  "30",
                                  "--fluid25d-hillside-inspection-advance-seconds",
                                  "600",
                                  "--fluid25d-hillside-source-context"};
    const auto parsed = parse_project(args);
    require(parsed.simulation.scenario == Fluid25DScenario::HillsideFlowStudy &&
                parsed.hillside_inspection_advance_seconds == 600.0F &&
                parsed.hillside_source_context,
            "hillside study and explicit windowed inspection controls must parse");
    auto continuous_args = args;
    const auto pause_option = std::find(continuous_args.begin(), continuous_args.end(),
                                        "--fluid25d-hillside-inspection-advance-seconds");
    continuous_args.erase(pause_option, pause_option + 2);
    continuous_args.insert(continuous_args.end(),
                           {"--fluid25d-motion-markers", "--fluid25d-hillside-camera", "branch",
                            "--fluid25d-hillside-advance-and-continue-seconds", "3300"});
    const auto continuous = parse_project(continuous_args);
    require(continuous.motion_markers && continuous.hillside_camera == "branch" &&
                continuous.hillside_advance_and_continue_seconds == 3300.0F &&
                !continuous.hillside_inspection_advance_seconds &&
                continuous.simulation.natural_flow_source_m3_per_s ==
                    parsed.simulation.natural_flow_source_m3_per_s &&
                continuous.simulation.fixed_delta_seconds ==
                    parsed.simulation.fixed_delta_seconds &&
                continuous.simulation.simulation_substeps == parsed.simulation.simulation_substeps,
            "motion/camera/continuous advance are presentation controls and preserve hydraulic "
            "forcing");
    auto v5_args = continuous_args;
    *(std::find(v5_args.begin(), v5_args.end(), "--fluid25d-hillside-camera") + 1) = "travel";
    require_throws([&] { static_cast<void>(parse_project(v5_args)); },
                   "downstream camera rejects crops without the measured collection pocket");
    *(std::find(v5_args.begin(), v5_args.end(), "--grid-width") + 1) = "512";
    *(std::find(v5_args.begin(), v5_args.end(), "--grid-height") + 1) = "512";
    v5_args.insert(v5_args.end(),
                   {"--fluid25d-motion-marker-mode", "local", "--fluid25d-hillside-depth-cues"});
    const auto v5 = parse_project(v5_args);
    require(v5.motion_marker_mode == "local" && v5.catchment_render.hillside_depth_cues &&
                v5.hillside_camera == "travel" &&
                v5.simulation.natural_flow_source_m3_per_s == 30.0F,
            "V5 presentation opt-ins preserve reference forcing");
    auto invalid_response = v5_args;
    invalid_response.push_back("--fluid25d-hillside-supply-response");
    require_throws([&] { static_cast<void>(parse_project(invalid_response)); },
                   "response refuses non-Q100 reference");
    const auto source_value =
        std::find(v5_args.begin(), v5_args.end(), "--fluid25d-natural-flow-source-m3-per-s");
    *(source_value + 1) = "100";
    v5_args.push_back("--fluid25d-hillside-supply-response");
    require(parse_project(v5_args).hillside_supply_response, "Q100 response is explicitly opt-in");
    require_throws(
        [&] { static_cast<void>(parse_project({"fluid_25d", "--fluid25d-hillside-depth-cues"})); },
        "hillside depth palette cannot alter other scenarios");
    require_throws(
        [&] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-motion-marker-mode", "local"}));
        },
        "local marker mode requires opted-in markers");
    auto conflicted = args;
    conflicted.insert(conflicted.end(),
                      {"--fluid25d-hillside-advance-and-continue-seconds", "3300"});
    require_throws([&] { static_cast<void>(parse_project(conflicted)); },
                   "a requested inspection pause and automatic continue cannot coexist");
    continuous_args.push_back("--headless");
    require_throws([&] { static_cast<void>(parse_project(continuous_args)); },
                   "compute-and-continue cannot change deterministic headless time");
    require_throws(
        [&] { static_cast<void>(parse_project({"fluid_25d", "--fluid25d-motion-markers"})); },
        "motion markers cannot silently activate on unrelated product scenarios");
    auto dye_args = args;
    dye_args.insert(dye_args.end(), {"--fluid25d-dye-pulse-start-seconds", "3600",
                                     "--fluid25d-dye-pulse-duration-seconds", "120",
                                     "--fluid25d-catchment-view", "transport-inspection"});
    const auto dyed_parsed = parse_project(dye_args);
    require(fluid_25d_transport_inspection_available(dyed_parsed.simulation) &&
                !dyed_parsed.simulation.source_active_duration_seconds.has_value(),
            "hillside dye timing must remain independent of continuous hydraulic supply");
    args.push_back("--headless");
    require_throws([&] { static_cast<void>(parse_project(args)); },
                   "windowed advance must never alter deterministic headless timing");
}

void test_terrain_water_protocol_construction() {
    using namespace cubey::projects::fluid::fluid_25d;
    TerrainFixture fixture;
    Fluid25DConfig rain_config = test_config(3U, 2U, Fluid25DScenario::TerrainCase);
    rain_config.cell_size_m = 3.5F;
    rain_config.terrain_water_protocol = Fluid25DTerrainWaterProtocol::RainPulse;
    rain_config.rainfall_depth_rate_m_per_s =
        fluid_25d_rainfall_depth_rate_m_per_s_from_mm_per_hour(900.0F);
    rain_config.source_active_duration_seconds = 1.0F;
    const Fluid25DScenarioData rain =
        load_fluid_25d_terrain_scenario(rain_config, fixture.root, 1U, 1U);
    require(std::all_of(rain.initial_water_depth_m.begin(), rain.initial_water_depth_m.end(),
                        [](float value) { return value == 0.0F; }) &&
                std::all_of(rain.source_depth_rate_m_per_s.begin(),
                            rain.source_depth_rate_m_per_s.end(),
                            [&rain_config](float value) {
                                return value == rain_config.rainfall_depth_rate_m_per_s;
                            }),
            "rain-pulse should construct one uniform source field over immutable terrain");
    require(rain.boundary_outflow_face_mask ==
                make_fluid_25d_all_outward_boundary_outflow_mask(rain.width, rain.height),
            "rain-pulse should open every outward perimeter face");

    Fluid25DConfig sheet_config = test_config(3U, 2U, Fluid25DScenario::TerrainCase);
    sheet_config.cell_size_m = 3.5F;
    sheet_config.terrain_water_protocol = Fluid25DTerrainWaterProtocol::SheetRelease;
    sheet_config.sheet_initial_depth_m = 0.03F;
    const Fluid25DScenarioData sheet =
        load_fluid_25d_terrain_scenario(sheet_config, fixture.root, 1U, 1U);
    require(std::all_of(sheet.initial_water_depth_m.begin(), sheet.initial_water_depth_m.end(),
                        [&sheet_config](float value) {
                            return value == sheet_config.sheet_initial_depth_m;
                        }) &&
                std::all_of(sheet.source_depth_rate_m_per_s.begin(),
                            sheet.source_depth_rate_m_per_s.end(),
                            [](float value) { return value == 0.0F; }),
            "sheet-release should construct one uniform initial-depth field with no source");
    require(sheet.boundary_outflow_face_mask ==
                make_fluid_25d_all_outward_boundary_outflow_mask(sheet.width, sheet.height),
            "sheet-release should open every outward perimeter face");

    Fluid25DConfig invalid = sheet_config;
    invalid.sheet_initial_depth_m = 0.0F;
    require_throws(
        [&] { static_cast<void>(load_fluid_25d_terrain_scenario(invalid, fixture.root, 1U, 1U)); },
        "sheet-release construction should fail closed for zero depth");
}

void test_mountain_source_outlet_field_construction() {
    using namespace cubey::projects::fluid::fluid_25d;
    const std::size_t cell_count = fluid_25d_scenario_cell_count(
        kFluid25DMountainSourceOutletGridWidth, kFluid25DMountainSourceOutletGridHeight);
    Fluid25DScenarioData mountain{
        .width = kFluid25DMountainSourceOutletGridWidth,
        .height = kFluid25DMountainSourceOutletGridHeight,
        .cell_size_m = kFluid25DMountainSourceOutletCellSizeM,
        .terrain_height_m = std::vector<float>(cell_count, 0.0F),
        .initial_water_depth_m = std::vector<float>(cell_count, 1.0F),
        .source_depth_rate_m_per_s = std::vector<float>(cell_count, 1.0F),
        .sink_depth_rate_m_per_s = std::vector<float>(cell_count, 1.0F),
        .boundary_outflow_face_mask = std::vector<std::uint32_t>(cell_count, 7U),
    };
    for (std::size_t index = 0U; index < cell_count; ++index) {
        mountain.terrain_height_m[index] = static_cast<float>(index) * 0.001F;
    }
    const std::string expected_identity = fluid_25d_terrain_case_identity(
        kFluid25DMountainSourceOutletElevationSha256, kFluid25DMountainSourceOutletCropSha256,
        kFluid25DMountainSourceOutletCropX, kFluid25DMountainSourceOutletCropZ,
        kFluid25DMountainSourceOutletGridWidth, kFluid25DMountainSourceOutletGridHeight,
        kFluid25DMountainSourceOutletCellSizeM);
    mountain.terrain_provenance = Fluid25DTerrainCaseProvenance{
        .manifest_path = "synthetic-mountain-heightfield.json",
        .source_id = "synthetic-mountain-source",
        .elevation_sha256 = std::string(kFluid25DMountainSourceOutletElevationSha256),
        .transformed_crop_sha256 = std::string(kFluid25DMountainSourceOutletCropSha256),
        .crop_x = kFluid25DMountainSourceOutletCropX,
        .crop_z = kFluid25DMountainSourceOutletCropZ,
        .crop_width = kFluid25DMountainSourceOutletGridWidth,
        .crop_height = kFluid25DMountainSourceOutletGridHeight,
        .sample_spacing_m = kFluid25DMountainSourceOutletCellSizeM,
        .identity = expected_identity,
    };
    const std::vector<float> immutable_terrain = mountain.terrain_height_m;
    validate_fluid_25d_mountain_source_outlet_terrain_identity(mountain);
    author_fluid_25d_mountain_source_outlet_fields(mountain);

    require(mountain.terrain_height_m == immutable_terrain,
            "mountain source/outlet authoring should not modify imported terrain");
    require(mountain.terrain_provenance.has_value() &&
                mountain.terrain_provenance->identity == expected_identity &&
                mountain.terrain_provenance->source_id == "synthetic-mountain-source",
            "mountain source/outlet authoring should retain immutable terrain provenance");
    validate_fluid_25d_mountain_source_outlet_terrain_identity(mountain);
    require(mountain.source_cell ==
                    fluid_25d_scenario_index(mountain.width, mountain.height,
                                             kFluid25DMountainSourceOutletSourceX,
                                             kFluid25DMountainSourceOutletSourceZ) &&
                mountain.sink_cell == fluid_25d_scenario_index(mountain.width, mountain.height,
                                                               kFluid25DMountainSourceOutletSinkX,
                                                               kFluid25DMountainSourceOutletSinkZ),
            "mountain source/outlet authoring should retain reviewed endpoint centroids");

    std::size_t source_cells = 0U;
    std::size_t sink_cells = 0U;
    std::size_t visible_sink_disk_cells = 0U;
    double source_depth_rate_sum_m_per_s = 0.0;
    double sink_depth_rate_sum_m_per_s = 0.0;
    for (std::size_t index = 0U; index < cell_count; ++index) {
        const std::uint32_t x = static_cast<std::uint32_t>(index % mountain.width);
        const std::uint32_t z = static_cast<std::uint32_t>(index / mountain.width);
        const bool source = mountain.source_depth_rate_m_per_s[index] > 0.0F;
        const bool sink = mountain.sink_depth_rate_m_per_s[index] > 0.0F;
        const std::int32_t sink_dx = static_cast<std::int32_t>(x) -
                                     static_cast<std::int32_t>(kFluid25DMountainSourceOutletSinkX);
        const std::int32_t sink_dz = static_cast<std::int32_t>(z) -
                                     static_cast<std::int32_t>(kFluid25DMountainSourceOutletSinkZ);
        const bool visible_sink = (sink_dx * sink_dx) + (sink_dz * sink_dz) <= 25;
        require(!(source && sink),
                "mountain source/outlet endpoint regions should remain non-overlapping");
        if (sink) {
            require(visible_sink && fluid_25d_mountain_source_outlet_is_drain_cell(x, z) &&
                        mountain.initial_water_depth_m[index] >= 2.0F,
                    "mountain active outlet cells should be the reviewed visible drain reserve");
        }
        source_cells += source ? 1U : 0U;
        sink_cells += sink ? 1U : 0U;
        visible_sink_disk_cells += visible_sink ? 1U : 0U;
        source_depth_rate_sum_m_per_s += mountain.source_depth_rate_m_per_s[index];
        sink_depth_rate_sum_m_per_s += mountain.sink_depth_rate_m_per_s[index];
    }
    require(source_cells == 81U && visible_sink_disk_cells == 81U && sink_cells == 3U,
            "mountain source and visible outlet disks should cover 81 cells, with three reviewed "
            "drains");
    constexpr double kMountainCellAreaM2 =
        static_cast<double>(kFluid25DMountainSourceOutletCellSizeM) *
        static_cast<double>(kFluid25DMountainSourceOutletCellSizeM);
    require_close(source_depth_rate_sum_m_per_s * kMountainCellAreaM2,
                  kFluid25DMountainSourceOutletEndpointTotalVolumeRateM3PerS, 0.00001,
                  "mountain source region should normalize to its configured physical capacity");
    require_close(sink_depth_rate_sum_m_per_s * kMountainCellAreaM2,
                  kFluid25DMountainSourceOutletEndpointTotalVolumeRateM3PerS, 0.00001,
                  "mountain outlet region should normalize to its configured physical capacity");
    require(std::all_of(mountain.boundary_outflow_face_mask.begin(),
                        mountain.boundary_outflow_face_mask.end(),
                        [](std::uint32_t mask) { return mask == 0U; }),
            "mountain source/outlet demo should keep every outer boundary closed");

    constexpr std::array<std::array<std::uint32_t, 2U>, 15U> route_controls{
        std::array<std::uint32_t, 2U>{8U, 60U},
        {24U, 60U},
        {40U, 62U},
        {56U, 65U},
        {72U, 69U},
        {88U, 78U},
        {104U, 89U},
        {120U, 91U},
        {136U, 92U},
        {152U, 98U},
        {168U, 108U},
        {184U, 122U},
        {200U, 123U},
        {216U, 123U},
        {232U, 122U},
    };
    for (const auto& control : route_controls) {
        require(mountain.initial_water_depth_m[fluid_25d_scenario_index(
                    mountain.width, mountain.height, control[0], control[1])] >= 0.08F,
                "mountain source/outlet route controls should remain connected by nonzero water");
    }
    require_close(mountain.initial_water_depth_m[mountain.source_cell], 0.30, kDepthToleranceM,
                  "mountain source pool should retain its reviewed maximum initial depth");
    require_close(mountain.initial_water_depth_m[mountain.sink_cell], 0.50, kDepthToleranceM,
                  "mountain outlet pool should retain its reviewed maximum initial depth");

    Fluid25DScenarioData wrong_crop = mountain;
    wrong_crop.terrain_provenance->transformed_crop_sha256 = std::string(64U, '0');
    require_throws([&] { validate_fluid_25d_mountain_source_outlet_terrain_identity(wrong_crop); },
                   "mountain source/outlet demo should reject a synthetic wrong crop identity");
    Fluid25DScenarioData wrong_source = mountain;
    wrong_source.terrain_provenance->elevation_sha256 = std::string(64U, '0');
    require_throws(
        [&] { validate_fluid_25d_mountain_source_outlet_terrain_identity(wrong_source); },
        "mountain source/outlet demo should reject a synthetic wrong source identity");
}

void test_source_outlet_endpoint_shoulders() {
    using namespace cubey::projects::fluid::fluid_25d;
    constexpr std::uint32_t width = 128U;
    constexpr std::uint32_t height = 64U;
    const Fluid25DScenarioData scenario =
        make_fluid_25d_scenario(Fluid25DScenario::SourceOutletDemo, width, height, 1.0F);
    const Fluid25DSourceOutletGeometry geometry(width, height);
    const auto index = [](std::uint32_t x, std::uint32_t y) {
        return fluid_25d_scenario_index(width, height, x, y);
    };
    const auto smoothstep = [](float t) {
        const float bounded_t = std::clamp(t, 0.0F, 1.0F);
        return bounded_t * bounded_t * (3.0F - (2.0F * bounded_t));
    };
    const std::uint32_t upstream_length =
        std::min(kFluid25DSourceOutletUpstreamShoulderLengthCells, geometry.source_x);
    const std::uint32_t downstream_length = std::min(
        kFluid25DSourceOutletDownstreamShoulderLengthCells, (width - 1U) - geometry.sink_x);
    require(scenario.source_cell ==
                    index(geometry.source_x,
                          static_cast<std::uint32_t>(std::lround(
                              geometry.channel_center_y(static_cast<float>(geometry.source_x))))) &&
                scenario.sink_cell ==
                    index(geometry.sink_x,
                          static_cast<std::uint32_t>(std::lround(
                              geometry.channel_center_y(static_cast<float>(geometry.sink_x))))),
            "endpoint shoulders should retain the source and sink marker centroids");

    // Reconstruct each terrain column from the established interior route
    // profile plus the four-cell upstream and downstream smoothstep end caps.
    // This also pins every route cell, not merely its centerline.
    for (std::uint32_t x = 0U; x < width; ++x) {
        const float progress = geometry.route_progress(static_cast<float>(x));
        const float upstream_t = x < geometry.source_x ? static_cast<float>(geometry.source_x - x) /
                                                             static_cast<float>(upstream_length)
                                                       : 0.0F;
        const float downstream_t = x > geometry.sink_x ? static_cast<float>(x - geometry.sink_x) /
                                                             static_cast<float>(downstream_length)
                                                       : 0.0F;
        for (std::uint32_t y = 0U; y < height; ++y) {
            const float normalized_offset =
                (static_cast<float>(y) - geometry.channel_center_y(static_cast<float>(x))) /
                geometry.channel_half_width(static_cast<float>(x));
            const float expected_terrain =
                4.40F - (kFluid25DSourceOutletLongitudinalFallM * progress) +
                (kFluid25DSourceOutletUpstreamContainmentRiseM * smoothstep(upstream_t)) +
                (kFluid25DSourceOutletDownstreamContainmentRiseM * smoothstep(downstream_t)) +
                geometry.cross_sectional_relief_m(normalized_offset);
            require(scenario.terrain_height_m[index(x, y)] == expected_terrain,
                    "source-outlet terrain should preserve the full route and use bounded smooth "
                    "endpoint shoulders");
        }
    }

    const std::uint32_t source_center_y = static_cast<std::uint32_t>(
        std::lround(geometry.channel_center_y(static_cast<float>(geometry.source_x))));
    const std::uint32_t sink_center_y = static_cast<std::uint32_t>(
        std::lround(geometry.channel_center_y(static_cast<float>(geometry.sink_x))));
    const float source_center_bed_m =
        scenario.terrain_height_m[index(geometry.source_x, source_center_y)];
    const float sink_center_bed_m =
        scenario.terrain_height_m[index(geometry.sink_x, sink_center_y)];
    const float upstream_plateau_m =
        scenario.terrain_height_m[index(geometry.source_x - upstream_length, source_center_y)];
    const float downstream_plateau_m =
        scenario.terrain_height_m[index(geometry.sink_x + downstream_length, sink_center_y)];
    const auto source_left_crest = fluid_25d_source_outlet_bank_crest(
        geometry, geometry.source_x, scenario.terrain_height_m, true);
    const auto source_right_crest = fluid_25d_source_outlet_bank_crest(
        geometry, geometry.source_x, scenario.terrain_height_m, false);
    const auto sink_left_crest = fluid_25d_source_outlet_bank_crest(
        geometry, geometry.sink_x, scenario.terrain_height_m, true);
    const auto sink_right_crest = fluid_25d_source_outlet_bank_crest(
        geometry, geometry.sink_x, scenario.terrain_height_m, false);
    const float source_target_surface_m =
        std::min(source_left_crest.elevation_m, source_right_crest.elevation_m) -
        kFluid25DSourceOutletInitialFreeboardM;
    const float sink_target_surface_m =
        std::min(sink_left_crest.elevation_m, sink_right_crest.elevation_m) -
        kFluid25DSourceOutletInitialFreeboardM;
    require_close(upstream_plateau_m - source_center_bed_m,
                  kFluid25DSourceOutletUpstreamContainmentRiseM, 0.001,
                  "the upstream end cap should reach its three-metre containment plateau by cell "
                  "4");
    require_close(downstream_plateau_m - sink_center_bed_m,
                  kFluid25DSourceOutletDownstreamContainmentRiseM, 0.001,
                  "the outlet end cap should reach its 3.30-metre containment plateau by cell 4");
    require(upstream_plateau_m > source_target_surface_m &&
                downstream_plateau_m > sink_target_surface_m,
            "both end-cap plateaus should stand above their initial centerline target surfaces");

    // The shoulders are monotone in the direction away from the route and
    // remain flat after their clamped reach.
    for (std::uint32_t y = 0U; y < height; ++y) {
        for (std::uint32_t x = 0U; x < geometry.source_x; ++x) {
            require(
                scenario.terrain_height_m[index(x, y)] >=
                    scenario.terrain_height_m[index(x + 1U, y)],
                "upstream containment terrain should rise monotonically toward the closed edge");
        }
        for (std::uint32_t x = geometry.sink_x; x + 1U < width; ++x) {
            require(scenario.terrain_height_m[index(x, y)] <=
                        scenario.terrain_height_m[index(x + 1U, y)],
                    "downstream containment terrain should rise monotonically beyond the outlet");
        }
        require(scenario.terrain_height_m[index(0U, y)] ==
                        scenario.terrain_height_m[index(geometry.source_x - upstream_length, y)] &&
                    scenario.terrain_height_m[index(width - 1U, y)] ==
                        scenario.terrain_height_m[index(geometry.sink_x + downstream_length, y)],
                "endpoint containment shoulders should remain level after their short ramps");
    }

    // Rebuild the absolute-freeboard seed and unchanged endpoint rates. The
    // shoulder edit must not move initial water, alter forcing strips, or
    // change their normalized two-cubic-metre-per-second rates.
    const auto endpoint_weight = [](float x_offset, float y_offset, float radius_x) {
        const float normalized_distance_squared =
            (x_offset * x_offset) / (radius_x * radius_x) + (y_offset * y_offset) / (3.25F * 3.25F);
        return std::sqrt(std::max(0.0F, 1.0F - normalized_distance_squared));
    };
    const float endpoint_radius_x = geometry.endpoint_radius_x();
    std::vector<float> expected_source_rate(scenario.source_depth_rate_m_per_s.size(), 0.0F);
    std::vector<float> expected_sink_rate(scenario.sink_depth_rate_m_per_s.size(), 0.0F);
    const std::uint32_t strip_length =
        std::min(kFluid25DSourceOutletForcingStripLengthCells,
                 std::max(1U, (geometry.sink_x - geometry.source_x) / 3U));
    std::vector<std::size_t> source_region;
    std::vector<std::size_t> sink_region;
    for (std::uint32_t x = geometry.source_x; x <= geometry.sink_x; ++x) {
        const auto left_crest =
            fluid_25d_source_outlet_bank_crest(geometry, x, scenario.terrain_height_m, true);
        const auto right_crest =
            fluid_25d_source_outlet_bank_crest(geometry, x, scenario.terrain_height_m, false);
        const std::uint32_t first_y = std::min(left_crest.y_cell, right_crest.y_cell);
        const std::uint32_t last_y = std::max(left_crest.y_cell, right_crest.y_cell);
        const float center_y = geometry.channel_center_y(static_cast<float>(x));
        const float half_width = geometry.channel_half_width(static_cast<float>(x));
        for (std::uint32_t y = first_y; y <= last_y; ++y) {
            const std::size_t cell = index(x, y);
            const float normalized_offset = std::abs(static_cast<float>(y) - center_y) / half_width;
            if (normalized_offset > kFluid25DSourceOutletForcingHalfWidthNormalized ||
                scenario.initial_water_depth_m[cell] <= 0.10F) {
                continue;
            }
            if (x < geometry.source_x + strip_length) {
                source_region.push_back(cell);
            }
            if (x + strip_length > geometry.sink_x) {
                sink_region.push_back(cell);
            }
        }
    }
    require(!source_region.empty() && !sink_region.empty(),
            "source-outlet shoulder invariant should retain populated forcing regions");
    const float source_rate = kFluid25DSourceOutletEndpointTotalVolumeRateM3PerS /
                              (static_cast<float>(source_region.size()));
    const float sink_rate = kFluid25DSourceOutletEndpointTotalVolumeRateM3PerS /
                            (static_cast<float>(sink_region.size()));
    for (const std::size_t cell : source_region) {
        expected_source_rate[cell] = source_rate;
    }
    for (const std::size_t cell : sink_region) {
        expected_sink_rate[cell] = sink_rate;
    }
    const float expected_cell_rate_scale = 1.0F / (scenario.cell_size_m * scenario.cell_size_m);
    for (std::size_t cell = 0U; cell < scenario.initial_water_depth_m.size(); ++cell) {
        float expected_seed = 0.0F;
        const std::uint32_t x = static_cast<std::uint32_t>(cell % width);
        const std::uint32_t y = static_cast<std::uint32_t>(cell / width);
        if (x >= geometry.source_x && x <= geometry.sink_x) {
            const auto left_crest =
                fluid_25d_source_outlet_bank_crest(geometry, x, scenario.terrain_height_m, true);
            const auto right_crest =
                fluid_25d_source_outlet_bank_crest(geometry, x, scenario.terrain_height_m, false);
            const std::uint32_t first_y = std::min(left_crest.y_cell, right_crest.y_cell);
            const std::uint32_t last_y = std::max(left_crest.y_cell, right_crest.y_cell);
            if (y >= first_y && y <= last_y) {
                const float lower_crest_m =
                    std::min(left_crest.elevation_m, right_crest.elevation_m);
                const float target_surface_m =
                    lower_crest_m - kFluid25DSourceOutletInitialFreeboardM;
                const float source_weight =
                    endpoint_weight(static_cast<float>(x) - static_cast<float>(geometry.source_x),
                                    static_cast<float>(y) -
                                        static_cast<float>(std::lround(geometry.channel_center_y(
                                            static_cast<float>(geometry.source_x)))),
                                    endpoint_radius_x);
                const float sink_weight =
                    endpoint_weight(static_cast<float>(x) - static_cast<float>(geometry.sink_x),
                                    static_cast<float>(y) -
                                        static_cast<float>(std::lround(geometry.channel_center_y(
                                            static_cast<float>(geometry.sink_x)))),
                                    endpoint_radius_x);
                expected_seed =
                    std::max({std::max(0.0F, target_surface_m - scenario.terrain_height_m[cell]),
                              0.45F * source_weight, 0.70F * sink_weight});
                if (scenario.initial_water_depth_m[cell] > 0.0F) {
                    const float seeded_surface_m =
                        scenario.terrain_height_m[cell] + scenario.initial_water_depth_m[cell];
                    require(lower_crest_m - seeded_surface_m >= 0.25F,
                            "source-outlet endpoint seed should remain below the discrete lower "
                            "crest");
                }
            }
        }
        require(scenario.initial_water_depth_m[cell] == expected_seed,
                "endpoint shoulder terrain must leave every initial seed cell bitwise unchanged");
        require(scenario.source_depth_rate_m_per_s[cell] ==
                        expected_source_rate[cell] * expected_cell_rate_scale &&
                    scenario.sink_depth_rate_m_per_s[cell] ==
                        expected_sink_rate[cell] * expected_cell_rate_scale,
                "endpoint shoulder terrain must leave the normalized source/sink masks and rates "
                "unchanged");
    }

    // A very small supported grid clamps each shoulder to the available
    // outside cells instead of dividing by zero or changing the route contract.
    const Fluid25DScenarioData small =
        make_fluid_25d_scenario(Fluid25DScenario::SourceOutletDemo, 6U, 3U, 1.0F);
    const Fluid25DSourceOutletGeometry small_geometry(6U, 3U);
    const std::uint32_t small_center_y = static_cast<std::uint32_t>(
        std::lround(small_geometry.channel_center_y(static_cast<float>(small_geometry.source_x))));
    require_close(small.terrain_height_m[fluid_25d_scenario_index(6U, 3U, 0U, small_center_y)] -
                      small.terrain_height_m[fluid_25d_scenario_index(
                          6U, 3U, small_geometry.source_x, small_center_y)],
                  kFluid25DSourceOutletUpstreamContainmentRiseM, 0.001,
                  "upstream endpoint shoulder should clamp to the available span on small grids");
    const std::uint32_t small_sink_center_y = static_cast<std::uint32_t>(
        std::lround(small_geometry.channel_center_y(static_cast<float>(small_geometry.sink_x))));
    require_close(
        small.terrain_height_m[fluid_25d_scenario_index(6U, 3U, 5U, small_sink_center_y)] -
            small.terrain_height_m[fluid_25d_scenario_index(6U, 3U, small_geometry.sink_x,
                                                            small_sink_center_y)],
        kFluid25DSourceOutletDownstreamContainmentRiseM, 0.001,
        "downstream endpoint shoulder should clamp to the available span on small grids");

    // The non-opt-in RiverCatchment scenario remains the same analytic fixture.
    const Fluid25DScenarioData river =
        make_fluid_25d_scenario(Fluid25DScenario::RiverCatchment, 12U, 8U, 1.0F);
    const float river_center_y = 0.5F * static_cast<float>(river.height - 1U);
    for (std::uint32_t y = 0U; y < river.height; ++y) {
        for (std::uint32_t x = 0U; x < river.width; ++x) {
            const float y_offset = static_cast<float>(y) - river_center_y;
            const float expected_terrain =
                3.20F - (0.085F * static_cast<float>(x)) + (0.085F * y_offset * y_offset);
            require(
                river.terrain_height_m[fluid_25d_scenario_index(river.width, river.height, x, y)] ==
                    expected_terrain,
                "source-outlet shoulder changes must not modify RiverCatchment terrain");
        }
    }
}

} // namespace cubey::projects::fluid::fluid_25d::test_detail
