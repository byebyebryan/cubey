#pragma once

#include "ocean_config.h"

#include <array>
#include <cstdint>

namespace cubey::projects::ocean {

struct OceanCascadeSpectrumDiagnostics {
    bool enabled = false;
    bool domain_active = false;
    bool peak_supported = false;
    float peak_angular_frequency = 0.0F;
    float peak_wavelength_m = 0.0F;
    float safe_min_wavelength_m = 0.0F;
    float max_wavelength_m = 0.0F;
};

struct OceanSpectrumDiagnostics {
    std::array<OceanCascadeSpectrumDiagnostics, kOceanCascadeCount> cascades{};
    std::uint32_t overlapping_pair_count = 0U;
    float max_overlap_octaves = 0.0F;
};

[[nodiscard]] float ocean_jonswap_alpha(float wind_speed, float fetch_length_m);
[[nodiscard]] float ocean_jonswap_peak_frequency(float wind_speed, float fetch_length_m);
[[nodiscard]] float ocean_finite_depth_wavelength(float angular_frequency, float depth_m);
[[nodiscard]] OceanSpectrumDiagnostics ocean_spectrum_diagnostics(const OceanConfig& config);

} // namespace cubey::projects::ocean
