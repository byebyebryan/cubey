#pragma once

#include <cubey/procedural/field_2d.h>
#include <cubey/procedural/noise.h>

#include <cstdint>

namespace cubey::procedural {

struct FieldDomain2D {
    float x_scale = 1.0F;
    float y_scale = 1.0F;
    float x_offset = 0.0F;
    float y_offset = 0.0F;
};

struct NoiseSource2DWarp {
    bool enabled = false;
    std::uint64_t seed_offset = 7001U;
    CoherentDomainWarpConfig coherent{};
};

enum class NoiseSource2DBackend {
    LegacyFbm,
    LegacyRidgedFbm,
    CoherentNoise,
};

enum class NoiseSource2DOutput {
    Signed,
    Unit,
};

struct NoiseSource2D {
    NoiseSource2DBackend backend = NoiseSource2DBackend::LegacyFbm;
    NoiseSource2DOutput output = NoiseSource2DOutput::Signed;
    std::uint64_t seed = 0;
    Fbm2DConfig legacy_fbm{};
    CoherentNoiseConfig coherent{};
    FieldDomain2D domain{};
    NoiseSource2DWarp warp{};
};

[[nodiscard]] float sample_noise_source_2d(float x, float y, const NoiseSource2D& config);
[[nodiscard]] ScalarField2D sample_noise_source_field_2d(Grid2DDesc desc,
                                                         const NoiseSource2D& config);

} // namespace cubey::procedural
