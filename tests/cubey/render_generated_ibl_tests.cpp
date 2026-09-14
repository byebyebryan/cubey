#include <cubey/render/generated_ibl.h>
#include <cubey/render/texture.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

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

float read_lut_channel(std::span<const std::uint8_t> bytes, std::uint32_t extent, std::uint32_t x,
                       std::uint32_t y, std::uint32_t channel) {
    const std::size_t texel = (static_cast<std::size_t>(y) * static_cast<std::size_t>(extent)) +
                              static_cast<std::size_t>(x);
    return read_float(bytes, (texel * 4U) + channel);
}

float read_cube_channel(std::span<const std::uint8_t> bytes, std::uint32_t extent,
                        std::uint32_t mip, std::uint32_t face, std::uint32_t x, std::uint32_t y,
                        std::uint32_t channel) {
    std::size_t texel_offset = 0;
    for (std::uint32_t prior_mip = 0; prior_mip < mip; ++prior_mip) {
        const std::uint32_t prior_extent =
            cubey::render::texture_cube_mip_extent(extent, prior_mip);
        texel_offset += static_cast<std::size_t>(6U) * prior_extent * prior_extent;
    }

    const std::uint32_t mip_extent = cubey::render::texture_cube_mip_extent(extent, mip);
    texel_offset += static_cast<std::size_t>(face) * mip_extent * mip_extent;
    texel_offset += static_cast<std::size_t>(y) * mip_extent;
    texel_offset += x;
    return read_float(bytes, (texel_offset * 4U) + channel);
}

struct TestVec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

TestVec3 operator+(TestVec3 lhs, TestVec3 rhs) {
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

TestVec3 operator*(TestVec3 value, float scalar) {
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

float dot(TestVec3 lhs, TestVec3 rhs) {
    return (lhs.x * rhs.x) + (lhs.y * rhs.y) + (lhs.z * rhs.z);
}

TestVec3 normalize(TestVec3 value) {
    const float length = std::sqrt(dot(value, value));
    return {value.x / length, value.y / length, value.z / length};
}

TestVec3 mix(TestVec3 lhs, TestVec3 rhs, float t) {
    return (lhs * (1.0F - t)) + (rhs * t);
}

TestVec3 generated_radiance_reference(TestVec3 direction) {
    const float sky = std::clamp(direction.y * 0.5F + 0.5F, 0.0F, 1.0F);
    const TestVec3 ground{0.028F, 0.026F, 0.023F};
    const TestVec3 horizon{0.18F, 0.20F, 0.22F};
    const TestVec3 zenith{0.48F, 0.58F, 0.72F};
    TestVec3 color = mix(mix(ground, horizon, sky), zenith, sky * sky);

    const TestVec3 key_direction = normalize(TestVec3{-0.35F, 0.42F, 0.84F});
    const TestVec3 fill_direction = normalize(TestVec3{0.82F, 0.18F, -0.32F});
    const float key = std::pow(std::max(dot(direction, key_direction), 0.0F), 48.0F);
    const float fill = std::pow(std::max(dot(direction, fill_direction), 0.0F), 10.0F);
    color = color + (TestVec3{3.8F, 3.35F, 2.7F} * key);
    color = color + (TestVec3{0.18F, 0.25F, 0.34F} * fill);
    return color;
}

TestVec3 cube_direction_reference(std::uint32_t face, float u, float v) {
    switch (face) {
    case 0:
        return normalize(TestVec3{1.0F, -v, -u});
    default:
        throw std::runtime_error("test only covers positive X cube face");
    }
}

TestVec3 legacy_prefiltered_reference(TestVec3 direction, float roughness) {
    const TestVec3 average{0.20F, 0.22F, 0.25F};
    return mix(generated_radiance_reference(direction), average,
               std::clamp(roughness * 0.82F, 0.0F, 1.0F));
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
    const float sheen_directional_albedo = read_float(data.brdf_lut_rgba32f, 3);
    require(std::isfinite(sheen_directional_albedo) && sheen_directional_albedo >= 0.0F &&
                sheen_directional_albedo <= 1.0F,
            "generated DFG LUT alpha should contain bounded sheen directional albedo");
}

void test_generated_pbr_environment_config_rejects_zero_dimensions() {
    cubey::render::GeneratedPbrEnvironmentConfig config;
    config.prefiltered_mip_levels = 0;
    require_throws([&] { cubey::render::validate_generated_pbr_environment_config(config); },
                   "generated IBL config should reject zero mip counts");
}

void test_pbr_environment_texture_bindings_validate_required_views() {
    cubey::render::PbrEnvironmentTextureBindings bindings{
        .irradiance_sampler = reinterpret_cast<VkSampler>(0x10),
        .irradiance_view = reinterpret_cast<VkImageView>(0x11),
        .prefiltered_sampler = reinterpret_cast<VkSampler>(0x12),
        .prefiltered_view = reinterpret_cast<VkImageView>(0x13),
        .previous_prefiltered_sampler = reinterpret_cast<VkSampler>(0x12),
        .previous_prefiltered_view = reinterpret_cast<VkImageView>(0x13),
        .brdf_lut_sampler = reinterpret_cast<VkSampler>(0x14),
        .brdf_lut_view = reinterpret_cast<VkImageView>(0x15),
        .prefiltered_mip_levels = 4,
        .intensity = 1.0F,
    };
    cubey::render::validate_pbr_environment_texture_bindings(bindings);

    bindings.prefiltered_view = VK_NULL_HANDLE;
    require_throws([&] { cubey::render::validate_pbr_environment_texture_bindings(bindings); },
                   "PBR environment bindings should reject missing prefiltered cube views");
}

void test_pbr_environment_frame_bindings_convert_creation_bindings() {
    const cubey::render::PbrEnvironmentTextureBindings bindings{
        .irradiance_sampler = reinterpret_cast<VkSampler>(0x10),
        .irradiance_view = reinterpret_cast<VkImageView>(0x11),
        .irradiance_layout = VK_IMAGE_LAYOUT_GENERAL,
        .prefiltered_sampler = reinterpret_cast<VkSampler>(0x12),
        .prefiltered_view = reinterpret_cast<VkImageView>(0x13),
        .prefiltered_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .previous_prefiltered_sampler = reinterpret_cast<VkSampler>(0x14),
        .previous_prefiltered_view = reinterpret_cast<VkImageView>(0x15),
        .previous_prefiltered_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .brdf_lut_sampler = reinterpret_cast<VkSampler>(0x16),
        .brdf_lut_view = reinterpret_cast<VkImageView>(0x17),
        .brdf_lut_layout = VK_IMAGE_LAYOUT_GENERAL,
        .prefiltered_mip_levels = 6,
        .prefiltered_blend = 0.35F,
        .intensity = 1.25F,
    };

    const cubey::render::PbrEnvironmentFrameBindings frame =
        cubey::render::pbr_environment_frame_bindings(bindings);
    require(frame.prefiltered_sampler == bindings.prefiltered_sampler &&
                frame.prefiltered_view == bindings.prefiltered_view &&
                frame.prefiltered_layout == bindings.prefiltered_layout &&
                frame.previous_prefiltered_sampler == bindings.previous_prefiltered_sampler &&
                frame.previous_prefiltered_view == bindings.previous_prefiltered_view &&
                frame.previous_prefiltered_layout == bindings.previous_prefiltered_layout &&
                frame.prefiltered_mip_levels == bindings.prefiltered_mip_levels &&
                frame.prefiltered_blend == bindings.prefiltered_blend &&
                frame.intensity == bindings.intensity,
            "PBR environment frame conversion should preserve mutable frame state");
    cubey::render::validate_pbr_environment_frame_bindings(frame);
}

void test_pbr_environment_frame_bindings_validate_transition_state() {
    cubey::render::PbrEnvironmentFrameBindings frame{
        .prefiltered_sampler = reinterpret_cast<VkSampler>(0x12),
        .prefiltered_view = reinterpret_cast<VkImageView>(0x13),
        .previous_prefiltered_sampler = reinterpret_cast<VkSampler>(0x14),
        .previous_prefiltered_view = reinterpret_cast<VkImageView>(0x15),
        .prefiltered_mip_levels = 4,
        .prefiltered_blend = 0.5F,
        .intensity = 1.0F,
    };
    cubey::render::validate_pbr_environment_frame_bindings(frame);

    frame.prefiltered_sampler = VK_NULL_HANDLE;
    require_throws([&] { cubey::render::validate_pbr_environment_frame_bindings(frame); },
                   "PBR environment frame should reject missing current prefiltered sampler");
    frame.prefiltered_sampler = reinterpret_cast<VkSampler>(0x12);

    frame.prefiltered_view = VK_NULL_HANDLE;
    require_throws([&] { cubey::render::validate_pbr_environment_frame_bindings(frame); },
                   "PBR environment frame should reject missing current prefiltered view");
    frame.prefiltered_view = reinterpret_cast<VkImageView>(0x13);

    frame.previous_prefiltered_sampler = VK_NULL_HANDLE;
    require_throws([&] { cubey::render::validate_pbr_environment_frame_bindings(frame); },
                   "PBR environment frame should reject missing previous prefiltered sampler");
    frame.previous_prefiltered_sampler = reinterpret_cast<VkSampler>(0x14);

    frame.previous_prefiltered_view = VK_NULL_HANDLE;
    require_throws([&] { cubey::render::validate_pbr_environment_frame_bindings(frame); },
                   "PBR environment frame should reject missing previous prefiltered view");
    frame.previous_prefiltered_view = reinterpret_cast<VkImageView>(0x15);

    frame.prefiltered_mip_levels = 0;
    require_throws([&] { cubey::render::validate_pbr_environment_frame_bindings(frame); },
                   "PBR environment frame should reject zero prefiltered mip levels");
    frame.prefiltered_mip_levels = 4;

    frame.prefiltered_blend = -0.01F;
    require_throws([&] { cubey::render::validate_pbr_environment_frame_bindings(frame); },
                   "PBR environment frame should reject blend values below zero");
    frame.prefiltered_blend = 1.01F;
    require_throws([&] { cubey::render::validate_pbr_environment_frame_bindings(frame); },
                   "PBR environment frame should reject blend values above one");
    frame.prefiltered_blend = std::numeric_limits<float>::quiet_NaN();
    require_throws([&] { cubey::render::validate_pbr_environment_frame_bindings(frame); },
                   "PBR environment frame should reject non-finite blend values");
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
    const float white_conductor_energy = read_lut_channel(
        data.brdf_lut_rgba32f, config.brdf_lut_extent, grazing_to_mid_view, high_roughness, 2);
    const float sheen_directional_albedo = read_lut_channel(
        data.brdf_lut_rgba32f, config.brdf_lut_extent, grazing_to_mid_view, high_roughness, 3);

    require(std::isfinite(dfg_scale) && std::isfinite(dfg_bias) &&
                std::isfinite(white_conductor_energy) && std::isfinite(sheen_directional_albedo),
            "DFG LUT should contain finite energy terms");
    require(dfg_scale >= 0.0F && dfg_scale <= 1.0F, "DFG scale should stay normalized");
    require(dfg_bias >= 0.0F && dfg_bias <= 1.0F, "DFG bias should stay normalized");
    require(sheen_directional_albedo >= 0.0F && sheen_directional_albedo <= 1.0F,
            "DFG alpha should store bounded sheen directional albedo");

    const float single_scatter_energy = dfg_scale + dfg_bias;
    require(single_scatter_energy > 0.05F,
            "rough white conductor should retain some single-scatter energy");
    require(single_scatter_energy < 0.95F,
            "rough white conductor should expose single-scatter energy loss");
    require(std::fabs(white_conductor_energy - single_scatter_energy) < 0.0001F,
            "DFG blue channel should store white-conductor single-scatter energy");
    require(std::fabs((single_scatter_energy / white_conductor_energy) - 1.0F) < 0.0001F,
            "stored DFG energy should support exact white-conductor compensation");
    require(std::fabs(dfg_scale - 0.584076881F) < 0.00001F &&
                std::fabs(dfg_bias - 0.010429758F) < 0.00001F &&
                std::fabs(white_conductor_energy - 0.594506621F) < 0.00001F,
            "adding sheen directional albedo should preserve the existing RGB DFG result");
}

void test_generated_pbr_dfg_lut_stores_sheen_directional_albedo() {
    const cubey::render::GeneratedPbrEnvironmentConfig config{
        .irradiance_extent = 1,
        .prefiltered_extent = 1,
        .prefiltered_mip_levels = 1,
        .brdf_lut_extent = 24,
        .intensity = 1.0F,
    };
    const cubey::render::GeneratedPbrEnvironmentData data =
        cubey::render::generate_pbr_environment_data(config);

    float minimum = 1.0F;
    float maximum = 0.0F;
    for (std::uint32_t y = 0; y < config.brdf_lut_extent; ++y) {
        for (std::uint32_t x = 0; x < config.brdf_lut_extent; ++x) {
            const float value =
                read_lut_channel(data.brdf_lut_rgba32f, config.brdf_lut_extent, x, y, 3);
            require(std::isfinite(value), "sheen directional albedo should remain finite");
            require(value >= 0.0F && value <= 1.0F,
                    "sheen directional albedo should remain normalized");
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
    }
    require(maximum - minimum > 0.03F,
            "sheen directional albedo should retain a nonconstant integrated response");

    const std::uint32_t grazing_view = config.brdf_lut_extent / 8U;
    const std::uint32_t face_on_view = config.brdf_lut_extent - 2U;
    const std::uint32_t low_roughness = config.brdf_lut_extent / 5U;
    const std::uint32_t high_roughness = config.brdf_lut_extent - 2U;
    const float grazing_low = read_lut_channel(data.brdf_lut_rgba32f, config.brdf_lut_extent,
                                               grazing_view, low_roughness, 3);
    const float face_on_low = read_lut_channel(data.brdf_lut_rgba32f, config.brdf_lut_extent,
                                               face_on_view, low_roughness, 3);
    const float grazing_high = read_lut_channel(data.brdf_lut_rgba32f, config.brdf_lut_extent,
                                                grazing_view, high_roughness, 3);
    require(std::fabs(grazing_low - face_on_low) > 0.005F,
            "sheen directional albedo should vary with view angle");
    require(std::fabs(grazing_low - grazing_high) > 0.005F,
            "sheen directional albedo should vary with roughness");

    const cubey::render::GeneratedPbrEnvironmentConfig reference_config{
        .irradiance_extent = 1,
        .prefiltered_extent = 1,
        .prefiltered_mip_levels = 1,
        .brdf_lut_extent = 64,
        .intensity = 1.0F,
    };
    const auto reference = cubey::render::generate_pbr_environment_data(reference_config);
    struct ReferenceProbe {
        std::uint32_t x;
        std::uint32_t y;
        float directional_albedo_512;
    };
    // Frozen 512-sample cosine-QMC reference points span near-grazing,
    // mid-view, near-face-on and low/mid/high roughness. The runtime generator
    // uses a documented 128-sample budget and must remain close to this result.
    constexpr std::array<ReferenceProbe, 9> kReferenceProbes{{
        {0U, 3U, 1.0F},
        {31U, 3U, 0.000000170F},
        {63U, 3U, 0.0F},
        {0U, 31U, 0.688764572F},
        {31U, 31U, 0.187346429F},
        {63U, 31U, 0.026549103F},
        {0U, 60U, 0.767466307F},
        {31U, 60U, 0.354817867F},
        {63U, 60U, 0.155324712F},
    }};
    float maximum_error = 0.0F;
    float mean_error = 0.0F;
    for (const ReferenceProbe& probe : kReferenceProbes) {
        const float actual = read_lut_channel(reference.brdf_lut_rgba32f, 64, probe.x, probe.y, 3);
        const float error = std::fabs(actual - probe.directional_albedo_512);
        maximum_error = std::max(maximum_error, error);
        mean_error += error;
    }
    mean_error /= static_cast<float>(kReferenceProbes.size());
    require(maximum_error <= 0.02F,
            "128-sample sheen directional albedo should remain within its 512-sample error bound");
    require(mean_error <= 0.005F,
            "128-sample sheen directional albedo should retain low mean reference error");
}

void test_generated_pbr_prefilter_uses_ggx_convolution_not_legacy_average_mix() {
    const cubey::render::GeneratedPbrEnvironmentConfig config{
        .irradiance_extent = 1,
        .prefiltered_extent = 4,
        .prefiltered_mip_levels = 3,
        .brdf_lut_extent = 1,
        .intensity = 1.0F,
    };
    const cubey::render::GeneratedPbrEnvironmentData data =
        cubey::render::generate_pbr_environment_data(config);

    constexpr std::uint32_t mip = 1;
    constexpr std::uint32_t face = 0;
    constexpr std::uint32_t x = 0;
    constexpr std::uint32_t y = 0;
    const std::uint32_t mip_extent =
        cubey::render::texture_cube_mip_extent(config.prefiltered_extent, mip);
    const float u = ((static_cast<float>(x) + 0.5F) / static_cast<float>(mip_extent)) * 2.0F - 1.0F;
    const float v = ((static_cast<float>(y) + 0.5F) / static_cast<float>(mip_extent)) * 2.0F - 1.0F;
    const float roughness =
        static_cast<float>(mip) / static_cast<float>(config.prefiltered_mip_levels - 1U);
    const TestVec3 legacy =
        legacy_prefiltered_reference(cube_direction_reference(face, u, v), roughness);

    const float actual_r = read_cube_channel(data.prefiltered_cube_rgba32f,
                                             config.prefiltered_extent, mip, face, x, y, 0);
    const float actual_g = read_cube_channel(data.prefiltered_cube_rgba32f,
                                             config.prefiltered_extent, mip, face, x, y, 1);
    const float actual_b = read_cube_channel(data.prefiltered_cube_rgba32f,
                                             config.prefiltered_extent, mip, face, x, y, 2);
    require(std::isfinite(actual_r) && std::isfinite(actual_g) && std::isfinite(actual_b),
            "rough prefiltered mip should contain finite radiance");
    const float sharp_r = read_cube_channel(data.prefiltered_cube_rgba32f,
                                            config.prefiltered_extent, 0, face, x, y, 0);
    const float sharp_g = read_cube_channel(data.prefiltered_cube_rgba32f,
                                            config.prefiltered_extent, 0, face, x, y, 1);
    const float sharp_b = read_cube_channel(data.prefiltered_cube_rgba32f,
                                            config.prefiltered_extent, 0, face, x, y, 2);
    const float sharp_distance = std::fabs(actual_r - sharp_r) + std::fabs(actual_g - sharp_g) +
                                 std::fabs(actual_b - sharp_b);
    require(sharp_distance > 0.001F,
            "rough prefiltered mip should differ from the sharp radiance mip");
    const float legacy_distance = std::fabs(actual_r - legacy.x) + std::fabs(actual_g - legacy.y) +
                                  std::fabs(actual_b - legacy.z);
    require(legacy_distance > 0.001F,
            "rough prefiltered mip should not use the legacy radiance-to-average mix");
}

void test_pbr_equirectangular_sampling_maps_cardinal_directions() {
    std::vector<float> pixels(8U * 2U * 4U, 0.02F);
    const auto set_pixel = [&pixels](std::uint32_t x, std::uint32_t y, TestVec3 color) {
        const std::size_t offset =
            ((static_cast<std::size_t>(y) * 8U) + static_cast<std::size_t>(x)) * 4U;
        pixels[offset + 0U] = color.x;
        pixels[offset + 1U] = color.y;
        pixels[offset + 2U] = color.z;
        pixels[offset + 3U] = 1.0F;
    };
    for (std::uint32_t y = 0; y < 2; ++y) {
        set_pixel(3, y, {1.0F, 0.2F, 0.1F});
        set_pixel(4, y, {1.0F, 0.2F, 0.1F});
        set_pixel(5, y, {0.1F, 1.0F, 0.2F});
        set_pixel(6, y, {0.1F, 1.0F, 0.2F});
    }

    const cubey::render::PbrEquirectangularImage image{
        .width = 8,
        .height = 2,
        .rgba32f = pixels,
    };

    const cubey::math::Vec3 forward = cubey::render::sample_pbr_equirectangular_radiance(
        image, cubey::math::Vec3{0.0F, 0.0F, 1.0F});
    const cubey::math::Vec3 right = cubey::render::sample_pbr_equirectangular_radiance(
        image, cubey::math::Vec3{1.0F, 0.0F, 0.0F});

    require(std::fabs(forward.r - 1.0F) < 0.0001F,
            "equirectangular +Z should sample the center longitude");
    require(std::fabs(forward.g - 0.2F) < 0.0001F,
            "equirectangular +Z should preserve center color");
    require(std::fabs(right.r - 0.1F) < 0.0001F,
            "equirectangular +X should sample the three-quarter longitude");
    require(std::fabs(right.g - 1.0F) < 0.0001F, "equirectangular +X should preserve side color");
}

void test_pbr_environment_data_can_be_generated_from_equirectangular_hdr() {
    std::vector<float> pixels(8U * 2U * 4U, 0.1F);
    const auto set_pixel = [&pixels](std::uint32_t x, std::uint32_t y, TestVec3 color) {
        const std::size_t offset =
            ((static_cast<std::size_t>(y) * 8U) + static_cast<std::size_t>(x)) * 4U;
        pixels[offset + 0U] = color.x;
        pixels[offset + 1U] = color.y;
        pixels[offset + 2U] = color.z;
        pixels[offset + 3U] = 1.0F;
    };
    for (std::uint32_t y = 0; y < 2; ++y) {
        set_pixel(3, y, {0.7F, 0.3F, 0.2F});
        set_pixel(4, y, {0.7F, 0.3F, 0.2F});
    }

    const cubey::render::PbrEquirectangularImage image{
        .width = 8,
        .height = 2,
        .rgba32f = pixels,
    };
    const cubey::render::GeneratedPbrEnvironmentConfig config{
        .irradiance_extent = 1,
        .prefiltered_extent = 1,
        .prefiltered_mip_levels = 1,
        .brdf_lut_extent = 2,
        .intensity = 1.0F,
    };

    const cubey::render::GeneratedPbrEnvironmentData data =
        cubey::render::generate_pbr_environment_data_from_equirectangular(image, config);

    require(data.irradiance_cube_rgba32f.size() ==
                cubey::render::texture_cube_byte_size(
                    config.irradiance_extent, 1,
                    cubey::render::texture_format_byte_size(VK_FORMAT_R32G32B32A32_SFLOAT)),
            "HDR irradiance cube should match expected byte size");
    require(data.prefiltered_cube_rgba32f.size() ==
                cubey::render::texture_cube_byte_size(
                    config.prefiltered_extent, config.prefiltered_mip_levels,
                    cubey::render::texture_format_byte_size(VK_FORMAT_R32G32B32A32_SFLOAT)),
            "HDR prefiltered cube should match expected byte size");
    require(data.brdf_lut_rgba32f.size() ==
                static_cast<std::size_t>(config.brdf_lut_extent) *
                    static_cast<std::size_t>(config.brdf_lut_extent) *
                    cubey::render::texture_format_byte_size(VK_FORMAT_R32G32B32A32_SFLOAT),
            "HDR DFG LUT should match expected byte size");
    require(std::fabs(read_cube_channel(data.prefiltered_cube_rgba32f, config.prefiltered_extent, 0,
                                        4, 0, 0, 0) -
                      0.7F) < 0.0001F,
            "sharp HDR prefiltered +Z texel should sample source radiance");
    require(read_cube_channel(data.prefiltered_cube_rgba32f, config.prefiltered_extent, 0, 4, 0, 0,
                              3) == 1.0F,
            "HDR prefiltered alpha should remain one");
}
