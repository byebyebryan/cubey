#include "terrain_export.h"
#include "terrain_patch.h"

#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct TerrainGenerateConfig {
    cubey::projects::terrain::TerrainPatchRequest request =
        cubey::projects::terrain::default_terrain_patch_request();
    std::filesystem::path output_dir{};
    cubey::projects::terrain::TerrainExportOptions export_options{};
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
        std::size_t parsed = 0U;
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
        std::size_t parsed = 0U;
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
        std::size_t parsed = 0U;
        const float result = std::stof(std::string(value), &parsed);
        if (parsed != value.size() || !std::isfinite(result)) {
            throw std::runtime_error("invalid");
        }
        return result;
    } catch (const std::exception&) {
        throw std::runtime_error("invalid float for " + std::string(option));
    }
}

[[nodiscard]] TerrainGenerateConfig parse_config(int argc, char** argv) {
    TerrainGenerateConfig config;
    for (int index = 1; index < argc; ++index) {
        const std::string_view arg(argv[index]);
        if (arg == "--grid-size") {
            const std::uint32_t size = parse_u32(require_value(index, argc, argv, arg), arg);
            config.request.domain.interior_grid.width = size;
            config.request.domain.interior_grid.height = size;
        } else if (arg == "--grid-width") {
            config.request.domain.interior_grid.width =
                parse_u32(require_value(index, argc, argv, arg), arg);
        } else if (arg == "--grid-height") {
            config.request.domain.interior_grid.height =
                parse_u32(require_value(index, argc, argv, arg), arg);
        } else if (arg == "--terrain-cell-size") {
            config.request.domain.interior_grid.cell_size =
                parse_float(require_value(index, argc, argv, arg), arg);
        } else if (arg == "--terrain-origin-x") {
            config.request.domain.interior_grid.origin_x =
                parse_float(require_value(index, argc, argv, arg), arg);
        } else if (arg == "--terrain-origin-z") {
            config.request.domain.interior_grid.origin_y =
                parse_float(require_value(index, argc, argv, arg), arg);
        } else if (arg == "--terrain-seed") {
            config.request.domain.seed = parse_u64(require_value(index, argc, argv, arg), arg);
        } else if (arg == "--terrain-recipe") {
            config.request.recipe_id = std::string(require_value(index, argc, argv, arg));
        } else if (arg == "--terrain-output-dir") {
            config.output_dir =
                std::filesystem::path(std::string(require_value(index, argc, argv, arg)));
        } else if (arg == "--terrain-export-raw") {
            config.export_options.write_raw_float32 = true;
        } else if (arg == "--headless") {
            continue;
        } else {
            throw std::runtime_error("unknown argument: " + std::string(arg));
        }
    }
    if (config.output_dir.empty()) {
        throw std::runtime_error("terrain_generate requires --terrain-output-dir");
    }
    config.request.generator_revision =
        cubey::projects::terrain::terrain_generator_revision_for_recipe(config.request.recipe_id);
    return config;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const TerrainGenerateConfig config = parse_config(argc, argv);
        const cubey::projects::terrain::TerrainPatchProduct product =
            cubey::projects::terrain::generate_terrain_patch(config.request);
        cubey::projects::terrain::write_terrain_field_exports(product, config.output_dir,
                                                              config.export_options);
        std::cout << "terrain_generate: recipe=" << product.request.recipe_id
                  << " grid=" << product.fields.desc().width << 'x' << product.fields.desc().height
                  << " fields=" << product.fields.field_count()
                  << " hash=" << product.summary.content_hash
                  << " wrote=" << config.output_dir.string() << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "terrain_generate: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
