#include "fluid_3d_sources.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace cubey::projects::fluid_3d {
namespace {

constexpr float kTau = 6.28318530718F;
constexpr std::array<float, 3> kPlumeCenter{0.5F, 0.10F, 0.5F};
constexpr std::array<float, 3> kExplosionCenter{0.5F, 0.24F, 0.5F};
constexpr float kFireTurbulenceRadius = 0.006F;

[[nodiscard]] std::array<float, 3> add(std::array<float, 3> lhs,
                                       std::array<float, 3> rhs) {
    return {lhs[0] + rhs[0], lhs[1] + rhs[1], lhs[2] + rhs[2]};
}

[[nodiscard]] std::array<float, 3> scale(std::array<float, 3> value, float amount) {
    return {value[0] * amount, value[1] * amount, value[2] * amount};
}

[[nodiscard]] std::array<float, 3> clamp01(std::array<float, 3> value) {
    return {
        std::clamp(value[0], 0.035F, 0.965F),
        std::clamp(value[1], 0.035F, 0.965F),
        std::clamp(value[2], 0.035F, 0.965F),
    };
}

[[nodiscard]] float length(std::array<float, 3> value) {
    return std::sqrt((value[0] * value[0]) + (value[1] * value[1]) + (value[2] * value[2]));
}

[[nodiscard]] std::array<float, 3> normalize_or_zero(std::array<float, 3> value) {
    const float vector_length = length(value);
    if (vector_length <= 0.00001F) {
        return {};
    }
    return {value[0] / vector_length, value[1] / vector_length, value[2] / vector_length};
}

[[nodiscard]] std::array<float, 3> plume_disk_offset(std::uint32_t index,
                                                     std::uint32_t count) {
    if (count <= 1U) {
        return {};
    }
    constexpr float kGoldenAngle = 2.39996323F;
    const float rank = static_cast<float>(index + 1U);
    const float normalized_count = static_cast<float>(count);
    const float radius = 0.075F * std::sqrt(rank / normalized_count);
    const float angle = kGoldenAngle * rank;
    return {std::cos(angle) * radius, 0.0F, std::sin(angle) * radius};
}

[[nodiscard]] Fluid3DSourceState create_plume_source(const Fluid3DConfig& config,
                                                     std::uint32_t index) {
    const std::array<float, 3> offset = plume_disk_offset(index, config.source_count);
    const std::array<float, 3> swirl_direction = normalize_or_zero({-offset[2], 0.0F, offset[0]});
    const float source_index = static_cast<float>(index);
    const std::array<float, 3> velocity =
        normalize_or_zero(add({0.0F, 1.0F, 0.0F},
                              scale(swirl_direction,
                                    0.18F + 0.08F * std::sin(source_index * kTau * 0.37F))));
    return {
        .position = add(kPlumeCenter, offset),
        .velocity = velocity,
        .material_amount = {config.source_smoke_amount, config.source_heat_amount, 0.0F, 0.0F},
        .radius = config.source_radius,
    };
}

[[nodiscard]] std::array<float, 3> explosion_direction(std::uint32_t index, std::uint32_t count) {
    if (count <= 1U) {
        return {0.0F, 1.0F, 0.0F};
    }
    constexpr float kGoldenAngle = 2.39996323F;
    const float source_index = static_cast<float>(index);
    const float normalized_count = static_cast<float>(count);
    const float height = -0.15F + 0.85F * (source_index / (normalized_count - 1.0F));
    const float radius = std::sqrt(std::max(1.0F - height * height, 0.0F));
    const float angle = kGoldenAngle * (source_index + 1.0F);
    return normalize_or_zero({std::cos(angle) * radius, height + 0.45F,
                              std::sin(angle) * radius});
}

[[nodiscard]] Fluid3DSourceState create_explosion_source(const Fluid3DConfig& config,
                                                         std::uint32_t index) {
    const std::array<float, 3> direction = explosion_direction(index, config.source_count);
    return {
        .position = add(kExplosionCenter, scale(direction, 0.020F)),
        .velocity = direction,
        .material_amount =
            {
                config.source_smoke_amount,
                config.source_heat_amount * 1.45F,
                config.source_flame_amount,
                0.0F,
            },
        .radius = config.source_radius * 1.15F,
    };
}

[[nodiscard]] Fluid3DSourceState create_fire_source(const Fluid3DConfig& config,
                                                    std::uint32_t index) {
    const std::array<float, 3> offset = scale(plume_disk_offset(index, config.source_count), 1.05F);
    const std::array<float, 3> swirl_direction = normalize_or_zero({offset[2], 0.0F, -offset[0]});
    const std::array<float, 3> velocity =
        normalize_or_zero(add({0.0F, 1.0F, 0.0F}, scale(swirl_direction, 0.22F)));
    return {
        .position = add(kPlumeCenter, offset),
        .velocity = velocity,
        .material_amount =
            {
                config.source_smoke_amount * 0.020F,
                config.source_heat_amount * 2.40F,
                0.0F,
                config.source_flame_amount * 2.20F,
            },
        .radius = config.source_radius,
    };
}

[[nodiscard]] Fluid3DSourceState create_source(const Fluid3DConfig& config, std::uint32_t index) {
    switch (config.scenario) {
    case Fluid3DScenario::SmokePlume:
        return create_plume_source(config, index);
    case Fluid3DScenario::Explosion:
        return create_explosion_source(config, index);
    case Fluid3DScenario::Fire:
        return create_fire_source(config, index);
    }
    return create_plume_source(config, index);
}

void apply_fire_turbulence(Fluid3DSourceState& source, std::uint32_t index, float time) {
    const float source_index = static_cast<float>(index);
    const float phase = source_index * 2.173F;
    const float drift = time * (1.05F + source_index * 0.11F) + phase;
    const float swell = time * (0.52F + source_index * 0.07F) + phase * 1.9F;
    const float drift_wave = 0.5F + 0.5F * std::sin(drift);
    const float swell_wave = 0.5F + 0.5F * std::sin(swell);
    const std::array<float, 3> position_jitter{
        std::sin(drift) * kFireTurbulenceRadius,
        (0.5F + 0.5F * std::sin(swell + 1.7F)) * kFireTurbulenceRadius * 0.25F,
        std::cos(drift * 0.83F + 0.6F) * kFireTurbulenceRadius,
    };
    source.position = clamp01(add(source.position, position_jitter));
    source.velocity =
        normalize_or_zero(add(source.velocity,
                              {
                                  std::sin(swell * 1.3F) * 0.09F,
                                  std::sin(drift * 0.7F + 0.4F) * 0.025F,
                                  std::cos(swell * 1.1F + 0.8F) * 0.09F,
                              }));
    source.material_amount[1] *= 0.96F + swell_wave * 0.08F;
    source.material_amount[3] *= 0.92F + swell_wave * 0.12F + drift_wave * 0.04F;
    source.radius *= 0.96F + swell_wave * 0.08F;
}

[[nodiscard]] float source_emission_scale(const Fluid3DConfig& config, const FrameTiming& timing) {
    switch (config.scenario) {
    case Fluid3DScenario::SmokePlume:
        return 1.0F;
    case Fluid3DScenario::Explosion: {
        const float time = static_cast<float>(timing.elapsed_seconds);
        const float interval = std::max(config.explosion_interval_seconds, 0.0001F);
        const float duration = std::clamp(config.explosion_duration_seconds, 0.0001F, interval);
        const float phase = std::fmod(std::max(time, 0.0F), interval);
        return phase < duration ? config.explosion_boost : 0.0F;
    }
    case Fluid3DScenario::Fire:
        return 1.0F;
    }
    return 1.0F;
}

void validate_source_count(const Fluid3DConfig& config) {
    if (config.source_count == 0 || config.source_count > kMaxFluid3DSourceCount) {
        throw std::runtime_error("fluid 3D source count must be 1..16");
    }
}

} // namespace

std::vector<Fluid3DSourceState> create_fluid_3d_sources(const Fluid3DConfig& config) {
    validate_source_count(config);
    std::vector<Fluid3DSourceState> sources;
    sources.reserve(config.source_count);
    for (std::uint32_t index = 0; index < config.source_count; ++index) {
        sources.push_back(create_source(config, index));
    }
    return sources;
}

std::vector<Fluid3DSourceGpu>
fluid_3d_sources_to_gpu(const std::vector<Fluid3DSourceState>& sources,
                        const Fluid3DConfig& config, float emission_scale) {
    std::vector<Fluid3DSourceGpu> gpu;
    gpu.reserve(sources.size());
    const float source_scale = std::max(emission_scale, 0.0F);
    for (const Fluid3DSourceState& source : sources) {
        gpu.push_back({
            .position_radius =
                {
                    source.position[0],
                    source.position[1],
                    source.position[2],
                    source.radius,
                },
            .velocity_strength =
                {
                    source.velocity[0],
                    source.velocity[1],
                    source.velocity[2],
                    config.source_velocity_strength * source_scale,
                },
            .material_amount =
                {
                    source.material_amount[0] * source_scale,
                    source.material_amount[1] * source_scale,
                    source.material_amount[2] * source_scale,
                    source.material_amount[3] * source_scale,
                },
        });
    }
    return gpu;
}

std::vector<Fluid3DSourceGpu>
update_fluid_3d_sources(std::vector<Fluid3DSourceState>& sources, const Fluid3DConfig& config,
                        const FrameTiming& timing) {
    if (sources.size() != config.source_count) {
        sources = create_fluid_3d_sources(config);
    }
    for (std::uint32_t index = 0; index < config.source_count; ++index) {
        sources[index] = create_source(config, index);
        if (config.scenario == Fluid3DScenario::Fire) {
            apply_fire_turbulence(sources[index], index, static_cast<float>(timing.elapsed_seconds));
        }
    }
    return fluid_3d_sources_to_gpu(sources, config, source_emission_scale(config, timing));
}

std::size_t fluid_3d_source_byte_size(const Fluid3DConfig& config) {
    validate_source_count(config);
    return sizeof(Fluid3DSourceGpu) * static_cast<std::size_t>(config.source_count);
}

std::size_t fluid_3d_source_capacity_byte_size() {
    return sizeof(Fluid3DSourceGpu) * static_cast<std::size_t>(kMaxFluid3DSourceCount);
}

} // namespace cubey::projects::fluid_3d
