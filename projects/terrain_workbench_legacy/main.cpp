#include "terrain_generator.h"
#include "terrain_debug_export.h"
#include "terrain_phase_profile.h"

#include <cubey/engine/capture_queue.h>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace {

inline constexpr std::size_t kTerrainDebugEncodeWorkerCount = 2U;
inline constexpr std::size_t kTerrainDebugEncodeBacklog = 4U;

struct TerrainCliConfig {
    cubey::projects::terrain::TerrainRegionConfig terrain{};
    cubey::projects::terrain::TerrainDebugView debug_view =
        cubey::projects::terrain::TerrainDebugView::Final;
    std::filesystem::path output_path{};
    std::filesystem::path output_dir{};
    std::filesystem::path profile_output_prefix{};
    bool export_all_views = false;
    bool headless = false;
};

[[nodiscard]] std::string_view require_value(int& index, int argc, char** argv,
                                             std::string_view option) {
    if (index + 1 >= argc) {
        throw std::runtime_error("missing value for " + std::string(option));
    }
    ++index;
    return argv[index];
}

[[nodiscard]] std::uint32_t parse_u32(std::string_view value, std::string_view option) {
    try {
        std::size_t parsed = 0;
        const unsigned long result = std::stoul(std::string(value), &parsed, 10);
        if (parsed != value.size()) {
            throw std::runtime_error("invalid");
        }
        return static_cast<std::uint32_t>(result);
    } catch (const std::exception&) {
        throw std::runtime_error("invalid integer for " + std::string(option));
    }
}

[[nodiscard]] std::uint64_t parse_u64(std::string_view value, std::string_view option) {
    try {
        std::size_t parsed = 0;
        const unsigned long long result = std::stoull(std::string(value), &parsed, 10);
        if (parsed != value.size()) {
            throw std::runtime_error("invalid");
        }
        return static_cast<std::uint64_t>(result);
    } catch (const std::exception&) {
        throw std::runtime_error("invalid integer for " + std::string(option));
    }
}

[[nodiscard]] float parse_float(std::string_view value, std::string_view option) {
    try {
        std::size_t parsed = 0;
        const float result = std::stof(std::string(value), &parsed);
        if (parsed != value.size()) {
            throw std::runtime_error("invalid");
        }
        return result;
    } catch (const std::exception&) {
        throw std::runtime_error("invalid float for " + std::string(option));
    }
}

[[nodiscard]] TerrainCliConfig parse_config(int argc, char** argv) {
    TerrainCliConfig config{};
    for (int index = 1; index < argc; ++index) {
        const std::string_view arg(argv[index]);
        if (arg == "--seed") {
            config.terrain.seed = parse_u64(require_value(index, argc, argv, arg), arg);
        } else if (arg == "--grid-size") {
            const std::uint32_t size = parse_u32(require_value(index, argc, argv, arg), arg);
            config.terrain.grid_width = size;
            config.terrain.grid_height = size;
        } else if (arg == "--grid-width") {
            config.terrain.grid_width = parse_u32(require_value(index, argc, argv, arg), arg);
        } else if (arg == "--grid-height") {
            config.terrain.grid_height = parse_u32(require_value(index, argc, argv, arg), arg);
        } else if (arg == "--width") {
            config.terrain.grid_width = parse_u32(require_value(index, argc, argv, arg), arg);
        } else if (arg == "--height") {
            config.terrain.grid_height = parse_u32(require_value(index, argc, argv, arg), arg);
        } else if (arg == "--cell-size") {
            config.terrain.cell_size_m = parse_float(require_value(index, argc, argv, arg), arg);
        } else if (arg == "--recipe" || arg == "--terrain-recipe") {
            config.terrain.recipe_id = std::string(require_value(index, argc, argv, arg));
        } else if (arg == "--headless") {
            config.headless = true;
        } else if (arg == "--terrain-debug-view") {
            const std::string_view value = require_value(index, argc, argv, arg);
            if (value == "all") {
                config.export_all_views = true;
            } else {
                config.debug_view = cubey::projects::terrain::terrain_debug_view_from_name(value);
            }
        } else if (arg == "--output") {
            config.output_path = std::filesystem::path{
                std::string(require_value(index, argc, argv, arg))};
        } else if (arg == "--terrain-output-dir") {
            config.output_dir = std::filesystem::path{
                std::string(require_value(index, argc, argv, arg))};
        } else if (arg == "--profile-output") {
            config.profile_output_prefix =
                cubey::projects::terrain::terrain_phase_profile_output_prefix(
                    require_value(index, argc, argv, arg));
        } else {
            throw std::runtime_error("unknown argument: " + std::string(arg));
        }
    }
    return config;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const TerrainCliConfig cli_config = parse_config(argc, argv);
        if (!cli_config.output_path.empty() && !cli_config.output_dir.empty()) {
            throw std::runtime_error("terrain export accepts either --output or --terrain-output-dir");
        }
        if (cli_config.export_all_views && cli_config.output_dir.empty()) {
            throw std::runtime_error("terrain debug view all requires --terrain-output-dir");
        }
        cubey::projects::terrain::TerrainPhaseProfile phase_profile(
            cli_config.profile_output_prefix);
        const auto total_start = cubey::projects::terrain::TerrainPhaseProfile::now();

        cubey::projects::terrain::TerrainRegionProduct product;
        {
            cubey::projects::terrain::TerrainPhaseScope phase(phase_profile, "generate_region");
            product = cubey::projects::terrain::generate_terrain_region(cli_config.terrain);
        }

        cubey::projects::terrain::TerrainPhaseProfileMetadata metadata =
            cubey::projects::terrain::terrain_phase_profile_metadata("terrain",
                                                                     product.config);
        metadata.field_count = static_cast<std::uint32_t>(product.fields.field_count());
        if (cli_config.headless || !cli_config.output_path.empty() ||
            !cli_config.output_dir.empty()) {
            if (cli_config.output_path.empty() && cli_config.output_dir.empty()) {
                throw std::runtime_error(
                    "terrain headless export requires --output or --terrain-output-dir");
            }
            cubey::jobs::JobSystem encode_jobs(kTerrainDebugEncodeWorkerCount);
            cubey::CaptureQueue captures(encode_jobs);
            if (cli_config.export_all_views) {
                metadata.output_count = static_cast<std::uint32_t>(
                    cubey::projects::terrain::terrain_debug_review_views().size());
                {
                    cubey::projects::terrain::TerrainPhaseScope phase(phase_profile,
                                                                      "write_debug_views");
                    cubey::CaptureBacklog backlog(kTerrainDebugEncodeBacklog);
                    for (const cubey::projects::terrain::TerrainDebugView view :
                         cubey::projects::terrain::terrain_debug_review_views()) {
                        const std::filesystem::path output_path =
                            cli_config.output_dir /
                            (std::string(cubey::projects::terrain::terrain_debug_view_name(view)) +
                             ".png");
                        backlog.enqueue(
                            cubey::projects::terrain::enqueue_terrain_debug_png(
                                captures, product, view, output_path));
                    }
                    backlog.finish_all();
                }
                {
                    cubey::projects::terrain::TerrainPhaseScope phase(phase_profile,
                                                                      "write_manifest");
                    cubey::projects::terrain::write_terrain_debug_manifest(
                        product, cli_config.output_dir);
                }
            } else if (!cli_config.output_dir.empty()) {
                const std::filesystem::path output_path =
                    cli_config.output_dir /
                    (std::string(cubey::projects::terrain::terrain_debug_view_name(
                         cli_config.debug_view)) +
                     ".png");
                metadata.output_count = 1U;
                cubey::projects::terrain::TerrainPhaseScope phase(phase_profile,
                                                                  "write_debug_view");
                cubey::CaptureTicket ticket =
                    cubey::projects::terrain::enqueue_terrain_debug_png(
                        captures, product, cli_config.debug_view, output_path);
                ticket.finish();
            } else {
                metadata.output_count = 1U;
                cubey::projects::terrain::TerrainPhaseScope phase(phase_profile,
                                                                  "write_debug_view");
                cubey::CaptureTicket ticket =
                    cubey::projects::terrain::enqueue_terrain_debug_png(
                        captures, product, cli_config.debug_view, cli_config.output_path);
                ticket.finish();
            }
        }
        std::cout << "terrain: generated product '" << product.config.recipe_id << "' "
                  << product.fields.desc().width << "x" << product.fields.desc().height
                  << " fields=" << product.fields.field_count();
        if (!cli_config.output_path.empty()) {
            std::cout << " wrote " << cli_config.output_path.string() << " view="
                      << cubey::projects::terrain::terrain_debug_view_name(cli_config.debug_view);
        } else if (!cli_config.output_dir.empty()) {
            std::cout << " wrote " << cli_config.output_dir.string();
            if (cli_config.export_all_views) {
                std::cout << " views=all";
            } else {
                std::cout << " view="
                          << cubey::projects::terrain::terrain_debug_view_name(
                                 cli_config.debug_view);
            }
        }
        std::cout << '\n';
        phase_profile.set_metadata(std::move(metadata));
        phase_profile.record_elapsed("total", total_start);
        phase_profile.write();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "terrain: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
