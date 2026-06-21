#include "terrain_generator.h"
#include "terrain_debug_export.h"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

struct TerrainCliConfig {
    cubey::projects::terrain::TerrainRegionConfig terrain{};
    cubey::projects::terrain::TerrainDebugView debug_view =
        cubey::projects::terrain::TerrainDebugView::Final;
    std::filesystem::path output_path{};
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
        } else if (arg == "--cell-size") {
            config.terrain.cell_size_m = parse_float(require_value(index, argc, argv, arg), arg);
        } else if (arg == "--headless") {
            config.headless = true;
        } else if (arg == "--terrain-debug-view") {
            config.debug_view = cubey::projects::terrain::terrain_debug_view_from_name(
                require_value(index, argc, argv, arg));
        } else if (arg == "--output") {
            config.output_path = std::filesystem::path{
                std::string(require_value(index, argc, argv, arg))};
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
        const cubey::projects::terrain::TerrainRegionProduct product =
            cubey::projects::terrain::generate_terrain_region(cli_config.terrain);
        if (cli_config.headless || !cli_config.output_path.empty()) {
            if (cli_config.output_path.empty()) {
                throw std::runtime_error("terrain headless export requires --output");
            }
            cubey::projects::terrain::write_terrain_debug_png(product, cli_config.debug_view,
                                                              cli_config.output_path);
        }
        std::cout << "terrain: generated product '" << product.config.recipe_id << "' "
                  << product.fields.desc().width << "x" << product.fields.desc().height
                  << " fields=" << product.fields.field_count();
        if (!cli_config.output_path.empty()) {
            std::cout << " wrote " << cli_config.output_path.string() << " view="
                      << cubey::projects::terrain::terrain_debug_view_name(cli_config.debug_view);
        }
        std::cout << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "terrain: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
