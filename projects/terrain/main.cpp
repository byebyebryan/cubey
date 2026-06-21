#include "terrain_generator.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

namespace {

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

[[nodiscard]] cubey::projects::terrain::TerrainRegionConfig parse_config(int argc, char** argv) {
    cubey::projects::terrain::TerrainRegionConfig config{};
    for (int index = 1; index < argc; ++index) {
        const std::string_view arg(argv[index]);
        if (arg == "--seed") {
            config.seed = parse_u64(require_value(index, argc, argv, arg), arg);
        } else if (arg == "--grid-size") {
            const std::uint32_t size = parse_u32(require_value(index, argc, argv, arg), arg);
            config.grid_width = size;
            config.grid_height = size;
        } else if (arg == "--grid-width") {
            config.grid_width = parse_u32(require_value(index, argc, argv, arg), arg);
        } else if (arg == "--grid-height") {
            config.grid_height = parse_u32(require_value(index, argc, argv, arg), arg);
        } else if (arg == "--cell-size") {
            config.cell_size_m = parse_float(require_value(index, argc, argv, arg), arg);
        } else {
            throw std::runtime_error("unknown argument: " + std::string(arg));
        }
    }
    return config;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const cubey::projects::terrain::TerrainRegionProduct product =
            cubey::projects::terrain::generate_terrain_region(parse_config(argc, argv));
        std::cout << "terrain: generated product '" << product.config.recipe_id << "' "
                  << product.fields.desc().width << "x" << product.fields.desc().height
                  << " fields=" << product.fields.field_count() << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "terrain: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
