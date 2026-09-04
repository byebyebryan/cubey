#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct LoadedImage {
    int width = 0;
    int height = 0;
    unsigned char* pixels = nullptr;
};

struct ImageStats {
    double mean_luma = 0.0;
    double min_luma = std::numeric_limits<double>::max();
    double max_luma = 0.0;
};

struct Rgb {
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
};

struct RegionStats {
    Rgb color{};
    double luma = 0.0;
    std::size_t pixel_count = 0;
};

struct CircleStats {
    Rgb color{};
    double luma = 0.0;
    std::size_t pixel_count = 0;
};

[[nodiscard]] float parse_threshold(const char* value, const char* name) {
    try {
        std::size_t parsed = 0;
        const float threshold = std::stof(value, &parsed);
        if (parsed != std::string(value).size() || !std::isfinite(threshold) || threshold < 0.0F) {
            throw std::runtime_error("");
        }
        return threshold;
    } catch (...) {
        throw std::runtime_error(std::string("invalid ") + name + " threshold");
    }
}

[[nodiscard]] LoadedImage load_rgba_image(const std::filesystem::path& path) {
    LoadedImage image;
    int channels = 0;
    image.pixels = stbi_load(path.string().c_str(), &image.width, &image.height, &channels, 4);
    if (image.pixels == nullptr) {
        throw std::runtime_error("failed to decode PNG: " + path.string());
    }
    if (image.width <= 0 || image.height <= 0) {
        stbi_image_free(image.pixels);
        throw std::runtime_error("decoded PNG has invalid dimensions");
    }
    return image;
}

[[nodiscard]] ImageStats compute_stats(const LoadedImage& image) {
    ImageStats stats;
    const std::size_t pixel_count =
        static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
    for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
        const unsigned char* rgba = image.pixels + pixel * 4U;
        const double red = static_cast<double>(rgba[0]) / 255.0;
        const double green = static_cast<double>(rgba[1]) / 255.0;
        const double blue = static_cast<double>(rgba[2]) / 255.0;
        const double luma = red * 0.2126 + green * 0.7152 + blue * 0.0722;
        stats.mean_luma += luma;
        stats.min_luma = std::min(stats.min_luma, luma);
        stats.max_luma = std::max(stats.max_luma, luma);
    }
    stats.mean_luma /= static_cast<double>(pixel_count);
    return stats;
}

[[nodiscard]] Rgb pixel_rgb(const LoadedImage& image, int x, int y) {
    const std::size_t offset =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
         static_cast<std::size_t>(x)) *
        4U;
    return {
        .red = static_cast<double>(image.pixels[offset]) / 255.0,
        .green = static_cast<double>(image.pixels[offset + 1U]) / 255.0,
        .blue = static_cast<double>(image.pixels[offset + 2U]) / 255.0,
    };
}

[[nodiscard]] double luma(Rgb color) {
    return color.red * 0.2126 + color.green * 0.7152 + color.blue * 0.0722;
}

[[nodiscard]] Rgb corner_background(const LoadedImage& image) {
    constexpr std::array<std::array<float, 2>, 4> kSamples{{
        {{0.03F, 0.03F}},
        {{0.97F, 0.03F}},
        {{0.03F, 0.97F}},
        {{0.97F, 0.97F}},
    }};
    Rgb result;
    for (const auto& sample : kSamples) {
        const int x = static_cast<int>(sample[0] * static_cast<float>(image.width - 1));
        const int y = static_cast<int>(sample[1] * static_cast<float>(image.height - 1));
        const Rgb color = pixel_rgb(image, x, y);
        result.red += color.red;
        result.green += color.green;
        result.blue += color.blue;
    }
    result.red /= static_cast<double>(kSamples.size());
    result.green /= static_cast<double>(kSamples.size());
    result.blue /= static_cast<double>(kSamples.size());
    return result;
}

[[nodiscard]] RegionStats foreground_vertical_region(const LoadedImage& image, int region,
                                                     int region_count, double background_luma) {
    const int x_begin = (image.width * region) / region_count;
    const int x_end = (image.width * (region + 1)) / region_count;
    const int y_begin = image.height / 8;
    const int y_end = image.height - y_begin;
    RegionStats stats;
    for (int y = y_begin; y < y_end; ++y) {
        for (int x = x_begin; x < x_end; ++x) {
            const Rgb color = pixel_rgb(image, x, y);
            if (luma(color) <= background_luma + 0.04) {
                continue;
            }
            stats.color.red += color.red;
            stats.color.green += color.green;
            stats.color.blue += color.blue;
            stats.luma += luma(color);
            ++stats.pixel_count;
        }
    }
    if (stats.pixel_count != 0U) {
        const double count = static_cast<double>(stats.pixel_count);
        stats.color.red /= count;
        stats.color.green /= count;
        stats.color.blue /= count;
        stats.luma /= count;
    }
    return stats;
}

[[nodiscard]] CircleStats normalized_circle_region(const LoadedImage& image, float center_x,
                                                   float center_y, float radius) {
    const float image_scale = static_cast<float>(std::min(image.width, image.height));
    const float radius_pixels = radius * image_scale;
    const float radius_squared = radius_pixels * radius_pixels;
    const int center_pixel_x = static_cast<int>(center_x * static_cast<float>(image.width));
    const int center_pixel_y = static_cast<int>(center_y * static_cast<float>(image.height));
    const int range = static_cast<int>(std::ceil(radius_pixels));
    CircleStats stats;
    for (int y = std::max(center_pixel_y - range, 0);
         y <= std::min(center_pixel_y + range, image.height - 1); ++y) {
        for (int x = std::max(center_pixel_x - range, 0);
             x <= std::min(center_pixel_x + range, image.width - 1); ++x) {
            const float dx = static_cast<float>(x - center_pixel_x);
            const float dy = static_cast<float>(y - center_pixel_y);
            if ((dx * dx) + (dy * dy) > radius_squared) {
                continue;
            }
            const Rgb color = pixel_rgb(image, x, y);
            stats.color.red += color.red;
            stats.color.green += color.green;
            stats.color.blue += color.blue;
            stats.luma += luma(color);
            ++stats.pixel_count;
        }
    }
    if (stats.pixel_count != 0U) {
        const double count = static_cast<double>(stats.pixel_count);
        stats.color.red /= count;
        stats.color.green /= count;
        stats.color.blue /= count;
        stats.luma /= count;
    }
    return stats;
}

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_furnace_regions(const std::array<RegionStats, 4>& regions,
                             const char* conformance_case) {
    for (const RegionStats& region : regions) {
        require(region.pixel_count >= 64U,
                "PBR furnace conformance capture is missing an inspectable specimen");
    }
    const auto minmax = std::minmax_element(
        regions.begin(), regions.end(),
        [](const RegionStats& lhs, const RegionStats& rhs) { return lhs.luma < rhs.luma; });
    require(minmax.second->luma - minmax.first->luma > 0.015,
            "PBR furnace conformance materials did not produce a visible relational difference");
    std::printf("material_conformance: %s", conformance_case);
    for (const RegionStats& region : regions) {
        std::printf(" luma=%.4f rgb=(%.4f,%.4f,%.4f)", region.luma, region.color.red,
                    region.color.green, region.color.blue);
    }
    std::printf("\n");
}

void check_furnace_ior(const LoadedImage& image) {
    const double background_luma = luma(corner_background(image));
    std::array<RegionStats, 4> regions{};
    for (int index = 0; index < 4; ++index) {
        regions[static_cast<std::size_t>(index)] =
            foreground_vertical_region(image, index, 4, background_luma);
    }
    require_furnace_regions(regions, "furnace-ior");
    require(
        regions[0].luma + 0.001 < regions[1].luma && regions[1].luma + 0.001 < regions[2].luma &&
            regions[2].luma + 0.001 < regions[3].luma,
        "IOR conformance specimens should retain the authored 1.0 < 1.5 < 2.42 < 0 response order");
    for (const RegionStats& region : regions) {
        require(std::abs(region.color.red - region.color.green) < 0.025 &&
                    std::abs(region.color.green - region.color.blue) < 0.025,
                "white-IBL IOR specimens should preserve neutral color channels");
    }
}

void check_furnace_specular(const LoadedImage& image) {
    const double background_luma = luma(corner_background(image));
    std::array<RegionStats, 4> regions{};
    for (int index = 0; index < 4; ++index) {
        regions[static_cast<std::size_t>(index)] =
            foreground_vertical_region(image, index, 4, background_luma);
    }
    require_furnace_regions(regions, "furnace-specular");
    require(std::abs(regions[0].luma - regions[1].luma) > 0.01,
            "specular factor zero and one should produce different white-IBL responses");
    require(regions[2].color.red > regions[2].color.blue + 0.02,
            "red specular-color specimen should retain a red F0 response");
    require(regions[3].color.blue > regions[3].color.red + 0.02,
            "blue specular-color specimen should retain a blue F0 response");
}

void check_gltf_specular_test(const LoadedImage& image) {
    // This is the fixed front-on capture layout in projects/gltf_viewer. Sampling the sphere
    // centers, rather than a foreground mask, prevents the atmosphere background from acting as
    // evidence for a dark or missing SpecularTest model.
    constexpr std::size_t kColumnCount = 5U;
    constexpr std::size_t kRowCount = 7U;
    constexpr float kFirstCenterX = 0.529F;
    constexpr float kFirstCenterY = 0.363F;
    constexpr float kCenterSpacing = 0.045F;
    constexpr float kSphereRadius = 0.015F;
    std::array<CircleStats, kColumnCount * kRowCount> spheres{};
    for (std::size_t row = 0; row < kRowCount; ++row) {
        for (std::size_t column = 0; column < kColumnCount; ++column) {
            spheres[(row * kColumnCount) + column] = normalized_circle_region(
                image, kFirstCenterX + (static_cast<float>(column) * kCenterSpacing),
                kFirstCenterY + (static_cast<float>(row) * kCenterSpacing), kSphereRadius);
        }
    }
    for (const CircleStats& sphere : spheres) {
        require(sphere.pixel_count >= 128U,
                "SpecularTest capture should retain all fixed-grid sphere specimens");
    }

    const auto sphere = [&spheres](std::size_t row, std::size_t column) -> const CircleStats& {
        return spheres[(row * kColumnCount) + column];
    };
    const CircleStats& factor_minimum = sphere(0U, 0U);
    const CircleStats& factor_maximum = sphere(0U, 4U);
    const CircleStats& white_minimum = sphere(2U, 0U);
    const CircleStats& white_maximum = sphere(2U, 4U);
    const CircleStats& yellow_maximum = sphere(4U, 4U);
    std::printf("material_conformance: gltf-specular-test factor=(%.4f,%.4f) white=(%.4f,%.4f) "
                "yellow=(%.4f,%.4f,%.4f)\n",
                factor_minimum.luma, factor_maximum.luma, white_minimum.luma, white_maximum.luma,
                yellow_maximum.color.red, yellow_maximum.color.green, yellow_maximum.color.blue);
    require(factor_maximum.luma > factor_minimum.luma + 0.015,
            "SpecularTest factor row should retain a visible zero-to-one response");
    require(white_maximum.luma > white_minimum.luma + 0.015,
            "SpecularTest white-color row should retain a visible zero-to-one response");
    require(yellow_maximum.color.red > yellow_maximum.color.blue + 0.01 &&
                yellow_maximum.color.green > yellow_maximum.color.blue + 0.01,
            "SpecularTest yellow-color row should retain a bounded model chromatic response");
}

void check_material_conformance(const std::filesystem::path& path, std::string_view case_name) {
    LoadedImage image = load_rgba_image(path);
    try {
        if (case_name == "furnace-ior") {
            check_furnace_ior(image);
        } else if (case_name == "furnace-specular") {
            check_furnace_specular(image);
        } else if (case_name == "gltf-specular-test") {
            check_gltf_specular_test(image);
        } else {
            throw std::runtime_error("unknown material conformance case");
        }
    } catch (...) {
        stbi_image_free(image.pixels);
        throw;
    }
    stbi_image_free(image.pixels);
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::string_view{argv[1]} == "--material-conformance") {
        try {
            check_material_conformance(argv[3], argv[2]);
            return 0;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "material_conformance failed: %s\n", error.what());
            return 1;
        }
    }
    if (argc != 4) {
        std::fprintf(stderr, "usage: %s <png> <min-mean-luma> <min-luma-range>\n", argv[0]);
        return 2;
    }

    try {
        const std::filesystem::path path = argv[1];
        const float min_mean_luma = parse_threshold(argv[2], "mean luma");
        const float min_luma_range = parse_threshold(argv[3], "luma range");
        LoadedImage image = load_rgba_image(path);
        const ImageStats stats = compute_stats(image);
        stbi_image_free(image.pixels);

        const double luma_range = stats.max_luma - stats.min_luma;
        std::printf("png_stats: %s mean=%.6f min=%.6f max=%.6f range=%.6f\n", path.string().c_str(),
                    stats.mean_luma, stats.min_luma, stats.max_luma, luma_range);
        if (stats.mean_luma < static_cast<double>(min_mean_luma) ||
            luma_range < static_cast<double>(min_luma_range)) {
            std::fprintf(stderr, "PNG stats below thresholds: mean >= %.6f, range >= %.6f\n",
                         min_mean_luma, min_luma_range);
            return 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "png_stats failed: %s\n", error.what());
        return 1;
    }
}
