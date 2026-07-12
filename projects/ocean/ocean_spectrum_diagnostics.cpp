#include "ocean_spectrum_diagnostics.h"

#include <algorithm>
#include <cmath>

namespace cubey::projects::ocean {
namespace {

constexpr float kGravity = 9.81F;

[[nodiscard]] float dispersion_frequency_squared(float wave_number, float depth_m) {
    return kGravity * wave_number * std::tanh(wave_number * depth_m);
}

} // namespace

float ocean_jonswap_alpha(float wind_speed, float fetch_length_m) {
    return 0.076F *
           std::pow((wind_speed * wind_speed) /
                        std::max(fetch_length_m * kGravity, 0.001F),
                    0.22F);
}

float ocean_jonswap_peak_frequency(float wind_speed, float fetch_length_m) {
    return 22.0F *
           std::pow((kGravity * kGravity) /
                        std::max(wind_speed * fetch_length_m, 0.001F),
                    1.0F / 3.0F);
}

float ocean_finite_depth_wavelength(float angular_frequency, float depth_m) {
    const float frequency_squared = angular_frequency * angular_frequency;
    const float depth = std::max(depth_m, 0.001F);
    float low = 0.0F;
    float high = std::max(frequency_squared / kGravity, 0.001F);
    while (dispersion_frequency_squared(high, depth) < frequency_squared) {
        high *= 2.0F;
    }
    for (std::uint32_t iteration = 0; iteration < 64U; ++iteration) {
        const float middle = (low + high) * 0.5F;
        if (dispersion_frequency_squared(middle, depth) < frequency_squared) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return 2.0F * kOceanPi / std::max((low + high) * 0.5F, 0.000001F);
}

OceanSpectrumDiagnostics ocean_spectrum_diagnostics(const OceanConfig& config) {
    OceanSpectrumDiagnostics diagnostics{};
    for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
        OceanCascadeSpectrumDiagnostics& result = diagnostics.cascades[cascade];
        result.enabled = ocean_cascade_enabled(config, cascade);

        const OceanCascadeConfig& cascade_config = ocean_cascade(config, cascade);
        const std::uint32_t map_size = ocean_cascade_map_size(config, cascade);
        const float fetch_m = cascade_config.fetch_length_km * 1000.0F;
        result.peak_angular_frequency =
            ocean_jonswap_peak_frequency(cascade_config.wind_speed, fetch_m);
        result.peak_wavelength_m =
            ocean_finite_depth_wavelength(result.peak_angular_frequency, config.depth);
        result.safe_min_wavelength_m =
            kOceanCascadeSmallestWaveMultiplier * cascade_config.tile_length /
            std::max(static_cast<float>(map_size), 1.0F);
        result.max_wavelength_m = cascade_config.tile_length;

        const OceanCascadeDomain domain = ocean_cascade_domain(config, cascade);
        result.domain_active = config.spectral_domains_enabled && domain.active;
        if (result.domain_active) {
            result.safe_min_wavelength_m =
                std::max(result.safe_min_wavelength_m, domain.low_wavelength);
            result.max_wavelength_m = std::min(result.max_wavelength_m, domain.high_wavelength);
        }
        result.peak_supported = result.enabled &&
                                result.peak_wavelength_m >= result.safe_min_wavelength_m &&
                                result.peak_wavelength_m <= result.max_wavelength_m;
    }

    for (std::uint32_t lhs = 0; lhs < kOceanCascadeCount; ++lhs) {
        const OceanCascadeSpectrumDiagnostics& a = diagnostics.cascades[lhs];
        if (!a.enabled) {
            continue;
        }
        for (std::uint32_t rhs = lhs + 1U; rhs < kOceanCascadeCount; ++rhs) {
            const OceanCascadeSpectrumDiagnostics& b = diagnostics.cascades[rhs];
            if (!b.enabled) {
                continue;
            }
            const float overlap_min = std::max(a.safe_min_wavelength_m,
                                               b.safe_min_wavelength_m);
            const float overlap_max = std::min(a.max_wavelength_m, b.max_wavelength_m);
            if (overlap_max <= overlap_min) {
                continue;
            }
            ++diagnostics.overlapping_pair_count;
            diagnostics.max_overlap_octaves =
                std::max(diagnostics.max_overlap_octaves,
                         std::log2(overlap_max / overlap_min));
        }
    }
    return diagnostics;
}

} // namespace cubey::projects::ocean
