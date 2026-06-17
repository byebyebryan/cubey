#pragma once

#include <cstdint>

namespace cubey::procedural {

struct Fbm2DConfig {
    std::uint32_t octaves = 4;
    float lacunarity = 2.03F;
    float gain = 0.52F;
    float initial_amplitude = 0.5F;
    std::uint32_t seed_stride = 1009U;
};

[[nodiscard]] std::uint32_t hash_u32(std::int32_t x, std::int32_t y, std::uint64_t seed);
[[nodiscard]] float random01(std::uint64_t seed, std::uint32_t index, std::uint32_t channel);
[[nodiscard]] float value_noise_2d(float x, float y, std::uint64_t seed);
[[nodiscard]] float fbm_2d(float x, float y, std::uint64_t seed, const Fbm2DConfig& config = {});
[[nodiscard]] float ridged_fbm_2d(float x, float y, std::uint64_t seed,
                                  const Fbm2DConfig& config = {});

} // namespace cubey::procedural
