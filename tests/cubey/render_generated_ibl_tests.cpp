#include <cubey/render/generated_ibl.h>
#include <cubey/render/texture.h>

#include <vulkan/vulkan.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_throws(auto&& action, const char* message) {
    try {
        action();
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error(message);
}

float read_float(std::span<const std::uint8_t> bytes, std::size_t float_index) {
    float value = 0.0F;
    std::memcpy(&value, bytes.data() + (float_index * sizeof(float)), sizeof(float));
    return value;
}

} // namespace

void test_generated_pbr_environment_data_is_deterministic_and_sized() {
    const cubey::render::GeneratedPbrEnvironmentConfig config{
        .irradiance_extent = 2,
        .prefiltered_extent = 4,
        .prefiltered_mip_levels = 3,
        .brdf_lut_extent = 4,
        .intensity = 0.75F,
    };

    const cubey::render::GeneratedPbrEnvironmentData data =
        cubey::render::generate_pbr_environment_data(config);
    const cubey::render::GeneratedPbrEnvironmentData repeated =
        cubey::render::generate_pbr_environment_data(config);

    require(data.irradiance_cube_rgba32f.size() ==
                cubey::render::texture_cube_byte_size(
                    config.irradiance_extent, 1,
                    cubey::render::texture_format_byte_size(VK_FORMAT_R32G32B32A32_SFLOAT)),
            "generated irradiance cube should match expected byte size");
    require(data.prefiltered_cube_rgba32f.size() ==
                cubey::render::texture_cube_byte_size(
                    config.prefiltered_extent, config.prefiltered_mip_levels,
                    cubey::render::texture_format_byte_size(VK_FORMAT_R32G32B32A32_SFLOAT)),
            "generated prefiltered cube should match expected byte size");
    require(data.brdf_lut_rgba32f.size() ==
                static_cast<std::size_t>(config.brdf_lut_extent) *
                    static_cast<std::size_t>(config.brdf_lut_extent) *
                    cubey::render::texture_format_byte_size(VK_FORMAT_R32G32B32A32_SFLOAT),
            "generated BRDF LUT should match expected byte size");
    require(data.irradiance_cube_rgba32f == repeated.irradiance_cube_rgba32f,
            "generated irradiance cube should be deterministic");
    require(data.prefiltered_cube_rgba32f == repeated.prefiltered_cube_rgba32f,
            "generated prefiltered cube should be deterministic");
    require(data.brdf_lut_rgba32f == repeated.brdf_lut_rgba32f,
            "generated BRDF LUT should be deterministic");

    require(std::isfinite(read_float(data.irradiance_cube_rgba32f, 0)),
            "generated irradiance should contain finite floats");
    require(read_float(data.irradiance_cube_rgba32f, 3) == 1.0F,
            "generated irradiance alpha should be one");
    require(std::isfinite(read_float(data.brdf_lut_rgba32f, 0)),
            "generated BRDF LUT should contain finite floats");
    require(read_float(data.brdf_lut_rgba32f, 3) == 1.0F,
            "generated BRDF LUT alpha should be one");
}

void test_generated_pbr_environment_config_rejects_zero_dimensions() {
    cubey::render::GeneratedPbrEnvironmentConfig config;
    config.prefiltered_mip_levels = 0;
    require_throws(
        [&] {
            cubey::render::validate_generated_pbr_environment_config(config);
        },
        "generated IBL config should reject zero mip counts");
}
