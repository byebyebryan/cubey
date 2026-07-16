#include "terrain_directional_backdrop_study.h"
#include "terrain_source_study.h"

#include "terrain_directional_relief.h"
#include "terrain_radial_relief.h"

#include <cubey/core/jobs.h>
#include <cubey/engine/capture_queue.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr float kDefaultExtentM = 16'384.0F;
constexpr float kHeightMaximumM = 6'000.0F;
constexpr float kSlopeMaximum = 2.0F;

struct Options {
    std::filesystem::path output_dir{};
    cubey::projects::terrain::TerrainSourceStudyRecipe recipe =
        cubey::projects::terrain::TerrainSourceStudyRecipe::MountainsHierarchyV2;
    std::uint64_t seed = 9012U;
    std::uint32_t grid_size = 512U;
    bool expanded = false;
    bool radial = false;
    cubey::projects::terrain::TerrainRadialFidelity radial_fidelity =
        cubey::projects::terrain::TerrainRadialFidelity::Control;
    float focus_height_m = 500.0F;
};

[[nodiscard]] Options parse_options(int argc, char** argv) {
    Options result;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option = argv[index];
        if (option == "--output-dir" && index + 1 < argc) {
            result.output_dir = argv[++index];
        } else if (option == "--recipe" && index + 1 < argc) {
            result.recipe =
                cubey::projects::terrain::terrain_source_study_recipe_from_name(argv[++index]);
        } else if (option == "--seed" && index + 1 < argc) {
            result.seed = std::stoull(argv[++index]);
        } else if (option == "--grid-size" && index + 1 < argc) {
            result.grid_size = static_cast<std::uint32_t>(std::stoul(argv[++index]));
        } else if (option == "--expanded") {
            result.expanded = true;
        } else if (option == "--radial") {
            result.expanded = true;
            result.radial = true;
        } else if (option == "--radial-fidelity" && index + 1 < argc) {
            result.expanded = true;
            result.radial = true;
            result.radial_fidelity =
                cubey::projects::terrain::terrain_radial_fidelity_from_name(argv[++index]);
        } else if (option == "--focus-height" && index + 1 < argc) {
            result.focus_height_m = std::stof(argv[++index]);
        } else {
            throw std::runtime_error("usage: terrain_directional_backdrop_report --output-dir path "
                                     "[--recipe id] [--seed integer] [--grid-size 128..1024] "
                                     "[--expanded] [--radial] "
                                     "[--radial-fidelity control|source|material|combined] "
                                     "[--focus-height 100..1000]");
        }
    }
    if (result.output_dir.empty() || result.grid_size < 128U || result.grid_size > 1'024U ||
        !std::isfinite(result.focus_height_m) || result.focus_height_m < 100.0F ||
        result.focus_height_m > 1'000.0F) {
        throw std::runtime_error("invalid terrain directional backdrop report options");
    }
    return result;
}

[[nodiscard]] std::array<std::uint8_t, 4> scalar_color(float value, float maximum) {
    const float unit = std::clamp(value / maximum, 0.0F, 1.0F);
    const auto byte = static_cast<std::uint8_t>(std::lround(unit * 255.0F));
    return {byte, byte, byte, 255U};
}

void write_pixel(std::vector<std::uint8_t>& rgba, std::size_t index,
                 std::array<std::uint8_t, 4> color) {
    for (std::size_t component = 0U; component < color.size(); ++component) {
        rgba[index * 4U + component] = color[component];
    }
}

[[nodiscard]] std::vector<float> slopes(const std::vector<float>& heights, std::uint32_t size,
                                        float spacing_m) {
    std::vector<float> result(heights.size());
    for (std::uint32_t y = 0U; y < size; ++y) {
        for (std::uint32_t x = 0U; x < size; ++x) {
            const std::uint32_t x0 = x == 0U ? x : x - 1U;
            const std::uint32_t x1 = std::min(x + 1U, size - 1U);
            const std::uint32_t y0 = y == 0U ? y : y - 1U;
            const std::uint32_t y1 = std::min(y + 1U, size - 1U);
            const float dx = heights[static_cast<std::size_t>(y) * size + x1] -
                             heights[static_cast<std::size_t>(y) * size + x0];
            const float dy = heights[static_cast<std::size_t>(y1) * size + x] -
                             heights[static_cast<std::size_t>(y0) * size + x];
            const float gradient_x = dx / (spacing_m * static_cast<float>(x1 - x0));
            const float gradient_y = dy / (spacing_m * static_cast<float>(y1 - y0));
            result[static_cast<std::size_t>(y) * size + x] =
                std::sqrt(gradient_x * gradient_x + gradient_y * gradient_y);
        }
    }
    return result;
}

[[nodiscard]] nlohmann::json
placement_json(const cubey::projects::terrain::TerrainDirectionalPlacementPlan& placement) {
    nlohmann::json sectors = nlohmann::json::array();
    for (const auto& sector : placement.sectors) {
        sectors.push_back({
            {"yaw_radians", sector.yaw_radians},
            {"near_height_m", sector.near_height_m},
            {"middle_height_m", sector.middle_height_m},
            {"far_height_m", sector.far_height_m},
            {"remote_height_m", sector.remote_height_m},
            {"prominence_m", sector.prominence_m},
            {"mountain", sector.mountain},
            {"open", sector.open},
            {"gradual_rise", sector.gradual_rise},
        });
    }
    return {
        {"source_focus_xz_m", {placement.source_focus_xz.x, placement.source_focus_xz.y}},
        {"mountain_yaw_radians", placement.mountain_yaw_radians},
        {"center_height_m", placement.center_height_m},
        {"local_relief_m", placement.local_relief_m},
        {"local_p95_slope", placement.local_p95_slope},
        {"mean_mountain_prominence_m", placement.mean_mountain_prominence_m},
        {"mountain_sector_count", placement.mountain_sector_count},
        {"open_sector_count", placement.open_sector_count},
        {"gradual_rise_sector_count", placement.gradual_rise_sector_count},
        {"largest_mountain_arc_sectors", placement.largest_mountain_arc_sectors},
        {"largest_open_arc_sectors", placement.largest_open_arc_sectors},
        {"contract_satisfied", placement.contract_satisfied},
        {"score", placement.score},
        {"sectors", std::move(sectors)},
    };
}

[[nodiscard]] nlohmann::json
stage_json(const cubey::projects::terrain::TerrainBackdropStagePlan& stage) {
    return {
        {"target_height_m", stage.target_height_m},
        {"source_center_height_m", stage.source_center_height_m},
        {"focus_height_m", stage.target_height_m - stage.source_center_height_m},
        {"terrain_vertical_offset_m", stage.terrain_vertical_offset_m},
        {"local_relief_m", stage.local_relief_m},
        {"local_p95_slope", stage.local_p95_slope},
        {"minimum_camera_clearance_m", stage.minimum_camera_clearance_m},
        {"relief_sector_count", stage.relief_sector_count},
        {"open_sector_count", stage.lower_frame_clear_sector_count},
        {"orbit_radius_m",
         {stage.orbit_min_radius_m, stage.orbit_default_radius_m, stage.orbit_max_radius_m}},
        {"orbit_elevation_radians",
         {stage.orbit_min_elevation_radians, stage.orbit_default_elevation_radians,
          stage.orbit_max_elevation_radians}},
        {"contract_satisfied", stage.contract_satisfied},
    };
}

} // namespace

int main(int argc, char** argv) {
    using namespace cubey::projects::terrain;
    try {
        const Options options = parse_options(argc, argv);
        std::filesystem::create_directories(options.output_dir);
        const TerrainSourceStudySource base_source(options.recipe, options.seed);
        const TerrainDirectionalPlacementPlan placement = plan_terrain_directional_placement(
            base_source, directional_backdrop_placement_request());
        const TerrainDirectionalReliefParameters relief_parameters =
            options.expanded ? expanded_directional_backdrop_relief_parameters(placement)
                             : TerrainDirectionalReliefParameters{
                                   .focus_xz = placement.source_focus_xz,
                                   .mountain_yaw_radians = placement.mountain_yaw_radians,
                               };
        const TerrainRadialReliefParameters radial_parameters =
            radial_fidelity_backdrop_relief_parameters(placement, options.radial_fidelity);
        const TerrainDirectionalReliefSource directional_source(base_source, relief_parameters);
        const TerrainRadialReliefSource radial_source(base_source, radial_parameters);
        const TerrainHeightSource& shaped_source =
            options.radial ? static_cast<const TerrainHeightSource&>(radial_source)
                           : static_cast<const TerrainHeightSource&>(directional_source);
        const TerrainBackdropStagePlan placement_stage =
            make_directional_backdrop_stage_plan(base_source, placement);
        TerrainDirectionalBackdropStageParameters shaped_stage_parameters;
        if (options.expanded) {
            shaped_stage_parameters = {
                .focus_height_m = options.focus_height_m,
                .orbit_min_radius_m = 100.0F,
                .orbit_default_radius_m = 400.0F,
                .orbit_max_radius_m = 1'000.0F,
            };
        }
        const TerrainBackdropStagePlan shaped_stage = make_directional_backdrop_stage_plan(
            shaped_source, placement, 1.0F, shaped_stage_parameters);

        const float extent_m =
            options.expanded ? expanded_backdrop_outer_radius_m() : kDefaultExtentM;
        const float spacing_m = 2.0F * extent_m / static_cast<float>(options.grid_size - 1U);
        const std::size_t sample_count =
            static_cast<std::size_t>(options.grid_size) * options.grid_size;
        std::vector<float> base_heights(sample_count);
        std::vector<float> shaped_heights(sample_count);
        std::vector<float> floor_heights(sample_count);
        std::vector<float> structure_heights(sample_count);
        std::vector<float> detail_heights(sample_count);
        std::vector<float> broad_gates(sample_count);
        std::vector<float> detail_gates(sample_count);
        cubey::jobs::JobSystem jobs(std::max(1U, std::thread::hardware_concurrency()));
        std::vector<cubey::jobs::JobHandle<void>> handles;
        constexpr std::uint32_t kRowsPerJob = 16U;
        for (std::uint32_t begin = 0U; begin < options.grid_size; begin += kRowsPerJob) {
            const std::uint32_t end = std::min(begin + kRowsPerJob, options.grid_size);
            handles.push_back(jobs.submit([&, begin, end] {
                for (std::uint32_t y = begin; y < end; ++y) {
                    for (std::uint32_t x = 0U; x < options.grid_size; ++x) {
                        const std::size_t index =
                            static_cast<std::size_t>(y) * options.grid_size + x;
                        const TerrainQuery query{
                            .world_xz =
                                placement.source_focus_xz +
                                cubey::math::Vec2{-extent_m + static_cast<float>(x) * spacing_m,
                                                  -extent_m + static_cast<float>(y) * spacing_m},
                            .footprint_m = spacing_m,
                        };
                        base_heights[index] = base_source.sample_height(query);
                        if (options.radial) {
                            const TerrainRadialReliefSample shaped =
                                radial_source.sample_composition(query);
                            shaped_heights[index] = shaped.height_m;
                            floor_heights[index] = shaped.floor_height_m;
                            structure_heights[index] = shaped.structure_height_m;
                            detail_heights[index] = shaped.detail_height_m;
                            broad_gates[index] = shaped.broad_gate;
                            detail_gates[index] = shaped.detail_gate;
                        } else {
                            const TerrainDirectionalReliefSample shaped =
                                directional_source.sample_composition(query);
                            shaped_heights[index] = shaped.height_m;
                            floor_heights[index] = shaped.floor_height_m;
                            broad_gates[index] = shaped.broad_gate;
                            detail_gates[index] = shaped.detail_gate;
                        }
                    }
                }
            }));
        }
        for (auto& handle : handles) {
            handle.get();
        }
        jobs.shutdown();
        const std::vector<float> base_slopes = slopes(base_heights, options.grid_size, spacing_m);
        const std::vector<float> shaped_slopes =
            slopes(shaped_heights, options.grid_size, spacing_m);

        std::vector<std::uint8_t> base_rgba(sample_count * 4U);
        std::vector<std::uint8_t> shaped_rgba(sample_count * 4U);
        std::vector<std::uint8_t> floor_rgba(sample_count * 4U);
        std::vector<std::uint8_t> structure_rgba(sample_count * 4U);
        std::vector<std::uint8_t> filtered_detail_rgba(sample_count * 4U);
        std::vector<std::uint8_t> base_slope_rgba(sample_count * 4U);
        std::vector<std::uint8_t> shaped_slope_rgba(sample_count * 4U);
        std::vector<std::uint8_t> broad_rgba(sample_count * 4U);
        std::vector<std::uint8_t> detail_rgba(sample_count * 4U);
        std::vector<std::uint8_t> placement_rgba(sample_count * 4U);
        for (std::uint32_t y = 0U; y < options.grid_size; ++y) {
            for (std::uint32_t x = 0U; x < options.grid_size; ++x) {
                const std::size_t index = static_cast<std::size_t>(y) * options.grid_size + x;
                write_pixel(base_rgba, index, scalar_color(base_heights[index], kHeightMaximumM));
                write_pixel(shaped_rgba, index,
                            scalar_color(shaped_heights[index], kHeightMaximumM));
                write_pixel(floor_rgba, index, scalar_color(floor_heights[index], kHeightMaximumM));
                if (options.radial) {
                    write_pixel(structure_rgba, index,
                                scalar_color(structure_heights[index], kHeightMaximumM));
                    write_pixel(filtered_detail_rgba, index,
                                scalar_color(detail_heights[index], kHeightMaximumM));
                }
                write_pixel(base_slope_rgba, index,
                            scalar_color(base_slopes[index], kSlopeMaximum));
                write_pixel(shaped_slope_rgba, index,
                            scalar_color(shaped_slopes[index], kSlopeMaximum));
                write_pixel(broad_rgba, index, scalar_color(broad_gates[index], 1.0F));
                write_pixel(detail_rgba, index, scalar_color(detail_gates[index], 1.0F));

                const float local_x = -extent_m + static_cast<float>(x) * spacing_m;
                const float local_z = -extent_m + static_cast<float>(y) * spacing_m;
                const float radius = std::sqrt(local_x * local_x + local_z * local_z);
                float yaw = std::atan2(local_x, -local_z);
                if (yaw < 0.0F) {
                    yaw += 2.0F * std::numbers::pi_v<float>;
                }
                const std::uint32_t sector =
                    std::min(static_cast<std::uint32_t>(
                                 yaw / (2.0F * std::numbers::pi_v<float>)*static_cast<float>(
                                           placement.sector_count)),
                             placement.sector_count - 1U);
                std::array<std::uint8_t, 4> color =
                    scalar_color(base_heights[index], kHeightMaximumM);
                const bool on_ring = std::abs(radius - 2'000.0F) < spacing_m * 2.0F ||
                                     std::abs(radius - 6'000.0F) < spacing_m * 2.0F ||
                                     std::abs(radius - 9'000.0F) < spacing_m * 2.0F ||
                                     std::abs(radius - 12'000.0F) < spacing_m * 2.0F;
                if (on_ring && placement.sectors[sector].mountain) {
                    color = {224U, 82U, 64U, 255U};
                } else if (on_ring && placement.sectors[sector].open) {
                    color = {64U, 154U, 224U, 255U};
                }
                const float direction_delta = std::abs(std::remainder(
                    yaw - placement.mountain_yaw_radians, 2.0F * std::numbers::pi_v<float>));
                if (direction_delta < 0.012F && radius > 500.0F) {
                    color = {245U, 210U, 70U, 255U};
                }
                write_pixel(placement_rgba, index, color);
            }
        }

        cubey::jobs::JobSystem encode_jobs(2U);
        cubey::CaptureQueue captures(encode_jobs);
        cubey::CaptureBacklog backlog(4U);
        const auto enqueue = [&](std::string_view name, std::vector<std::uint8_t> rgba) {
            backlog.enqueue(captures.enqueue_png({
                .output_path = options.output_dir / name,
                .width = options.grid_size,
                .height = options.grid_size,
                .rgba8 = std::move(rgba),
            }));
        };
        if (options.radial) {
            enqueue("full-source-height.png", base_rgba);
        }
        enqueue("base-height.png", std::move(base_rgba));
        enqueue("shaped-height.png", std::move(shaped_rgba));
        enqueue("floor-height.png", std::move(floor_rgba));
        if (options.radial) {
            enqueue("structure-height.png", std::move(structure_rgba));
            enqueue("filtered-detail-height.png", std::move(filtered_detail_rgba));
        }
        enqueue("base-slope.png", std::move(base_slope_rgba));
        enqueue("shaped-slope.png", std::move(shaped_slope_rgba));
        enqueue("broad-gate.png", std::move(broad_rgba));
        enqueue("detail-gate.png", std::move(detail_rgba));
        enqueue("placement-map.png", std::move(placement_rgba));
        backlog.finish_all();
        encode_jobs.shutdown();

        nlohmann::json shaping;
        if (options.radial) {
            shaping = {
                {"transition", "radial"},
                {"floor_footprint_m", radial_parameters.floor_footprint_m},
                {"floor_relief_fraction", radial_parameters.floor_relief_fraction},
                {"structure_footprint_m", radial_parameters.structure_footprint_m},
                {"detail_footprint_m", radial_parameters.detail_footprint_m},
                {"broad_range_m",
                 {radial_parameters.broad_start_m, radial_parameters.broad_full_m}},
                {"detail_range_m",
                 {radial_parameters.detail_start_m, radial_parameters.detail_full_m}},
            };
        } else {
            shaping = {
                {"transition", "directional"},
                {"floor_footprint_m", relief_parameters.floor_footprint_m},
                {"floor_relief_fraction", relief_parameters.floor_relief_fraction},
                {"structure_footprint_m", relief_parameters.structure_footprint_m},
                {"broad_range_m",
                 {relief_parameters.broad_start_m, relief_parameters.broad_full_m}},
                {"detail_range_m",
                 {relief_parameters.detail_start_m, relief_parameters.detail_full_m}},
                {"warp_period_m", relief_parameters.warp_period_m},
                {"warp_amplitude_m", relief_parameters.warp_amplitude_m},
                {"warp_octaves", relief_parameters.warp_octaves},
            };
        }
        const nlohmann::json report{
            {"schema", options.radial     ? "cubey.terrain.radial-backdrop-study.v1"
                       : options.expanded ? "cubey.terrain.directional-backdrop-study.v2"
                                          : "cubey.terrain.directional-backdrop-study.v1"},
            {"mode", options.radial     ? "expanded-radial"
                     : options.expanded ? "expanded"
                                        : "baseline"},
            {"recipe", terrain_source_study_recipe_name(options.recipe)},
            {"seed", options.seed},
            {"radial_fidelity", terrain_radial_fidelity_name(options.radial_fidelity)},
            {"domain",
             {{"extent_m", extent_m}, {"grid_size", options.grid_size}, {"spacing_m", spacing_m}}},
            {"placement", placement_json(placement)},
            {"placement_stage", stage_json(placement_stage)},
            {"shaped_stage", stage_json(shaped_stage)},
            {"shaping", std::move(shaping)},
        };
        std::ofstream output(options.output_dir / "report.json");
        if (!output) {
            throw std::runtime_error("failed to open directional backdrop report output");
        }
        output << report.dump(2) << '\n';
        std::cout << "terrain directional backdrop report: wrote " << options.output_dir << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain_directional_backdrop_report: " << error.what() << '\n';
        return 1;
    }
}
