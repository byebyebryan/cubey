#include <cubey/render/generated_ibl.h>

#include <cubey/core/math.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <span>
#include <stdexcept>
#include <utility>

namespace cubey::render {
namespace {

constexpr std::uint32_t kCubeFaceCount = 6;
constexpr VkFormat kIblTextureFormat = VK_FORMAT_R32G32B32A32_SFLOAT;

void append_rgba32f(std::vector<std::uint8_t>& bytes, std::array<float, 4> rgba) {
    const std::size_t offset = bytes.size();
    bytes.resize(offset + sizeof(float) * rgba.size());
    std::memcpy(bytes.data() + offset, rgba.data(), sizeof(float) * rgba.size());
}

[[nodiscard]] math::Vec3 cube_direction(std::uint32_t face, float u, float v) {
    switch (face) {
    case 0:
        return glm::normalize(math::Vec3{1.0F, -v, -u});
    case 1:
        return glm::normalize(math::Vec3{-1.0F, -v, u});
    case 2:
        return glm::normalize(math::Vec3{u, 1.0F, v});
    case 3:
        return glm::normalize(math::Vec3{u, -1.0F, -v});
    case 4:
        return glm::normalize(math::Vec3{u, -v, 1.0F});
    case 5:
        return glm::normalize(math::Vec3{-u, -v, -1.0F});
    default:
        throw std::runtime_error("cube face index is out of range");
    }
}

[[nodiscard]] math::Vec3 generated_radiance(math::Vec3 direction) {
    const float sky = std::clamp(direction.y * 0.5F + 0.5F, 0.0F, 1.0F);
    const math::Vec3 ground{0.028F, 0.026F, 0.023F};
    const math::Vec3 horizon{0.18F, 0.20F, 0.22F};
    const math::Vec3 zenith{0.48F, 0.58F, 0.72F};
    math::Vec3 color = glm::mix(glm::mix(ground, horizon, sky), zenith, sky * sky);

    const math::Vec3 key_direction = glm::normalize(math::Vec3{-0.35F, 0.42F, 0.84F});
    const math::Vec3 fill_direction = glm::normalize(math::Vec3{0.82F, 0.18F, -0.32F});
    const float key = std::pow(std::max(glm::dot(direction, key_direction), 0.0F), 48.0F);
    const float fill = std::pow(std::max(glm::dot(direction, fill_direction), 0.0F), 10.0F);
    color += math::Vec3{3.8F, 3.35F, 2.7F} * key;
    color += math::Vec3{0.18F, 0.25F, 0.34F} * fill;
    return color;
}

[[nodiscard]] math::Vec3 generated_irradiance(math::Vec3 direction) {
    return glm::mix(math::Vec3{0.035F, 0.038F, 0.042F}, generated_radiance(direction), 0.34F);
}

[[nodiscard]] math::Vec3 generated_prefiltered(math::Vec3 direction, float roughness) {
    const math::Vec3 radiance = generated_radiance(direction);
    const math::Vec3 average{0.20F, 0.22F, 0.25F};
    return glm::mix(radiance, average, std::clamp(roughness * 0.82F, 0.0F, 1.0F));
}

void append_cube(std::vector<std::uint8_t>& bytes, std::uint32_t extent,
                 std::uint32_t mip_levels, auto&& sample) {
    for (std::uint32_t mip = 0; mip < mip_levels; ++mip) {
        const std::uint32_t mip_extent = texture_cube_mip_extent(extent, mip);
        for (std::uint32_t face = 0; face < kCubeFaceCount; ++face) {
            for (std::uint32_t y = 0; y < mip_extent; ++y) {
                for (std::uint32_t x = 0; x < mip_extent; ++x) {
                    const float u = ((static_cast<float>(x) + 0.5F) /
                                     static_cast<float>(mip_extent)) *
                                        2.0F -
                                    1.0F;
                    const float v = ((static_cast<float>(y) + 0.5F) /
                                     static_cast<float>(mip_extent)) *
                                        2.0F -
                                    1.0F;
                    const math::Vec3 color = sample(cube_direction(face, u, v), mip);
                    append_rgba32f(bytes, {color.x, color.y, color.z, 1.0F});
                }
            }
        }
    }
}

void append_brdf_lut(std::vector<std::uint8_t>& bytes, std::uint32_t extent) {
    for (std::uint32_t y = 0; y < extent; ++y) {
        const float roughness =
            (static_cast<float>(y) + 0.5F) / static_cast<float>(extent);
        for (std::uint32_t x = 0; x < extent; ++x) {
            const float ndotv =
                (static_cast<float>(x) + 0.5F) / static_cast<float>(extent);
            const float fresnel = std::pow(1.0F - ndotv, 5.0F);
            const float scale = 1.0F - (roughness * 0.55F);
            const float bias = fresnel * (1.0F - roughness) * 0.08F;
            append_rgba32f(bytes, {scale, bias, 0.0F, 1.0F});
        }
    }
}

[[nodiscard]] cubey::vulkan::SamplerConfig ibl_sampler(std::uint32_t mip_levels) {
    return {
        .min_filter = VK_FILTER_LINEAR,
        .mag_filter = VK_FILTER_LINEAR,
        .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .max_lod = static_cast<float>(mip_levels > 0 ? mip_levels - 1U : 0U),
    };
}

} // namespace

void validate_generated_pbr_environment_config(const GeneratedPbrEnvironmentConfig& config) {
    if (config.irradiance_extent == 0 || config.prefiltered_extent == 0 ||
        config.prefiltered_mip_levels == 0 || config.brdf_lut_extent == 0) {
        throw std::runtime_error("generated PBR environment dimensions must be nonzero");
    }
}

GeneratedPbrEnvironmentData
generate_pbr_environment_data(const GeneratedPbrEnvironmentConfig& config) {
    validate_generated_pbr_environment_config(config);
    GeneratedPbrEnvironmentData data;
    data.irradiance_cube_rgba32f.reserve(texture_cube_byte_size(
        config.irradiance_extent, 1, texture_format_byte_size(kIblTextureFormat)));
    append_cube(data.irradiance_cube_rgba32f, config.irradiance_extent, 1,
                [](math::Vec3 direction, std::uint32_t) {
        return generated_irradiance(direction);
    });

    data.prefiltered_cube_rgba32f.reserve(texture_cube_byte_size(
        config.prefiltered_extent, config.prefiltered_mip_levels,
        texture_format_byte_size(kIblTextureFormat)));
    append_cube(data.prefiltered_cube_rgba32f, config.prefiltered_extent,
                config.prefiltered_mip_levels,
                [&config](math::Vec3 direction, std::uint32_t mip) {
        const float roughness = config.prefiltered_mip_levels == 1
                                    ? 0.0F
                                    : static_cast<float>(mip) /
                                          static_cast<float>(config.prefiltered_mip_levels - 1U);
        return generated_prefiltered(direction, roughness);
    });

    const std::size_t brdf_bytes = static_cast<std::size_t>(config.brdf_lut_extent) *
                                   static_cast<std::size_t>(config.brdf_lut_extent) *
                                   texture_format_byte_size(kIblTextureFormat);
    data.brdf_lut_rgba32f.reserve(brdf_bytes);
    append_brdf_lut(data.brdf_lut_rgba32f, config.brdf_lut_extent);
    return data;
}

GeneratedPbrEnvironment create_generated_pbr_environment(
    const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
    const GeneratedPbrEnvironmentConfig& config) {
    const GeneratedPbrEnvironmentData data = generate_pbr_environment_data(config);
    TextureCube irradiance = create_uploaded_texture_cube(
        device, gpu,
        {
            .extent = config.irradiance_extent,
            .mip_levels = 1,
            .format = kIblTextureFormat,
            .bytes = std::span<const std::uint8_t>{data.irradiance_cube_rgba32f.data(),
                                                   data.irradiance_cube_rgba32f.size()},
            .create_sampler = true,
            .sampler = ibl_sampler(1),
        });
    TextureCube prefiltered = create_uploaded_texture_cube(
        device, gpu,
        {
            .extent = config.prefiltered_extent,
            .mip_levels = config.prefiltered_mip_levels,
            .format = kIblTextureFormat,
            .bytes = std::span<const std::uint8_t>{data.prefiltered_cube_rgba32f.data(),
                                                   data.prefiltered_cube_rgba32f.size()},
            .create_sampler = true,
            .sampler = ibl_sampler(config.prefiltered_mip_levels),
        });
    Texture2D brdf_lut = create_uploaded_texture_2d(
        device, gpu,
        {
            .extent = {config.brdf_lut_extent, config.brdf_lut_extent},
            .format = kIblTextureFormat,
            .bytes =
                std::span<const std::uint8_t>{data.brdf_lut_rgba32f.data(),
                                              data.brdf_lut_rgba32f.size()},
            .create_sampler = true,
            .sampler = ibl_sampler(1),
        });
    return {
        .irradiance_cube = std::move(irradiance),
        .prefiltered_cube = std::move(prefiltered),
        .brdf_lut = std::move(brdf_lut),
        .prefiltered_mip_levels = config.prefiltered_mip_levels,
        .intensity = config.intensity,
    };
}

} // namespace cubey::render
