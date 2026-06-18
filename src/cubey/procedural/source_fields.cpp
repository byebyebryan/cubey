#include <cubey/procedural/source_fields.h>

#include <cubey/procedural/operators.h>

#include <cstdint>

namespace cubey::procedural {
namespace {

[[nodiscard]] std::int32_t coherent_seed(std::uint64_t seed) {
    return static_cast<std::int32_t>(seed & 0x7fff'ffffULL);
}

[[nodiscard]] float to_output_range(float value, NoiseSource2DBackend backend,
                                    NoiseSource2DOutput output) {
    if (output == NoiseSource2DOutput::Signed || backend == NoiseSource2DBackend::LegacyRidgedFbm) {
        return value;
    }
    return (value * 0.5F) + 0.5F;
}

} // namespace

float sample_noise_source_2d(float x, float y, const NoiseSource2D& config) {
    float sx = (x * config.domain.x_scale) + config.domain.x_offset;
    float sy = (y * config.domain.y_scale) + config.domain.y_offset;

    if (config.warp.enabled) {
        CoherentDomainWarpConfig warp = config.warp.coherent;
        warp.seed = coherent_seed(config.seed + config.warp.seed_offset);
        const CoherentWarp2D warped = domain_warp_2d(sx, sy, warp);
        sx = warped.x;
        sy = warped.y;
    }

    float value = 0.0F;
    switch (config.backend) {
    case NoiseSource2DBackend::LegacyFbm:
        value = fbm_2d(sx, sy, config.seed, config.legacy_fbm);
        break;
    case NoiseSource2DBackend::LegacyRidgedFbm:
        value = ridged_fbm_2d(sx, sy, config.seed, config.legacy_fbm);
        break;
    case NoiseSource2DBackend::CoherentNoise: {
        CoherentNoiseConfig coherent = config.coherent;
        coherent.seed = coherent_seed(config.seed);
        value = sample_coherent_noise_2d(sx, sy, coherent);
        break;
    }
    }

    return to_output_range(value, config.backend, config.output);
}

ScalarField2D sample_noise_source_field_2d(Grid2DDesc desc, const NoiseSource2D& config) {
    ScalarField2D result(desc, 0.0F);
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            result.at(x, y) =
                sample_noise_source_2d(grid_sample_x(desc, x), grid_sample_y(desc, y), config);
        }
    }
    return result;
}

} // namespace cubey::procedural
