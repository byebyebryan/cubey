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

struct ContrastStats {
    double mean_luma = 0.0;
    double standard_deviation = 0.0;
    std::size_t pixel_count = 0;
};

struct ChromaStats {
    double mean_chroma = 0.0;
    std::size_t pixel_count = 0;
};

struct EdgePositionStats {
    double position = 0.0;
    double strength = 0.0;
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

[[nodiscard]] double srgb_channel_to_linear(double value) {
    const double clamped = std::clamp(value, 0.0, 1.0);
    if (clamped <= 0.04045) {
        return clamped / 12.92;
    }
    return std::pow((clamped + 0.055) / 1.055, 2.4);
}

[[nodiscard]] Rgb srgb_to_linear(Rgb color) {
    return {
        .red = srgb_channel_to_linear(color.red),
        .green = srgb_channel_to_linear(color.green),
        .blue = srgb_channel_to_linear(color.blue),
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

[[nodiscard]] double maximum_foreground_chroma_vertical_region(const LoadedImage& image, int region,
                                                               int region_count,
                                                               double background_luma) {
    const int x_begin = (image.width * region) / region_count;
    const int x_end = (image.width * (region + 1)) / region_count;
    const int y_begin = image.height / 8;
    const int y_end = image.height - y_begin;
    double maximum_chroma = 0.0;
    for (int y = y_begin; y < y_end; ++y) {
        for (int x = x_begin; x < x_end; ++x) {
            const Rgb color = pixel_rgb(image, x, y);
            if (luma(color) <= background_luma + 0.04) {
                continue;
            }
            maximum_chroma =
                std::max(maximum_chroma, std::max({color.red, color.green, color.blue}) -
                                             std::min({color.red, color.green, color.blue}));
        }
    }
    return maximum_chroma;
}

[[nodiscard]] CircleStats normalized_circle_region(const LoadedImage& image, float center_x,
                                                   float center_y, float radius,
                                                   bool decode_srgb = false) {
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
            const Rgb color = decode_srgb ? srgb_to_linear(pixel_rgb(image, x, y))
                                          : pixel_rgb(image, x, y);
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

[[nodiscard]] ContrastStats normalized_square_contrast_region(const LoadedImage& image,
                                                              float center_x, float center_y,
                                                              float half_extent,
                                                              bool decode_srgb = false) {
    const float image_scale = static_cast<float>(std::min(image.width, image.height));
    const int center_pixel_x = static_cast<int>(center_x * static_cast<float>(image.width));
    const int center_pixel_y = static_cast<int>(center_y * static_cast<float>(image.height));
    const int half_extent_pixels = static_cast<int>(half_extent * image_scale);
    const int first_x = std::max(0, center_pixel_x - half_extent_pixels);
    const int last_x = std::min(image.width - 1, center_pixel_x + half_extent_pixels - 1);
    const int first_y = std::max(0, center_pixel_y - half_extent_pixels);
    const int last_y = std::min(image.height - 1, center_pixel_y + half_extent_pixels - 1);

    ContrastStats stats;
    double sum_squared_luma = 0.0;
    for (int y = first_y; y <= last_y; ++y) {
        for (int x = first_x; x <= last_x; ++x) {
            Rgb color = pixel_rgb(image, x, y);
            if (decode_srgb) {
                color = srgb_to_linear(color);
            }
            const double pixel_luma = luma(color);
            stats.mean_luma += pixel_luma;
            sum_squared_luma += pixel_luma * pixel_luma;
            ++stats.pixel_count;
        }
    }
    if (stats.pixel_count == 0U) {
        return stats;
    }
    stats.mean_luma /= static_cast<double>(stats.pixel_count);
    const double mean_square = sum_squared_luma / static_cast<double>(stats.pixel_count);
    stats.standard_deviation =
        std::sqrt(std::max(0.0, mean_square - (stats.mean_luma * stats.mean_luma)));
    return stats;
}

[[nodiscard]] ChromaStats normalized_chroma_region(const LoadedImage& image, float center_x,
                                                   float center_y, float half_width,
                                                   float half_height, bool decode_srgb = false) {
    const int center_pixel_x = static_cast<int>(center_x * static_cast<float>(image.width));
    const int center_pixel_y = static_cast<int>(center_y * static_cast<float>(image.height));
    const int half_width_pixels = static_cast<int>(half_width * static_cast<float>(image.width));
    const int half_height_pixels =
        static_cast<int>(half_height * static_cast<float>(image.height));
    const int first_x = std::max(0, center_pixel_x - half_width_pixels);
    const int last_x = std::min(image.width - 1, center_pixel_x + half_width_pixels - 1);
    const int first_y = std::max(0, center_pixel_y - half_height_pixels);
    const int last_y = std::min(image.height - 1, center_pixel_y + half_height_pixels - 1);

    ChromaStats stats;
    for (int y = first_y; y <= last_y; ++y) {
        for (int x = first_x; x <= last_x; ++x) {
            Rgb color = pixel_rgb(image, x, y);
            if (decode_srgb) {
                color = srgb_to_linear(color);
            }
            stats.mean_chroma +=
                std::max({color.red, color.green, color.blue}) -
                std::min({color.red, color.green, color.blue});
            ++stats.pixel_count;
        }
    }
    if (stats.pixel_count != 0U) {
        stats.mean_chroma /= static_cast<double>(stats.pixel_count);
    }
    return stats;
}

[[nodiscard]] EdgePositionStats vertical_checker_edge(const LoadedImage& image, float center_x,
                                                       float center_y, int expected_offset,
                                                       bool decode_srgb = false) {
    const int center_pixel_x = static_cast<int>(center_x * static_cast<float>(image.width));
    const int center_pixel_y = static_cast<int>(center_y * static_cast<float>(image.height));
    constexpr int kSampleOffsetY = 20;
    constexpr int kSearchRadius = 24;
    EdgePositionStats stats;
    for (int offset = expected_offset - kSearchRadius; offset <= expected_offset + kSearchRadius;
         ++offset) {
        const int x = center_pixel_x + offset;
        const int sample_y = center_pixel_y + kSampleOffsetY;
        if (x <= 0 || x + 1 >= image.width || sample_y <= 0 ||
            sample_y >= image.height - 1) {
            continue;
        }
        Rgb left = pixel_rgb(image, x - 1, sample_y);
        Rgb right = pixel_rgb(image, x + 1, sample_y);
        if (decode_srgb) {
            left = srgb_to_linear(left);
            right = srgb_to_linear(right);
        }
        const double strength = std::abs(luma(right) - luma(left));
        if (strength > stats.strength) {
            stats.strength = strength;
            stats.position = static_cast<double>(offset);
        }
    }
    return stats;
}

[[nodiscard]] EdgePositionStats horizontal_checker_edge(const LoadedImage& image, float center_x,
                                                         float center_y, int expected_offset,
                                                         bool decode_srgb = false) {
    const int center_pixel_x = static_cast<int>(center_x * static_cast<float>(image.width));
    const int center_pixel_y = static_cast<int>(center_y * static_cast<float>(image.height));
    constexpr int kSampleOffsetX = 20;
    constexpr int kSearchRadius = 24;
    EdgePositionStats stats;
    for (int offset = expected_offset - kSearchRadius; offset <= expected_offset + kSearchRadius;
         ++offset) {
        const int y = center_pixel_y + offset;
        const int sample_x = center_pixel_x + kSampleOffsetX;
        if (sample_x <= 0 || sample_x >= image.width - 1 || y <= 0 || y + 1 >= image.height) {
            continue;
        }
        Rgb top = pixel_rgb(image, sample_x, y - 1);
        Rgb bottom = pixel_rgb(image, sample_x, y + 1);
        if (decode_srgb) {
            top = srgb_to_linear(top);
            bottom = srgb_to_linear(bottom);
        }
        const double strength = std::abs(luma(bottom) - luma(top));
        if (strength > stats.strength) {
            stats.strength = strength;
            stats.position = static_cast<double>(offset);
        }
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
    // This fixed, neutral direct-plus-IBL fixture deterministically exposes a
    // descending response as its dielectric IOR sweep removes diffuse energy.
    // It is a furnace witness, not a general ordering rule for glTF assets.
    require(regions[0].luma > regions[1].luma + 0.001 &&
                regions[1].luma > regions[2].luma + 0.001 &&
                regions[2].luma > regions[3].luma + 0.001,
            "IOR conformance should retain the valid-surface 1.0 > 1.5 > 2.42 > 0 response order");
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

void check_furnace_clearcoat(const LoadedImage& image) {
    const double background_luma = luma(corner_background(image));
    std::array<RegionStats, 4> regions{};
    for (int index = 0; index < 4; ++index) {
        regions[static_cast<std::size_t>(index)] =
            foreground_vertical_region(image, index, 4, background_luma);
    }
    require_furnace_regions(regions, "furnace-clearcoat");
    // The left pair keeps the underlying material fixed while clearcoatFactor
    // is zero, so a clearcoat roughness change must be inert. The right pair
    // enables the layer and isolates its roughness under the same white IBL.
    require(std::abs(regions[0].luma - regions[1].luma) < 0.01,
            "clearcoat factor zero should leave roughness and the full underlying material inert");
    require(std::abs(regions[2].luma - regions[3].luma) > 0.005,
            "enabled clearcoat roughness should change the layered IBL response");
    require(std::abs(regions[0].luma - regions[2].luma) > 0.005,
            "enabled clearcoat should change the layered base response");
}

void check_furnace_anisotropy(const LoadedImage& image) {
    const double background_luma = luma(corner_background(image));
    std::array<RegionStats, 4> regions{};
    for (int index = 0; index < 4; ++index) {
        regions[static_cast<std::size_t>(index)] =
            foreground_vertical_region(image, index, 4, background_luma);
    }
    require_furnace_regions(regions, "furnace-anisotropy");
    // The left pair differs only in rotation while strength is zero. The right
    // pair enables the same strength at orthogonal rotations under the fixed
    // conformance light.
    require(std::abs(regions[0].luma - regions[1].luma) < 0.01,
            "anisotropy strength zero should leave rotation inert");
    require(std::abs(regions[2].luma - regions[3].luma) > 0.01,
            "enabled anisotropy rotation should change the directional response");
    require(std::abs(regions[0].luma - regions[2].luma) > 0.01,
            "enabled anisotropy should change the isotropic directional response");
}

void check_furnace_iridescence(const LoadedImage& image) {
    const double background_luma = luma(corner_background(image));
    std::array<RegionStats, 4> regions{};
    for (int index = 0; index < 4; ++index) {
        regions[static_cast<std::size_t>(index)] =
            foreground_vertical_region(image, index, 4, background_luma);
    }
    require_furnace_regions(regions, "furnace-iridescence");
    // The left controls differ only in a film thickness that must be inert at
    // zero factor. The enabled right pair exercises thin-film response over
    // both dielectric and metallic bases with distinct thicknesses.
    require(std::abs(regions[0].luma - regions[1].luma) < 0.01,
            "iridescence factor zero should leave film thickness inert");
    const auto chroma = [](const RegionStats& region) {
        return std::max({region.color.red, region.color.green, region.color.blue}) -
               std::min({region.color.red, region.color.green, region.color.blue});
    };
    const double dielectric_maximum_chroma =
        maximum_foreground_chroma_vertical_region(image, 2, 4, background_luma);
    const double metallic_maximum_chroma =
        maximum_foreground_chroma_vertical_region(image, 3, 4, background_luma);
    std::printf("material_conformance: furnace-iridescence maximum_chroma=(%.4f,%.4f)\n",
                dielectric_maximum_chroma, metallic_maximum_chroma);
    require(dielectric_maximum_chroma > 0.01,
            "enabled dielectric iridescence should produce a visible chromatic response");
    require(metallic_maximum_chroma > 0.01,
            "enabled metallic iridescence should produce a visible chromatic response");
    require(std::abs(regions[2].luma - regions[3].luma) > 0.005 ||
                std::abs(chroma(regions[2]) - chroma(regions[3])) > 0.005,
            "enabled iridescence bases and film thicknesses should retain a visible difference");
}

void check_furnace_sheen(const LoadedImage& image) {
    const double background_luma = luma(corner_background(image));
    std::array<RegionStats, 4> regions{};
    for (int index = 0; index < 4; ++index) {
        regions[static_cast<std::size_t>(index)] =
            foreground_vertical_region(image, index, 4, background_luma);
    }
    require_furnace_regions(regions, "furnace-sheen");
    require(std::abs(regions[0].luma - regions[1].luma) < 0.01,
            "sheen color zero should leave roughness and the full base response inert");
    const auto chroma = [](const RegionStats& region) {
        return std::max({region.color.red, region.color.green, region.color.blue}) -
               std::min({region.color.red, region.color.green, region.color.blue});
    };
    const double low_roughness_maximum_chroma =
        maximum_foreground_chroma_vertical_region(image, 2, 4, background_luma);
    const double high_roughness_maximum_chroma =
        maximum_foreground_chroma_vertical_region(image, 3, 4, background_luma);
    std::printf("material_conformance: furnace-sheen maximum_chroma=(%.4f,%.4f)\n",
                low_roughness_maximum_chroma, high_roughness_maximum_chroma);
    require(low_roughness_maximum_chroma > 0.02 && high_roughness_maximum_chroma > 0.02,
            "enabled cyan sheen specimens should retain a visible layered response");
    require(std::abs(regions[2].luma - regions[3].luma) > 0.005 ||
                std::abs(chroma(regions[2]) - chroma(regions[3])) > 0.005,
            "enabled sheen roughness should change the layered direct-plus-IBL response");
    for (const RegionStats& region : regions) {
        require(region.luma < 0.95 && region.color.red < 0.98 && region.color.green < 0.98 &&
                    region.color.blue < 0.98,
                "sheen furnace response should remain bounded below display saturation");
    }
}

void check_furnace_transmission(const LoadedImage& image) {
    constexpr std::array<float, 5> kCentersX{{0.150F, 0.325F, 0.500F, 0.675F, 0.850F}};
    constexpr float kCenterY = 0.500F;
    constexpr float kRadius = 0.050F;

    require(image.width == 512 && image.height == 512,
            "transmission furnace conformance requires its calibrated 512x512 capture");
    const Rgb background = corner_background(image);
    std::array<CircleStats, 5> specimens{};
    for (std::size_t index = 0; index < specimens.size(); ++index) {
        specimens[index] = normalized_circle_region(image, kCentersX[index], kCenterY, kRadius);
        require(specimens[index].pixel_count >= 1'000U,
                "transmission furnace capture should retain every specimen center");
        require(std::isfinite(specimens[index].luma) &&
                    std::isfinite(specimens[index].color.red) &&
                    std::isfinite(specimens[index].color.green) &&
                    std::isfinite(specimens[index].color.blue) &&
                    specimens[index].luma >= 0.0 && specimens[index].luma <= 1.0 &&
                    specimens[index].color.red >= 0.0 && specimens[index].color.red <= 1.0 &&
                    specimens[index].color.green >= 0.0 && specimens[index].color.green <= 1.0 &&
                    specimens[index].color.blue >= 0.0 && specimens[index].color.blue <= 1.0,
                "transmission furnace direct-BTDF output should remain finite and display bounded");
    }

    std::printf("material_conformance: furnace-transmission background=%.4f", luma(background));
    for (const CircleStats& specimen : specimens) {
        std::printf(" luma=%.4f rgb=(%.4f,%.4f,%.4f)", specimen.luma, specimen.color.red,
                    specimen.color.green, specimen.color.blue);
    }
    std::printf("\n");

    // The first face-on sphere sees only the back light with transmission
    // disabled and no IBL, retaining the old dark result. The remaining
    // witnesses are bright only through the direct microfacet BTDF.
    require(specimens[0].luma <= luma(background) + 0.012,
            "transmission factor zero should retain the unlit back-face control");
    require(specimens[1].luma >= specimens[0].luma + 0.10,
            "enabled smooth transmission should receive visible direct BTDF radiance");
    require(std::abs(specimens[1].luma - specimens[2].luma) >= 0.030,
            "enabled transmission roughness should change direct BTDF response");
    require(std::abs(specimens[1].luma - specimens[3].luma) >= 0.030,
            "enabled transmission IOR should change direct BTDF response");
    require(specimens[4].color.green >= specimens[4].color.red + 0.080 &&
                specimens[4].color.green >= specimens[4].color.blue + 0.080,
            "thick transmission should retain the authored green Beer-Lambert attenuation trend");
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

[[nodiscard]] double relative_color_distance(const CircleStats& lhs, const CircleStats& rhs) {
    const double red_delta = lhs.color.red - rhs.color.red;
    const double green_delta = lhs.color.green - rhs.color.green;
    const double blue_delta = lhs.color.blue - rhs.color.blue;
    const double distance =
        std::sqrt((red_delta * red_delta) + (green_delta * green_delta) +
                  (blue_delta * blue_delta));
    const double reference_length =
        std::sqrt((lhs.color.red * lhs.color.red) + (lhs.color.green * lhs.color.green) +
                  (lhs.color.blue * lhs.color.blue));
    // Keep malformed/black answer-key diagnostics finite without affecting the calibrated cells,
    // whose linear reference-vector lengths are all above 0.08.
    return distance / std::max(reference_length, 0.01);
}

void check_gltf_attenuation_test(const LoadedImage& image) {
    // These coordinates are the measured centers of the five-by-five authored grid in the
    // pinned AttenuationTest asset. Keep the capture contract in the corresponding CMake test:
    // changing the asset, FOV, camera distance, or aspect ratio requires recalibration rather
    // than silently sampling the backdrop.
    constexpr std::size_t kColumnCount = 5U;
    constexpr std::size_t kRowCount = 5U;
    constexpr std::array<float, kColumnCount> kColumnCenters{{0.404F, 0.445F, 0.500F, 0.568F,
                                                               0.664F}};
    constexpr std::array<float, kRowCount> kRowCenters{{0.207F, 0.354F, 0.500F, 0.646F, 0.793F}};
    constexpr float kSampleRadius = 0.004F;
    constexpr double kMinimumMonotonicLumaDrop = 0.001;
    // The long-lens calibration below keeps the individual cube faces nearly
    // front-on while preserving the complete authored grid in frame. The
    // answer-key's deepest two columns are intentionally close after the
    // captured sRGB result is decoded to linear, so require a positive,
    // presentation-stable separation rather than a larger arbitrary code
    // value gap.
    constexpr double kMinimumAnswerKeyLumaDrop = 0.002;
    constexpr double kMaximumRelativeShadeDistance = 0.20;

    require(image.width == 1920 && image.height == 1080,
            "AttenuationTest conformance requires its calibrated 1920x1080 capture");

    std::array<std::array<CircleStats, kColumnCount>, kRowCount> cells{};
    for (std::size_t row = 0; row < kRowCount; ++row) {
        for (std::size_t column = 0; column < kColumnCount; ++column) {
            cells[row][column] = normalized_circle_region(
                image, kColumnCenters[column], kRowCenters[row], kSampleRadius, true);
            require(cells[row][column].pixel_count >= 16U,
                    "AttenuationTest capture should retain every calibrated specimen ROI");
        }
    }

    std::printf("material_conformance: gltf-attenuation-test\n");
    for (std::size_t column = 0; column < kColumnCount; ++column) {
        const CircleStats& sample = cells[0][column];
        std::printf("  column[%zu] sample=(%.4f,%.4f,%.4f) luma=%.4f", column,
                    sample.color.red, sample.color.green, sample.color.blue, sample.luma);
        for (std::size_t row = 1; row < kRowCount; ++row) {
            const CircleStats& cell = cells[row][column];
            std::printf(" row[%zu]=(%.4f,%.4f,%.4f) luma=%.4f delta=%.4f", row,
                        cell.color.red, cell.color.green, cell.color.blue, cell.luma,
                        relative_color_distance(sample, cell));
        }
        std::printf("\n");
    }

    // The sample-color row is the authored answer key. Its blue response must remain visibly
    // tinted and monotonically deepen with the 0.25, 0.50, 1.0, 1.5, and 2.0 columns. These
    // checks also catch a shifted grid or a backdrop-only capture before row comparisons run.
    for (std::size_t column = 0; column < kColumnCount; ++column) {
        const CircleStats& sample = cells[0][column];
        require(sample.color.blue > sample.color.red + 0.02 &&
                    sample.color.blue > sample.color.green + 0.01,
                "AttenuationTest sample-color answer key should retain its blue tint");
        if (column != 0U) {
            require(cells[0][column - 1U].luma > sample.luma + kMinimumAnswerKeyLumaDrop,
                    "AttenuationTest sample-color answer key should deepen across columns");
        }
    }

    // Every authored row uses the same 0.25-to-2.0 thickness ordering, so its effective
    // attenuation should deepen in the same direction even when the row encodes thickness in a
    // factor, texture, node scale, or attenuation distance.
    double minimum_monotonic_drop = std::numeric_limits<double>::max();
    std::size_t minimum_drop_row = 0U;
    std::size_t minimum_drop_column = 0U;
    for (std::size_t row = 0; row < kRowCount; ++row) {
        for (std::size_t column = 1; column < kColumnCount; ++column) {
            const double drop = cells[row][column - 1U].luma - cells[row][column].luma;
            if (drop < minimum_monotonic_drop) {
                minimum_monotonic_drop = drop;
                minimum_drop_row = row;
                minimum_drop_column = column;
            }
        }
    }
    std::printf("  minimum monotonic luma drop=%.4f before row[%zu], column[%zu] (limit=%.4f)\n",
                minimum_monotonic_drop, minimum_drop_row, minimum_drop_column,
                kMinimumMonotonicLumaDrop);
    require(minimum_monotonic_drop >= kMinimumMonotonicLumaDrop,
            "AttenuationTest authored row should deepen across thickness columns");

    // The capture uses Linear tonemapping, but its default sRGB color attachment converts the
    // linear shader result before PNG readback. The ROI helper therefore decodes each stored
    // channel back to linear before averaging. Its calibrated long lens is intentional: the
    // asset requires face-on viewing, and a near 45-degree camera adds row-dependent screen
    // refraction/parallax before the Beer-Lambert result can be compared with the answer key.
    // Thickness Factor, Thickness Texture, Node Scale, and Attenuation Distance should all
    // reproduce the answer-key shade in their corresponding column. This is intentionally a
    // relational check, not a golden-image comparison; the fixed relative threshold allows
    // small raster and driver differences while still exposing an extension implementation gap.
    double maximum_shade_distance = 0.0;
    std::size_t maximum_shade_row = 0U;
    std::size_t maximum_shade_column = 0U;
    for (std::size_t row = 1; row < kRowCount; ++row) {
        for (std::size_t column = 0; column < kColumnCount; ++column) {
            const double distance = relative_color_distance(cells[0][column], cells[row][column]);
            if (distance > maximum_shade_distance) {
                maximum_shade_distance = distance;
                maximum_shade_row = row;
                maximum_shade_column = column;
            }
        }
    }
    std::printf("  maximum answer-key distance=%.4f at row[%zu], column[%zu] (limit=%.4f)\n",
                maximum_shade_distance, maximum_shade_row, maximum_shade_column,
                kMaximumRelativeShadeDistance);
    require(maximum_shade_distance <= kMaximumRelativeShadeDistance,
            "AttenuationTest authored row should match its answer-key column shade");
}

void check_gltf_transmission_roughness_test(const LoadedImage& image) {
    // The pinned TransmissionRoughnessTest GLB presents five IOR rows (2.42, 1.76, 1.50,
    // 1.33, and 1.0 from top to bottom) and nine roughness columns from smooth to rough. The
    // complete asset is captured face-on at the fixed size and framing in CMake; these ROIs stay
    // inside the tile faces and measure the transmitted chart's local luma contrast.
    constexpr std::size_t kRowCount = 5U;
    constexpr std::size_t kColumnCount = 9U;
    constexpr std::array<float, kColumnCount> kColumnCenters{{
        0.440625F, 0.468750F, 0.496875F, 0.525000F, 0.553125F,
        0.581250F, 0.609375F, 0.637500F, 0.665625F,
    }};
    constexpr std::array<float, kRowCount> kRowCenters{{
        0.398148F, 0.450926F, 0.501852F, 0.552778F, 0.601852F,
    }};
    constexpr float kTileHalfExtent = 0.016667F;
    constexpr std::size_t kSmoothColumn = 0U;
    constexpr std::size_t kRoughColumnStart = 6U;
    constexpr double kMinimumTransmittedContrastDrop = 0.007;
    constexpr double kMaximumAirContrastDrop = 0.006;
    constexpr double kMinimumIorSensitivitySeparation = 0.008;

    require(image.width == 1920 && image.height == 1080,
            "TransmissionRoughnessTest conformance requires its calibrated 1920x1080 capture");

    std::array<std::array<ContrastStats, kColumnCount>, kRowCount> tiles{};
    for (std::size_t row = 0; row < kRowCount; ++row) {
        for (std::size_t column = 0; column < kColumnCount; ++column) {
            tiles[row][column] = normalized_square_contrast_region(
                image, kColumnCenters[column], kRowCenters[row], kTileHalfExtent, true);
            require(tiles[row][column].pixel_count >= 1000U,
                    "TransmissionRoughnessTest capture should retain every authored tile ROI");
        }
    }

    std::array<double, kRowCount> roughness_drops{};
    std::printf("material_conformance: gltf-transmission-roughness-test\n");
    for (std::size_t row = 0; row < kRowCount; ++row) {
        double smooth_contrast = tiles[row][kSmoothColumn].standard_deviation;
        double rough_contrast = 0.0;
        for (std::size_t column = kRoughColumnStart; column < kColumnCount; ++column) {
            rough_contrast += tiles[row][column].standard_deviation;
        }
        rough_contrast /= static_cast<double>(kColumnCount - kRoughColumnStart);
        roughness_drops[row] = smooth_contrast - rough_contrast;
        std::printf("  row[%zu] smooth=%.4f rough_mean=%.4f drop=%.4f contrast=", row,
                    smooth_contrast, rough_contrast, roughness_drops[row]);
        for (std::size_t column = 0; column < kColumnCount; ++column) {
            std::printf(" %.4f", tiles[row][column].standard_deviation);
        }
        std::printf("\n");
    }

    // For every authored IOR above air, increasing roughness must measurably blur the transmitted
    // chart, and the smooth control must retain more local contrast than the rough end of the row.
    for (std::size_t row = 0; row + 1U < kRowCount; ++row) {
        require(roughness_drops[row] >= kMinimumTransmittedContrastDrop,
                "TransmissionRoughnessTest IOR>1 row should lose transmitted contrast with roughness");
        require(tiles[row][kSmoothColumn].standard_deviation >
                    tiles[row][kColumnCount - 1U].standard_deviation +
                        kMinimumTransmittedContrastDrop,
                "TransmissionRoughnessTest smooth column should retain more transmitted contrast");
    }

    // IOR=1 is the intentional negative control: microfacet roughness cannot refract air into a
    // different direction, so its transmitted chart should remain comparatively sharp. Requiring
    // a bounded drop here catches accidentally applying roughness as a generic blur, while the
    // separation from the highest-IOR row catches an implementation that ignores IOR entirely.
    require(roughness_drops[kRowCount - 1U] <= kMaximumAirContrastDrop,
            "TransmissionRoughnessTest IOR=1 control should remain insensitive to roughness");
    require(roughness_drops[0] >= roughness_drops[kRowCount - 1U] +
                                      kMinimumIorSensitivitySeparation,
            "TransmissionRoughnessTest higher IOR should amplify roughness sensitivity");
}

void check_gltf_dispersion_test(const LoadedImage& image) {
    // The pinned DispersionTest asset presents five IOR rows (2.42, 1.76, 1.50, 1.33, and
    // 1.0 from top to bottom) and five dispersion columns (0.0, 0.5, 1.0, 2.0, and 5.0 from
    // left to right). The face-on capture is intentionally pitched down just enough to expose
    // the prism edges; each ROI is centered on the corresponding authored prism rather than on
    // the checker backdrop.
    constexpr std::size_t kRowCount = 5U;
    constexpr std::size_t kColumnCount = 5U;
    constexpr std::array<float, kColumnCount> kColumnCenters{{
        0.4375F, 0.479167F, 0.520833F, 0.5625F, 0.604167F,
    }};
    constexpr std::array<float, kRowCount> kRowCenters{{
        0.402778F, 0.449074F, 0.495370F, 0.541667F, 0.587963F,
    }};
    constexpr float kHalfWidth = 0.011F;
    constexpr float kHalfHeight = 0.012F;
    constexpr std::size_t kHighIorRowCount = 2U;
    constexpr std::size_t kAirRow = kRowCount - 1U;
    constexpr std::size_t kZeroDispersionColumn = 0U;
    constexpr std::size_t kMaximumDispersionColumn = kColumnCount - 1U;
    // The fixed static environment still gives each column a slightly different neutral edge
    // response. Subtracting the IOR=1 row for each endpoint column removes that presentation
    // component before testing dispersion. Repeated captures measured a 0.016 endpoint gain;
    // retain roughly half that margin for driver/raster variation while catching a disabled
    // dispersion path.
    constexpr double kMinimumHighIorEndpointGain = 0.008;
    // Dispersion is defined to scale with IOR, so the IOR=1 row is a negative control. Its
    // endpoint spread stayed below 0.002 in repeated captures; the wider bound permits small
    // camera/driver differences without accepting a generally active dispersion path at air.
    constexpr double kMaximumAirEndpointSpread = 0.012;

    require(image.width == 1920 && image.height == 1080,
            "DispersionTest conformance requires its calibrated 1920x1080 capture");

    std::array<std::array<ChromaStats, kColumnCount>, kRowCount> cells{};
    for (std::size_t row = 0; row < kRowCount; ++row) {
        for (std::size_t column = 0; column < kColumnCount; ++column) {
            cells[row][column] = normalized_chroma_region(
                image, kColumnCenters[column], kRowCenters[row], kHalfWidth, kHalfHeight, true);
            require(cells[row][column].pixel_count >= 500U,
                    "DispersionTest capture should retain every authored prism ROI");
        }
    }

    std::printf("material_conformance: gltf-dispersion-test\n");
    for (std::size_t row = 0; row < kRowCount; ++row) {
        std::printf("  row[%zu] chroma=", row);
        for (std::size_t column = 0; column < kColumnCount; ++column) {
            std::printf(" %.4f", cells[row][column].mean_chroma);
        }
        std::printf("\n");
    }

    double high_ior_zero_dispersion = 0.0;
    double high_ior_maximum_dispersion = 0.0;
    for (std::size_t row = 0; row < kHighIorRowCount; ++row) {
        high_ior_zero_dispersion += cells[row][kZeroDispersionColumn].mean_chroma;
        high_ior_maximum_dispersion += cells[row][kMaximumDispersionColumn].mean_chroma;
    }
    high_ior_zero_dispersion /= static_cast<double>(kHighIorRowCount);
    high_ior_maximum_dispersion /= static_cast<double>(kHighIorRowCount);
    const double zero_dispersion_air = cells[kAirRow][kZeroDispersionColumn].mean_chroma;
    const double maximum_dispersion_air = cells[kAirRow][kMaximumDispersionColumn].mean_chroma;
    const double high_ior_endpoint_gain =
        (high_ior_maximum_dispersion - maximum_dispersion_air) -
        (high_ior_zero_dispersion - zero_dispersion_air);
    const double air_endpoint_spread =
        std::abs(maximum_dispersion_air - zero_dispersion_air);

    std::printf("  high-ior edge chroma: dispersion0=%.4f dispersion5=%.4f gain=%.4f (limit=%.4f)\n",
                high_ior_zero_dispersion, high_ior_maximum_dispersion, high_ior_endpoint_gain,
                kMinimumHighIorEndpointGain);
    std::printf("  IOR=1 endpoint spread=%.4f (limit=%.4f)\n", air_endpoint_spread,
                kMaximumAirEndpointSpread);
    require(high_ior_endpoint_gain >= kMinimumHighIorEndpointGain,
            "DispersionTest high-IOR endpoint should gain chromatic edge separation");
    require(air_endpoint_spread <= kMaximumAirEndpointSpread,
            "DispersionTest IOR=1 control should remain insensitive to dispersion");
}

void check_gltf_transmission_thinwall_test(const LoadedImage& image) {
    // The pinned TransmissionThinwallTestGrid asset lays out five IOR values from left to right
    // (1.00, 1.33, 1.50, 1.76, 2.42), with thin-wall spheres above matching volume spheres below.
    // The checker backdrop is phase-aligned between each pair. Compare local checker gradients,
    // rather than mean color, so the metric follows macroscopic refraction and is not dominated
    // by the static environment or a highlight moving across the sphere.
    constexpr std::size_t kColumnCount = 5U;
    constexpr std::array<float, kColumnCount> kColumnCenters{{
        0.4387F, 0.5204F, 0.6022F, 0.6840F, 0.7710F,
    }};
    constexpr float kThinWallCenterY = 0.4964F;
    constexpr float kVolumeCenterY = 0.6436F;
    // These are offsets from the projected sphere centers (not from the enclosing crop). The
    // checker phase changes slightly across the five columns, while the calibrated camera keeps
    // the horizontal edge at the same top-row offset. The edge helpers sample 20 pixels off the
    // sphere center to avoid the central environment highlight and locate the checker step in a
    // fixed local window; they compare position, not brightness.
    constexpr int kHorizontalEdgeOffset = 2;
    constexpr std::array<int, kColumnCount> kVerticalEdgeOffsets{{1, 0, -1, -2, -13}};
    constexpr double kMaximumThinWallEdgeDrift = 8.0;
    constexpr double kMaximumIorOnePairDisplacement = 8.0;
    constexpr double kMinimumHighIorPairGain = 8.0;

    require(image.width == 1920 && image.height == 1080,
            "TransmissionThinwallTestGrid conformance requires its calibrated 1920x1080 capture");

    std::array<double, kColumnCount> pair_displacements{};
    std::array<double, kColumnCount> thinwall_edge_drifts{};
    std::array<double, kColumnCount> vertical_pair_displacements{};
    std::array<double, kColumnCount> horizontal_pair_displacements{};
    for (std::size_t column = 0; column < kColumnCount; ++column) {
        const EdgePositionStats thinwall_vertical = vertical_checker_edge(
            image, kColumnCenters[column], kThinWallCenterY, kVerticalEdgeOffsets[column], true);
        const EdgePositionStats volume_vertical = vertical_checker_edge(
            image, kColumnCenters[column], kVolumeCenterY, kVerticalEdgeOffsets[column], true);
        const EdgePositionStats thinwall_horizontal = horizontal_checker_edge(
            image, kColumnCenters[column], kThinWallCenterY, kHorizontalEdgeOffset, true);
        const EdgePositionStats volume_horizontal = horizontal_checker_edge(
            image, kColumnCenters[column], kVolumeCenterY, kHorizontalEdgeOffset, true);
        require(thinwall_vertical.strength > 0.005 && volume_vertical.strength > 0.005 &&
                    thinwall_horizontal.strength > 0.005 && volume_horizontal.strength > 0.005,
                "TransmissionThinwallTestGrid capture should retain each checker edge");

        const double thinwall_vertical_drift =
            std::abs(thinwall_vertical.position - static_cast<double>(kVerticalEdgeOffsets[column]));
        const double thinwall_horizontal_drift =
            std::abs(thinwall_horizontal.position - static_cast<double>(kHorizontalEdgeOffset));
        thinwall_edge_drifts[column] =
            std::max(thinwall_vertical_drift, thinwall_horizontal_drift);
        vertical_pair_displacements[column] =
            volume_vertical.position - thinwall_vertical.position;
        horizontal_pair_displacements[column] =
            volume_horizontal.position - thinwall_horizontal.position;
        pair_displacements[column] =
            std::hypot(vertical_pair_displacements[column], horizontal_pair_displacements[column]);
        std::printf("  column[%zu] thinwall=(v=%.0f,h=%.0f) volume=(v=%.0f,h=%.0f)\n", column,
                    thinwall_vertical.position, thinwall_horizontal.position,
                    volume_vertical.position, volume_horizontal.position);
    }

    std::printf("material_conformance: gltf-transmission-thinwall-test\n");
    std::printf("  paired checker edge displacement (pixels)=");
    for (double displacement : pair_displacements) {
        std::printf(" %.2f", displacement);
    }
    std::printf("\n  vertical displacement=");
    for (double displacement : vertical_pair_displacements) {
        std::printf(" %.2f", displacement);
    }
    std::printf("\n  horizontal displacement=");
    for (double displacement : horizontal_pair_displacements) {
        std::printf(" %.2f", displacement);
    }
    std::printf("\n  thin-wall edge drift (pixels)=");
    for (double drift : thinwall_edge_drifts) {
        std::printf(" %.2f", drift);
    }
    std::printf("\n");

    // IOR=1 is the intentional paired control: adding a one-metre volume with no refractive
    // index change should not move the checker image. The thin-wall row remains geometry-stable
    // across IOR, while the 2.42 volume row must introduce a material, not merely tonal,
    // difference from its thin-wall counterpart.
    require(*std::max_element(thinwall_edge_drifts.begin(), thinwall_edge_drifts.end()) <=
                kMaximumThinWallEdgeDrift,
            "TransmissionThinwallTestGrid thin-wall row should retain checker geometry across IOR");
    require(pair_displacements[0] <= kMaximumIorOnePairDisplacement,
            "TransmissionThinwallTestGrid IOR=1 volume pair should retain the thin-wall geometry");
    const double high_ior_mean_displacement =
        (pair_displacements[kColumnCount - 2U] + pair_displacements[kColumnCount - 1U]) / 2.0;
    require(high_ior_mean_displacement >= pair_displacements[0] +
                                             (kMinimumHighIorPairGain / 2.0) &&
                pair_displacements.back() >=
                    pair_displacements[0] + kMinimumHighIorPairGain,
            "TransmissionThinwallTestGrid high-IOR volume should depart from thin-wall checker geometry");
}

void check_gltf_transmission_order_test(const LoadedImage& image) {
    // TransmissionOrderTest deliberately places the same alpha symbol behind,
    // intersecting, and in front of transmissive gems. The top row is BLEND,
    // followed by MASK and OPAQUE controls. This is a real-time semantic
    // witness: the front symbol remains visible after transmission, while the
    // left and center BLEND symbols must participate in the radiance sampled
    // through the gems instead of disappearing from it.
    constexpr std::array<float, 3> kColumnCenters{{0.2625F, 0.4875F, 0.7125F}};
    constexpr std::array<float, 3> kRowCenters{{0.25F, 0.50F, 0.75F}};
    constexpr float kHalfExtent = 0.040F;

    require(image.width == 1920 && image.height == 1080,
            "TransmissionOrderTest conformance requires its calibrated 1920x1080 capture");

    std::array<std::array<ContrastStats, 3>, 3> cells{};
    for (std::size_t row = 0; row < cells.size(); ++row) {
        for (std::size_t column = 0; column < cells[row].size(); ++column) {
            cells[row][column] = normalized_square_contrast_region(
                image, kColumnCenters[column], kRowCenters[row], kHalfExtent, true);
            require(cells[row][column].pixel_count >= 5'000U,
                    "TransmissionOrderTest capture should retain every authored gem ROI");
        }
    }

    std::printf("material_conformance: gltf-transmission-order-test\n");
    for (std::size_t row = 0; row < cells.size(); ++row) {
        std::printf("  row[%zu] center_luma=", row);
        for (const ContrastStats& cell : cells[row]) {
            std::printf(" %.4f", cell.mean_luma);
        }
        std::printf(" contrast=");
        for (const ContrastStats& cell : cells[row]) {
            std::printf(" %.4f", cell.standard_deviation);
        }
        std::printf("\n");
    }
    // Each probe sits on an authored alpha stroke rather than the blue gem's
    // center. The fixed static-environment camera makes its linear red-minus-
    // blue value a compact witness for the orange symbol reaching the sampled
    // radiance source.
    constexpr std::array<float, 3> kSourceProbeX{{0.2175F, 0.4650F, 0.7125F}};
    std::array<std::array<CircleStats, 3>, 3> source_probes{};
    for (std::size_t row = 0; row < kRowCenters.size(); ++row) {
        std::printf("  row[%zu] source probes red_minus_blue=", row);
        for (std::size_t column = 0; column < kSourceProbeX.size(); ++column) {
            source_probes[row][column] = normalized_circle_region(image, kSourceProbeX[column],
                                                                  kRowCenters[row], 0.012F, true);
            std::printf(" %.4f", source_probes[row][column].color.red -
                                     source_probes[row][column].color.blue);
        }
        std::printf("\n");
    }

    constexpr double kMinimumBlendSourceWarmth = -0.050;
    constexpr double kMinimumControlWarmth = -0.055;
    constexpr double kMinimumForegroundWarmthGain = 0.020;
    require(source_probes[0][0].color.red - source_probes[0][0].color.blue >=
                    kMinimumBlendSourceWarmth &&
                source_probes[0][1].color.red - source_probes[0][1].color.blue >=
                    kMinimumBlendSourceWarmth,
            "TransmissionOrderTest BLEND symbols behind and within gems should remain in "
            "refraction radiance");
    require(
        source_probes[1][0].color.red - source_probes[1][0].color.blue >= kMinimumControlWarmth &&
            source_probes[1][1].color.red - source_probes[1][1].color.blue >=
                kMinimumControlWarmth &&
            source_probes[2][0].color.red - source_probes[2][0].color.blue >=
                kMinimumControlWarmth &&
            source_probes[2][1].color.red - source_probes[2][1].color.blue >= kMinimumControlWarmth,
        "TransmissionOrderTest MASK and OPAQUE behind and within controls should remain visible");
    require(source_probes[0][2].color.red - source_probes[0][2].color.blue >=
                (source_probes[2][2].color.red - source_probes[2][2].color.blue) +
                    kMinimumForegroundWarmthGain,
            "TransmissionOrderTest foreground BLEND symbol should remain composited after "
            "transmission");
}

void check_material_conformance(const std::filesystem::path& path, std::string_view case_name) {
    LoadedImage image = load_rgba_image(path);
    try {
        if (case_name == "furnace-ior") {
            check_furnace_ior(image);
        } else if (case_name == "furnace-specular") {
            check_furnace_specular(image);
        } else if (case_name == "furnace-clearcoat") {
            check_furnace_clearcoat(image);
        } else if (case_name == "furnace-anisotropy") {
            check_furnace_anisotropy(image);
        } else if (case_name == "furnace-iridescence") {
            check_furnace_iridescence(image);
        } else if (case_name == "furnace-sheen") {
            check_furnace_sheen(image);
        } else if (case_name == "furnace-transmission") {
            check_furnace_transmission(image);
        } else if (case_name == "gltf-specular-test") {
            check_gltf_specular_test(image);
        } else if (case_name == "gltf-attenuation-test") {
            check_gltf_attenuation_test(image);
        } else if (case_name == "gltf-transmission-roughness-test") {
            check_gltf_transmission_roughness_test(image);
        } else if (case_name == "gltf-dispersion-test") {
            check_gltf_dispersion_test(image);
        } else if (case_name == "gltf-transmission-thinwall-test") {
            check_gltf_transmission_thinwall_test(image);
        } else if (case_name == "gltf-transmission-order-test") {
            check_gltf_transmission_order_test(image);
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
