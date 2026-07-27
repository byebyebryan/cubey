#include "planet_surface_product.h"

#include <cubey/core/math.h>
#include <cubey/procedural/noise.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace cubey::projects::planet {
namespace {

constexpr const char* kSurfaceName = "planet.orbital.surface";
constexpr const char* kSurfaceGenerator = "planet-orbital";
constexpr const char* kSurfaceFormulaVersion = "v2-ref-threejs-planets";
constexpr const char* kSurfaceDomain = "direction-cubemap";

[[nodiscard]] float smoothstep(float edge0, float edge1, float value) {
    const float t = glm::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

[[nodiscard]] std::uint8_t pack_unorm(float value) {
    return static_cast<std::uint8_t>(std::lround(glm::clamp(value, 0.0F, 1.0F) * 255.0F));
}

[[nodiscard]] std::uint64_t hash_bytes(std::span<const std::uint8_t> bytes) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

[[nodiscard]] cubey::math::Vec3 cube_direction(std::uint32_t face, float u, float v) {
    cubey::math::Vec3 direction{};
    switch (face) {
    case 0U:
        direction = {1.0F, -v, -u};
        break;
    case 1U:
        direction = {-1.0F, -v, u};
        break;
    case 2U:
        direction = {u, 1.0F, v};
        break;
    case 3U:
        direction = {u, -1.0F, -v};
        break;
    case 4U:
        direction = {u, -v, 1.0F};
        break;
    case 5U:
        direction = {-u, -v, -1.0F};
        break;
    default:
        throw std::invalid_argument("planet cubemap face must be in [0, 5]");
    }
    return glm::normalize(direction);
}

[[nodiscard]] std::array<float, 4> sample_surface_fields(cubey::math::Vec3 direction,
                                                           std::uint32_t seed) {
    const float warp_x = cubey::procedural::fbm_3d(direction.x * 1.15F,
                                                    direction.y * 1.15F,
                                                    direction.z * 1.15F, seed + 17U,
                                                    {.octaves = 3U, .gain = 0.53F});
    const float warp_y = cubey::procedural::fbm_3d(direction.x * 1.15F + 37.0F,
                                                    direction.y * 1.15F - 11.0F,
                                                    direction.z * 1.15F + 19.0F, seed + 31U,
                                                    {.octaves = 3U, .gain = 0.53F});
    const float warp_z = cubey::procedural::fbm_3d(direction.x * 1.15F - 23.0F,
                                                    direction.y * 1.15F + 41.0F,
                                                    direction.z * 1.15F - 7.0F, seed + 47U,
                                                    {.octaves = 3U, .gain = 0.53F});
    const cubey::math::Vec3 warped = glm::normalize(
        direction + cubey::math::Vec3{warp_x, warp_y, warp_z} * 0.14F);

    const float continent = 0.64F * cubey::procedural::fbm_3d(
                                 warped.x * 0.76F, warped.y * 0.76F, warped.z * 0.76F,
                                 seed, {.octaves = 5U, .gain = 0.56F}) +
                             0.36F * cubey::procedural::fbm_3d(
                                 warped.x * 2.1F, warped.y * 2.1F, warped.z * 2.1F, seed + 59U,
                                 {.octaves = 3U, .gain = 0.48F});
    const float land = smoothstep(-0.10F, 0.08F, continent);
    const float ridges = cubey::procedural::ridged_fbm_3d(
        warped.x * 3.2F, warped.y * 3.2F, warped.z * 3.2F, seed + 71U,
        {.octaves = 4U, .gain = 0.53F});
    const float relief = 0.5F + 0.5F * cubey::procedural::fbm_3d(
                                               warped.x * 7.0F, warped.y * 7.0F,
                                               warped.z * 7.0F, seed + 83U,
                                               {.octaves = 4U, .gain = 0.52F});
    const float elevation = land * glm::clamp(0.20F + 0.60F * ridges + 0.20F * relief, 0.0F, 1.0F);
    const float latitude = std::abs(direction.y);
    const float ice = smoothstep(0.78F, 0.94F, latitude + elevation * 0.04F) * land;
    const float roughness = glm::clamp(0.35F + 0.45F * relief + 0.20F * ridges, 0.0F, 1.0F);
    return {elevation, land, ice, roughness};
}

[[nodiscard]] std::vector<std::uint8_t>
generate_surface_payload(const PlanetSurfaceProductConfig& config) {
    const std::size_t texel_count = static_cast<std::size_t>(config.extent) * config.extent * 6U;
    std::vector<std::uint8_t> payload(texel_count * 4U);
    const std::uint32_t seed = static_cast<std::uint32_t>(config.seed);
    for (std::uint32_t face = 0U; face < 6U; ++face) {
        for (std::uint32_t y = 0U; y < config.extent; ++y) {
            const float v = ((static_cast<float>(y) + 0.5F) / static_cast<float>(config.extent)) *
                                2.0F -
                            1.0F;
            for (std::uint32_t x = 0U; x < config.extent; ++x) {
                const float u = ((static_cast<float>(x) + 0.5F) /
                                 static_cast<float>(config.extent)) *
                                    2.0F -
                                1.0F;
                const std::array<float, 4> fields =
                    sample_surface_fields(cube_direction(face, u, v), seed);
                const std::size_t index =
                    ((static_cast<std::size_t>(face) * config.extent + y) * config.extent + x) *
                    4U;
                for (std::size_t channel = 0U; channel < fields.size(); ++channel) {
                    payload[index + channel] = pack_unorm(fields[channel]);
                }
            }
        }
    }
    return payload;
}

} // namespace

cubey::procedural::ProceduralArtifactRecipe
planet_surface_product_recipe(const PlanetSurfaceProductConfig& config) {
    if (config.extent < 2U) {
        throw std::invalid_argument("planet surface extent must be at least 2");
    }
    return {
        .name = kSurfaceName,
        .generator = kSurfaceGenerator,
        .formula_version = kSurfaceFormulaVersion,
        .domain = kSurfaceDomain,
        .seed = config.seed,
        .parameter_hash = static_cast<std::uint64_t>(config.extent),
        .space = cubey::procedural::ProceduralDomainSpace::Spherical,
        .kind = cubey::procedural::ProceduralArtifactKind::TextureCube,
        .format = cubey::procedural::ProceduralArtifactValueFormat::Rgba8Unorm,
        .extent = {.width = config.extent, .height = config.extent, .depth = 1U, .faces = 6U,
                   .mip_levels = 1U},
    };
}

PlanetSurfaceProduct prepare_planet_surface_product(cubey::procedural::ProceduralArtifactCache& cache,
                                                    const PlanetSurfaceProductConfig& config) {
    const cubey::procedural::ProceduralArtifactRecipe recipe = planet_surface_product_recipe(config);
    const cubey::procedural::ProceduralArtifactCacheLoadResult cached = cache.load(recipe);
    if (cached.outcome == cubey::procedural::ProceduralArtifactCacheLoadOutcome::Hit &&
        cached.artifact.has_value() &&
        cubey::procedural::procedural_artifact_payload_size_matches(recipe,
                                                                      cached.artifact->payload.size())) {
        return {.config = config,
                .rgba = cached.artifact->payload,
                .content_hash = cached.artifact->metadata.content_hash,
                .cache_hit = true};
    }

    std::vector<std::uint8_t> payload = generate_surface_payload(config);
    const std::uint64_t content_hash = hash_bytes(payload);
    const cubey::procedural::ProceduralArtifactMetadata metadata =
        cubey::procedural::make_procedural_artifact_metadata(
            cubey::procedural::make_procedural_artifact_identity(
                recipe.name, recipe.generator, recipe.formula_version, recipe.domain, recipe.seed,
                recipe.space),
            recipe.kind, recipe.format, recipe.extent, content_hash);
    static_cast<void>(cache.store(recipe, metadata, payload));
    return {.config = config,
            .rgba = std::move(payload),
            .content_hash = content_hash,
            .cache_hit = false};
}

} // namespace cubey::projects::planet
