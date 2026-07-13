#pragma once

#include <cstdint>

namespace cubey::procedural {

enum class CoherentNoiseType {
    OpenSimplex2,
    OpenSimplex2S,
    Cellular,
    Perlin,
    ValueCubic,
    Value,
};

enum class CoherentFractalType {
    None,
    Fbm,
    Ridged,
    PingPong,
};

enum class CoherentCellularDistance {
    Euclidean,
    EuclideanSquared,
    Manhattan,
    Hybrid,
};

enum class CoherentCellularReturn {
    CellValue,
    Distance,
    Distance2,
    Distance2Add,
    Distance2Sub,
    Distance2Mul,
    Distance2Div,
};

enum class CoherentDomainWarpType {
    OpenSimplex2,
    OpenSimplex2Reduced,
    BasicGrid,
};

enum class CoherentDomainWarpFractalType {
    None,
    Progressive,
    Independent,
};

struct CoherentNoiseConfig {
    std::int32_t seed = 1337;
    float frequency = 0.01F;
    CoherentNoiseType noise_type = CoherentNoiseType::OpenSimplex2;
    CoherentFractalType fractal_type = CoherentFractalType::None;
    std::uint32_t octaves = 3;
    float lacunarity = 2.0F;
    float gain = 0.5F;
    float weighted_strength = 0.0F;
    float ping_pong_strength = 2.0F;
    CoherentCellularDistance cellular_distance = CoherentCellularDistance::EuclideanSquared;
    CoherentCellularReturn cellular_return = CoherentCellularReturn::Distance;
    float cellular_jitter = 1.0F;
};

struct CoherentDomainWarpConfig {
    std::int32_t seed = 1337;
    float frequency = 0.01F;
    CoherentDomainWarpType warp_type = CoherentDomainWarpType::OpenSimplex2;
    CoherentDomainWarpFractalType fractal_type = CoherentDomainWarpFractalType::None;
    std::uint32_t octaves = 3;
    float lacunarity = 2.0F;
    float gain = 0.5F;
    float weighted_strength = 0.0F;
    float ping_pong_strength = 2.0F;
    float amplitude = 1.0F;
};

struct CoherentWarp2D {
    float x = 0.0F;
    float y = 0.0F;
};

struct CoherentWarp3D {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

[[nodiscard]] float sample_coherent_noise_2d(float x, float y,
                                             const CoherentNoiseConfig& config = {});
[[nodiscard]] float sample_coherent_noise_3d(float x, float y, float z,
                                             const CoherentNoiseConfig& config = {});
[[nodiscard]] CoherentWarp2D domain_warp_2d(float x, float y,
                                            const CoherentDomainWarpConfig& config = {});
[[nodiscard]] CoherentWarp3D domain_warp_3d(float x, float y, float z,
                                            const CoherentDomainWarpConfig& config = {});

// Legacy deterministic value-noise backend retained for stable captures/tests.
struct Fbm2DConfig {
    std::uint32_t octaves = 4;
    float lacunarity = 2.03F;
    float gain = 0.52F;
    float initial_amplitude = 0.5F;
    std::uint32_t seed_stride = 1009U;
};

struct Fbm3DConfig {
    std::uint32_t octaves = 4;
    float lacunarity = 2.03F;
    float gain = 0.5F;
    float initial_amplitude = 0.5F;
    std::uint32_t seed_stride = 1013U;
};

[[nodiscard]] std::uint32_t hash_u32(std::uint32_t value);
[[nodiscard]] std::uint32_t hash_u32(std::int32_t x, std::int32_t y, std::uint64_t seed);
[[nodiscard]] std::uint32_t hash_u32(std::int32_t x, std::int32_t y, std::int32_t z,
                                     std::uint32_t seed);
[[nodiscard]] float hash_to_unit(std::uint32_t value);
[[nodiscard]] float hash_to_unit_masked_24(std::uint32_t value);
[[nodiscard]] float random01(std::uint64_t seed, std::uint32_t index, std::uint32_t channel);
[[nodiscard]] float value_noise_2d(float x, float y, std::uint64_t seed);
[[nodiscard]] float fbm_2d(float x, float y, std::uint64_t seed, const Fbm2DConfig& config = {});
[[nodiscard]] float ridged_fbm_2d(float x, float y, std::uint64_t seed,
                                  const Fbm2DConfig& config = {});
[[nodiscard]] float value_noise_3d(float x, float y, float z, std::uint32_t seed);
[[nodiscard]] float gradient_noise_3d(float x, float y, float z, std::uint32_t seed);
[[nodiscard]] float fbm_3d(float x, float y, float z, std::uint32_t seed,
                           const Fbm3DConfig& config = {});
[[nodiscard]] float ridged_fbm_3d(float x, float y, float z, std::uint32_t seed,
                                  const Fbm3DConfig& config = {});

} // namespace cubey::procedural
