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

float read_lut_channel(std::span<const std::uint8_t> bytes, std::uint32_t extent,
                       std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
    const std::size_t texel = (static_cast<std::size_t>(y) * static_cast<std::size_t>(extent)) +
                              static_cast<std::size_t>(x);
    return read_float(bytes, (texel * 4U) + channel);
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
            "generated DFG LUT should match expected byte size");
    require(data.irradiance_cube_rgba32f == repeated.irradiance_cube_rgba32f,
            "generated irradiance cube should be deterministic");
    require(data.prefiltered_cube_rgba32f == repeated.prefiltered_cube_rgba32f,
            "generated prefiltered cube should be deterministic");
    require(data.brdf_lut_rgba32f == repeated.brdf_lut_rgba32f,
            "generated DFG LUT should be deterministic");

    require(std::isfinite(read_float(data.irradiance_cube_rgba32f, 0)),
            "generated irradiance should contain finite floats");
    require(read_float(data.irradiance_cube_rgba32f, 3) == 1.0F,
            "generated irradiance alpha should be one");
    require(std::isfinite(read_float(data.brdf_lut_rgba32f, 0)),
            "generated DFG LUT should contain finite floats");
    require(read_float(data.brdf_lut_rgba32f, 3) == 1.0F,
            "generated DFG LUT alpha should be one");
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

void test_generated_pbr_dfg_lut_stores_energy_compensation_term() {
    const cubey::render::GeneratedPbrEnvironmentConfig config{
        .irradiance_extent = 1,
        .prefiltered_extent = 1,
        .prefiltered_mip_levels = 1,
        .brdf_lut_extent = 16,
        .intensity = 1.0F,
    };
    const cubey::render::GeneratedPbrEnvironmentData data =
        cubey::render::generate_pbr_environment_data(config);

    const std::uint32_t high_roughness = config.brdf_lut_extent - 1U;
    const std::uint32_t grazing_to_mid_view = config.brdf_lut_extent / 4U;
    const float dfg_scale = read_lut_channel(data.brdf_lut_rgba32f, config.brdf_lut_extent,
                                             grazing_to_mid_view, high_roughness, 0);
    const float dfg_bias = read_lut_channel(data.brdf_lut_rgba32f, config.brdf_lut_extent,
                                            grazing_to_mid_view, high_roughness, 1);
    const float white_conductor_energy =
        read_lut_channel(data.brdf_lut_rgba32f, config.brdf_lut_extent,
                         grazing_to_mid_view, high_roughness, 2);
    const float alpha =
        read_lut_channel(data.brdf_lut_rgba32f, config.brdf_lut_extent, grazing_to_mid_view,
                         high_roughness, 3);

    require(std::isfinite(dfg_scale) && std::isfinite(dfg_bias) &&
                std::isfinite(white_conductor_energy),
            "DFG LUT should contain finite energy terms");
    require(dfg_scale >= 0.0F && dfg_scale <= 1.0F, "DFG scale should stay normalized");
    require(dfg_bias >= 0.0F && dfg_bias <= 1.0F, "DFG bias should stay normalized");
    require(alpha == 1.0F, "DFG LUT alpha should remain opaque");

    const float single_scatter_energy = dfg_scale + dfg_bias;
    require(single_scatter_energy > 0.05F,
            "rough white conductor should retain some single-scatter energy");
    require(single_scatter_energy < 0.95F,
            "rough white conductor should expose single-scatter energy loss");
    require(std::fabs(white_conductor_energy - single_scatter_energy) < 0.0001F,
            "DFG blue channel should store white-conductor single-scatter energy");
    require(std::fabs((single_scatter_energy / white_conductor_energy) - 1.0F) < 0.0001F,
            "stored DFG energy should support exact white-conductor compensation");
}
