#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>

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

} // namespace

int main(int argc, char** argv) {
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
