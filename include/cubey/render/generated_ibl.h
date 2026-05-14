#pragma once

#include <cubey/core/math.h>
#include <cubey/render/texture.h>

#include <cstdint>
#include <span>
#include <vector>

namespace cubey::vulkan {
class Device;
class GpuRuntime;
} // namespace cubey::vulkan

namespace cubey::render {

struct GeneratedPbrEnvironmentConfig {
    std::uint32_t irradiance_extent = 32;
    std::uint32_t prefiltered_extent = 64;
    std::uint32_t prefiltered_mip_levels = 5;
    std::uint32_t brdf_lut_extent = 128;
    float intensity = 1.0F;
};

struct GeneratedPbrEnvironmentData {
    std::vector<std::uint8_t> irradiance_cube_rgba32f{};
    std::vector<std::uint8_t> prefiltered_cube_rgba32f{};
    std::vector<std::uint8_t> brdf_lut_rgba32f{};
};

struct GeneratedPbrEnvironment {
    TextureCube irradiance_cube;
    TextureCube prefiltered_cube;
    Texture2D brdf_lut;
    std::uint32_t prefiltered_mip_levels = 1;
    float intensity = 1.0F;
};

struct PbrEquirectangularImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::span<const float> rgba32f{};
};

void validate_generated_pbr_environment_config(const GeneratedPbrEnvironmentConfig& config);
void validate_pbr_equirectangular_image(const PbrEquirectangularImage& image);
[[nodiscard]] math::Vec3
sample_pbr_equirectangular_radiance(const PbrEquirectangularImage& image,
                                    math::Vec3 direction);
[[nodiscard]] GeneratedPbrEnvironmentData
generate_pbr_environment_data(const GeneratedPbrEnvironmentConfig& config = {});
[[nodiscard]] GeneratedPbrEnvironmentData
generate_pbr_environment_data_from_equirectangular(
    const PbrEquirectangularImage& image,
    const GeneratedPbrEnvironmentConfig& config = {});
[[nodiscard]] GeneratedPbrEnvironment
create_generated_pbr_environment(const cubey::vulkan::Device& device,
                                 cubey::vulkan::GpuRuntime& gpu,
                                 const GeneratedPbrEnvironmentConfig& config = {});
[[nodiscard]] GeneratedPbrEnvironment
create_pbr_environment_from_equirectangular(
    const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
    const PbrEquirectangularImage& image,
    const GeneratedPbrEnvironmentConfig& config = {});

} // namespace cubey::render
