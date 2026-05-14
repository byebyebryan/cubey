#include <cubey/asset/hdr_image.h>
#include <cubey/core/file_io.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_close(float value, float expected, const char* message) {
    constexpr float kTolerance = 0.0001F;
    if (value < expected - kTolerance || value > expected + kTolerance) {
        throw std::runtime_error(message);
    }
}

std::filesystem::path test_dir(const char* name) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

void append_text(std::vector<std::uint8_t>& bytes, const char* text) {
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        bytes.push_back(static_cast<std::uint8_t>(*cursor));
    }
}

void append_rgbe(std::vector<std::uint8_t>& bytes, std::uint8_t r, std::uint8_t g,
                 std::uint8_t b, std::uint8_t e) {
    bytes.push_back(r);
    bytes.push_back(g);
    bytes.push_back(b);
    bytes.push_back(e);
}

std::filesystem::path write_test_hdr(const std::filesystem::path& dir) {
    std::vector<std::uint8_t> bytes;
    append_text(bytes, "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 2\n");
    append_rgbe(bytes, 128, 64, 32, 129);
    append_rgbe(bytes, 64, 128, 32, 129);
    append_rgbe(bytes, 32, 64, 128, 129);
    append_rgbe(bytes, 16, 32, 64, 129);

    const std::filesystem::path path = dir / "tiny.hdr";
    cubey::write_binary_file(path, bytes);
    return path;
}

} // namespace

void test_hdr_image_loads_radiance_rgba32f_pixels() {
    const std::filesystem::path dir = test_dir("cubey_hdr_image_load");
    const std::filesystem::path path = write_test_hdr(dir);

    const cubey::asset::HdrImage image = cubey::asset::load_hdr_image(path);

    require(image.source_path == path, "HDR image should preserve its source path");
    require(image.width == 2, "HDR image should preserve width");
    require(image.height == 2, "HDR image should preserve height");
    require(image.rgba32f.size() == 16, "HDR image should decode to RGBA32F");
    require_close(image.rgba32f[0], 1.0F, "first red channel should decode from RGBE");
    require_close(image.rgba32f[1], 0.5F, "first green channel should decode from RGBE");
    require_close(image.rgba32f[2], 0.25F, "first blue channel should decode from RGBE");
    require_close(image.rgba32f[3], 1.0F, "decoded alpha should be one");
    require_close(image.rgba32f[8], 0.25F, "third red channel should decode from RGBE");
    require_close(image.rgba32f[9], 0.5F, "third green channel should decode from RGBE");
    require_close(image.rgba32f[10], 1.0F, "third blue channel should decode from RGBE");
    require(std::isfinite(image.rgba32f[15]), "HDR image should contain finite values");

    std::filesystem::remove_all(dir);
}

void test_hdr_image_rejects_non_hdr_input() {
    const std::filesystem::path dir = test_dir("cubey_hdr_image_reject");
    const std::filesystem::path path = dir / "not-hdr.txt";
    const std::string text = "not a Radiance HDR file";
    cubey::write_binary_file(
        path,
        std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(text.data()),
                                      text.size()});

    bool threw = false;
    try {
        static_cast<void>(cubey::asset::load_hdr_image(path));
    } catch (const std::runtime_error& error) {
        threw = std::string(error.what()).find("HDR image") != std::string::npos;
    }
    require(threw, "HDR loader should reject non-HDR inputs with a clear error");

    std::filesystem::remove_all(dir);
}
