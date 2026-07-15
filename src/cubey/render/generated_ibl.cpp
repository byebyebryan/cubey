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
constexpr std::uint32_t kDfgSampleCount = 512;
constexpr std::uint32_t kIrradianceSampleCount = 64;
constexpr std::uint32_t kPrefilterSampleCount = 128;
constexpr VkFormat kIblTextureFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
constexpr float kPi = 3.14159265359F;
constexpr float kDfgEnergyEpsilon = 0.0001F;

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

[[nodiscard]] std::size_t
checked_equirectangular_value_count(const PbrEquirectangularImage& image) {
    if (image.width == 0 || image.height == 0) {
        throw std::runtime_error("PBR equirectangular image dimensions must be nonzero");
    }
    const std::size_t pixel_count =
        static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
    if (pixel_count > std::numeric_limits<std::size_t>::max() / 4U) {
        throw std::runtime_error("PBR equirectangular image is too large");
    }
    return pixel_count * 4U;
}

[[nodiscard]] math::Vec3 equirectangular_texel(const PbrEquirectangularImage& image,
                                               std::uint32_t x, std::uint32_t y) {
    const std::size_t offset =
        ((static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width)) +
         static_cast<std::size_t>(x)) *
        4U;
    return {
        image.rgba32f[offset + 0U],
        image.rgba32f[offset + 1U],
        image.rgba32f[offset + 2U],
    };
}

[[nodiscard]] math::Vec3
sample_pbr_equirectangular_radiance_unchecked(const PbrEquirectangularImage& image,
                                              math::Vec3 direction) {
    const math::Vec3 normal = glm::normalize(direction);
    float u = (std::atan2(normal.x, normal.z) / (2.0F * kPi)) + 0.5F;
    u -= std::floor(u);
    const float v = std::acos(std::clamp(normal.y, -1.0F, 1.0F)) / kPi;

    const float sample_x = (u * static_cast<float>(image.width)) - 0.5F;
    const float sample_y = (v * static_cast<float>(image.height)) - 0.5F;
    const float floor_x = std::floor(sample_x);
    const float floor_y = std::floor(sample_y);
    const std::uint32_t width = image.width;
    const std::uint32_t height = image.height;
    const std::uint32_t x0 = static_cast<std::uint32_t>(static_cast<std::int64_t>(floor_x) %
                                                            static_cast<std::int64_t>(width) +
                                                        static_cast<std::int64_t>(width)) %
                             width;
    const std::uint32_t x1 = (x0 + 1U) % width;
    const std::uint32_t y0 =
        static_cast<std::uint32_t>(std::clamp(floor_y, 0.0F, static_cast<float>(height - 1U)));
    const std::uint32_t y1 = std::min(y0 + 1U, height - 1U);
    const float tx = sample_x - floor_x;
    const float ty = std::clamp(sample_y - floor_y, 0.0F, 1.0F);

    const math::Vec3 c00 = equirectangular_texel(image, x0, y0);
    const math::Vec3 c10 = equirectangular_texel(image, x1, y0);
    const math::Vec3 c01 = equirectangular_texel(image, x0, y1);
    const math::Vec3 c11 = equirectangular_texel(image, x1, y1);
    return glm::mix(glm::mix(c00, c10, tx), glm::mix(c01, c11, tx), ty);
}

[[nodiscard]] float radical_inverse_vdc(std::uint32_t bits) {
    bits = (bits << 16U) | (bits >> 16U);
    bits = ((bits & 0x55555555U) << 1U) | ((bits & 0xAAAAAAAAU) >> 1U);
    bits = ((bits & 0x33333333U) << 2U) | ((bits & 0xCCCCCCCCU) >> 2U);
    bits = ((bits & 0x0F0F0F0FU) << 4U) | ((bits & 0xF0F0F0F0U) >> 4U);
    bits = ((bits & 0x00FF00FFU) << 8U) | ((bits & 0xFF00FF00U) >> 8U);
    return static_cast<float>(bits) * 2.3283064365386963e-10F;
}

[[nodiscard]] std::array<float, 2> hammersley(std::uint32_t index, std::uint32_t count) {
    return {
        static_cast<float>(index) / static_cast<float>(count),
        radical_inverse_vdc(index),
    };
}

[[nodiscard]] math::Vec3 importance_sample_ggx(std::array<float, 2> xi, float roughness) {
    const float alpha = roughness * roughness;
    const float alpha2 = alpha * alpha;
    const float phi = 2.0F * kPi * xi[0];
    const float cos_theta = std::sqrt((1.0F - xi[1]) / (1.0F + ((alpha2 - 1.0F) * xi[1])));
    const float sin_theta = std::sqrt(std::max(1.0F - (cos_theta * cos_theta), 0.0F));
    return {
        std::cos(phi) * sin_theta,
        std::sin(phi) * sin_theta,
        cos_theta,
    };
}

[[nodiscard]] float visibility_smith_ggx_correlated(float ndotv, float ndotl, float roughness) {
    const float alpha = roughness * roughness;
    const float alpha2 = alpha * alpha;
    const float lambda_v =
        ndotl * std::sqrt(std::max(((ndotv - (alpha2 * ndotv)) * ndotv) + alpha2, 0.0F));
    const float lambda_l =
        ndotv * std::sqrt(std::max(((ndotl - (alpha2 * ndotl)) * ndotl) + alpha2, 0.0F));
    return 0.5F / std::max(lambda_v + lambda_l, 0.00001F);
}

[[nodiscard]] math::Vec3 tangent_to_world(math::Vec3 sample, math::Vec3 normal) {
    const math::Vec3 up =
        std::fabs(normal.z) < 0.999F ? math::Vec3{0.0F, 0.0F, 1.0F} : math::Vec3{0.0F, 1.0F, 0.0F};
    const math::Vec3 tangent = glm::normalize(glm::cross(up, normal));
    const math::Vec3 bitangent = glm::cross(normal, tangent);
    return glm::normalize((tangent * sample.x) + (bitangent * sample.y) + (normal * sample.z));
}

[[nodiscard]] math::Vec3 generated_prefiltered(math::Vec3 direction, float roughness) {
    const math::Vec3 normal = glm::normalize(direction);
    if (roughness <= 0.0001F) {
        return generated_radiance(normal);
    }

    const math::Vec3 view = normal;
    math::Vec3 color{0.0F, 0.0F, 0.0F};
    float total_weight = 0.0F;
    for (std::uint32_t sample = 0; sample < kPrefilterSampleCount; ++sample) {
        const math::Vec3 half_tangent =
            importance_sample_ggx(hammersley(sample, kPrefilterSampleCount), roughness);
        const math::Vec3 half_vector = tangent_to_world(half_tangent, normal);
        const math::Vec3 light =
            glm::normalize((2.0F * glm::dot(view, half_vector) * half_vector) - view);
        const float ndotl = std::max(glm::dot(normal, light), 0.0F);
        if (ndotl > 0.0F) {
            color += generated_radiance(light) * ndotl;
            total_weight += ndotl;
        }
    }

    if (total_weight <= 0.0F) {
        return generated_radiance(normal);
    }
    return color / total_weight;
}

[[nodiscard]] math::Vec3 sample_cosine_hemisphere(std::array<float, 2> xi) {
    const float phi = 2.0F * kPi * xi[0];
    const float cos_theta = std::sqrt(std::max(1.0F - xi[1], 0.0F));
    const float sin_theta = std::sqrt(std::max(xi[1], 0.0F));
    return {
        std::cos(phi) * sin_theta,
        std::sin(phi) * sin_theta,
        cos_theta,
    };
}

[[nodiscard]] math::Vec3 equirectangular_irradiance(const PbrEquirectangularImage& image,
                                                    math::Vec3 direction) {
    const math::Vec3 normal = glm::normalize(direction);
    math::Vec3 color{0.0F, 0.0F, 0.0F};
    for (std::uint32_t sample = 0; sample < kIrradianceSampleCount; ++sample) {
        const math::Vec3 hemisphere =
            sample_cosine_hemisphere(hammersley(sample, kIrradianceSampleCount));
        color += sample_pbr_equirectangular_radiance_unchecked(
            image, tangent_to_world(hemisphere, normal));
    }
    return color / static_cast<float>(kIrradianceSampleCount);
}

[[nodiscard]] math::Vec3 equirectangular_prefiltered(const PbrEquirectangularImage& image,
                                                     math::Vec3 direction, float roughness) {
    const math::Vec3 normal = glm::normalize(direction);
    if (roughness <= 0.0001F) {
        return sample_pbr_equirectangular_radiance_unchecked(image, normal);
    }

    const math::Vec3 view = normal;
    math::Vec3 color{0.0F, 0.0F, 0.0F};
    float total_weight = 0.0F;
    for (std::uint32_t sample = 0; sample < kPrefilterSampleCount; ++sample) {
        const math::Vec3 half_tangent =
            importance_sample_ggx(hammersley(sample, kPrefilterSampleCount), roughness);
        const math::Vec3 half_vector = tangent_to_world(half_tangent, normal);
        const math::Vec3 light =
            glm::normalize((2.0F * glm::dot(view, half_vector) * half_vector) - view);
        const float ndotl = std::max(glm::dot(normal, light), 0.0F);
        if (ndotl > 0.0F) {
            color += sample_pbr_equirectangular_radiance_unchecked(image, light) * ndotl;
            total_weight += ndotl;
        }
    }

    if (total_weight <= 0.0F) {
        return sample_pbr_equirectangular_radiance_unchecked(image, normal);
    }
    return color / total_weight;
}

void append_cube(std::vector<std::uint8_t>& bytes, std::uint32_t extent, std::uint32_t mip_levels,
                 auto&& sample) {
    for (std::uint32_t mip = 0; mip < mip_levels; ++mip) {
        const std::uint32_t mip_extent = texture_cube_mip_extent(extent, mip);
        for (std::uint32_t face = 0; face < kCubeFaceCount; ++face) {
            for (std::uint32_t y = 0; y < mip_extent; ++y) {
                for (std::uint32_t x = 0; x < mip_extent; ++x) {
                    const float u =
                        ((static_cast<float>(x) + 0.5F) / static_cast<float>(mip_extent)) * 2.0F -
                        1.0F;
                    const float v =
                        ((static_cast<float>(y) + 0.5F) / static_cast<float>(mip_extent)) * 2.0F -
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
        const float roughness = (static_cast<float>(y) + 0.5F) / static_cast<float>(extent);
        for (std::uint32_t x = 0; x < extent; ++x) {
            const float ndotv = (static_cast<float>(x) + 0.5F) / static_cast<float>(extent);
            const math::Vec3 view{
                std::sqrt(std::max(1.0F - (ndotv * ndotv), 0.0F)),
                0.0F,
                ndotv,
            };
            float scale = 0.0F;
            float bias = 0.0F;
            for (std::uint32_t sample = 0; sample < kDfgSampleCount; ++sample) {
                const math::Vec3 half_vector =
                    importance_sample_ggx(hammersley(sample, kDfgSampleCount), roughness);
                const math::Vec3 light =
                    glm::normalize((2.0F * glm::dot(view, half_vector) * half_vector) - view);
                const float ndotl = std::max(light.z, 0.0F);
                const float ndoth = std::max(half_vector.z, 0.0F);
                const float vdoth = std::max(glm::dot(view, half_vector), 0.0F);
                if (ndotl > 0.0F && ndoth > 0.0F && vdoth > 0.0F) {
                    const float visibility =
                        visibility_smith_ggx_correlated(ndotv, ndotl, roughness);
                    const float geometry_visibility = (4.0F * visibility * ndotl * vdoth) / ndoth;
                    const float fresnel = std::pow(1.0F - vdoth, 5.0F);
                    scale += (1.0F - fresnel) * geometry_visibility;
                    bias += fresnel * geometry_visibility;
                }
            }
            scale /= static_cast<float>(kDfgSampleCount);
            bias /= static_cast<float>(kDfgSampleCount);
            const float white_conductor_energy = std::max(scale + bias, kDfgEnergyEpsilon);
            append_rgba32f(bytes, {scale, bias, white_conductor_energy, 1.0F});
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

[[nodiscard]] GeneratedPbrEnvironment create_pbr_environment_from_data(
    const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
    const GeneratedPbrEnvironmentConfig& config, const GeneratedPbrEnvironmentData& data) {
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
            .bytes = std::span<const std::uint8_t>{data.brdf_lut_rgba32f.data(),
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

} // namespace

void validate_generated_pbr_environment_config(const GeneratedPbrEnvironmentConfig& config) {
    if (config.irradiance_extent == 0 || config.prefiltered_extent == 0 ||
        config.prefiltered_mip_levels == 0 || config.brdf_lut_extent == 0) {
        throw std::runtime_error("generated PBR environment dimensions must be nonzero");
    }
}

void validate_pbr_environment_texture_bindings(const PbrEnvironmentTextureBindings& bindings) {
    if (bindings.irradiance_sampler == VK_NULL_HANDLE ||
        bindings.irradiance_view == VK_NULL_HANDLE) {
        throw std::runtime_error("PBR environment irradiance cube binding is not initialized");
    }
    if (bindings.prefiltered_sampler == VK_NULL_HANDLE ||
        bindings.prefiltered_view == VK_NULL_HANDLE) {
        throw std::runtime_error("PBR environment prefiltered cube binding is not initialized");
    }
    if (bindings.previous_prefiltered_sampler == VK_NULL_HANDLE ||
        bindings.previous_prefiltered_view == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "PBR environment previous prefiltered cube binding is not initialized");
    }
    if (bindings.brdf_lut_sampler == VK_NULL_HANDLE || bindings.brdf_lut_view == VK_NULL_HANDLE) {
        throw std::runtime_error("PBR environment BRDF LUT binding is not initialized");
    }
    if (bindings.prefiltered_mip_levels == 0) {
        throw std::runtime_error("PBR environment prefiltered mip count must be nonzero");
    }
    if (!std::isfinite(bindings.prefiltered_blend) || bindings.prefiltered_blend < 0.0F ||
        bindings.prefiltered_blend > 1.0F) {
        throw std::runtime_error("PBR environment prefiltered blend must be within [0, 1]");
    }
}

void validate_pbr_equirectangular_image(const PbrEquirectangularImage& image) {
    const std::size_t value_count = checked_equirectangular_value_count(image);
    if (image.rgba32f.size() != value_count) {
        throw std::runtime_error("PBR equirectangular image must contain RGBA32F pixels");
    }
}

PbrEnvironmentTextureBindings
pbr_environment_texture_bindings(const GeneratedPbrEnvironment& environment) {
    return {
        .irradiance_sampler = environment.irradiance_cube.sampler().handle(),
        .irradiance_view = environment.irradiance_cube.view(),
        .prefiltered_sampler = environment.prefiltered_cube.sampler().handle(),
        .prefiltered_view = environment.prefiltered_cube.view(),
        .previous_prefiltered_sampler = environment.prefiltered_cube.sampler().handle(),
        .previous_prefiltered_view = environment.prefiltered_cube.view(),
        .brdf_lut_sampler = environment.brdf_lut.sampler().handle(),
        .brdf_lut_view = environment.brdf_lut.view(),
        .prefiltered_mip_levels = environment.prefiltered_mip_levels,
        .intensity = environment.intensity,
    };
}

math::Vec3 sample_pbr_equirectangular_radiance(const PbrEquirectangularImage& image,
                                               math::Vec3 direction) {
    validate_pbr_equirectangular_image(image);
    if (glm::dot(direction, direction) <= 0.0F) {
        throw std::runtime_error("PBR equirectangular sample direction must be nonzero");
    }
    return sample_pbr_equirectangular_radiance_unchecked(image, direction);
}

GeneratedPbrEnvironmentData
generate_pbr_environment_data(const GeneratedPbrEnvironmentConfig& config) {
    validate_generated_pbr_environment_config(config);
    GeneratedPbrEnvironmentData data;
    data.irradiance_cube_rgba32f.reserve(texture_cube_byte_size(
        config.irradiance_extent, 1, texture_format_byte_size(kIblTextureFormat)));
    append_cube(
        data.irradiance_cube_rgba32f, config.irradiance_extent, 1,
        [](math::Vec3 direction, std::uint32_t) { return generated_irradiance(direction); });

    data.prefiltered_cube_rgba32f.reserve(
        texture_cube_byte_size(config.prefiltered_extent, config.prefiltered_mip_levels,
                               texture_format_byte_size(kIblTextureFormat)));
    append_cube(data.prefiltered_cube_rgba32f, config.prefiltered_extent,
                config.prefiltered_mip_levels, [&config](math::Vec3 direction, std::uint32_t mip) {
                    const float roughness =
                        config.prefiltered_mip_levels == 1
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

GeneratedPbrEnvironmentData
generate_pbr_environment_data_from_equirectangular(const PbrEquirectangularImage& image,
                                                   const GeneratedPbrEnvironmentConfig& config) {
    validate_generated_pbr_environment_config(config);
    validate_pbr_equirectangular_image(image);
    GeneratedPbrEnvironmentData data;
    data.irradiance_cube_rgba32f.reserve(texture_cube_byte_size(
        config.irradiance_extent, 1, texture_format_byte_size(kIblTextureFormat)));
    append_cube(data.irradiance_cube_rgba32f, config.irradiance_extent, 1,
                [&image](math::Vec3 direction, std::uint32_t) {
                    return equirectangular_irradiance(image, direction);
                });

    data.prefiltered_cube_rgba32f.reserve(
        texture_cube_byte_size(config.prefiltered_extent, config.prefiltered_mip_levels,
                               texture_format_byte_size(kIblTextureFormat)));
    append_cube(data.prefiltered_cube_rgba32f, config.prefiltered_extent,
                config.prefiltered_mip_levels,
                [&image, &config](math::Vec3 direction, std::uint32_t mip) {
                    const float roughness =
                        config.prefiltered_mip_levels == 1
                            ? 0.0F
                            : static_cast<float>(mip) /
                                  static_cast<float>(config.prefiltered_mip_levels - 1U);
                    return equirectangular_prefiltered(image, direction, roughness);
                });

    const std::size_t brdf_bytes = static_cast<std::size_t>(config.brdf_lut_extent) *
                                   static_cast<std::size_t>(config.brdf_lut_extent) *
                                   texture_format_byte_size(kIblTextureFormat);
    data.brdf_lut_rgba32f.reserve(brdf_bytes);
    append_brdf_lut(data.brdf_lut_rgba32f, config.brdf_lut_extent);
    return data;
}

GeneratedPbrEnvironment
create_generated_pbr_environment(const cubey::vulkan::Device& device,
                                 cubey::vulkan::GpuRuntime& gpu,
                                 const GeneratedPbrEnvironmentConfig& config) {
    const GeneratedPbrEnvironmentData data = generate_pbr_environment_data(config);
    return create_pbr_environment_from_data(device, gpu, config, data);
}

GeneratedPbrEnvironment create_pbr_environment_from_equirectangular(
    const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
    const PbrEquirectangularImage& image, const GeneratedPbrEnvironmentConfig& config) {
    const GeneratedPbrEnvironmentData data =
        generate_pbr_environment_data_from_equirectangular(image, config);
    return create_pbr_environment_from_data(device, gpu, config, data);
}

} // namespace cubey::render
