#include "pyro_3d_sources.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace cubey::projects::fluid::pyro_3d {
namespace {

constexpr std::array<float, 3> kFireCenter{0.5F, 0.10F, 0.5F};
constexpr std::array<float, 3> kExplosionCenter{0.5F, 0.24F, 0.5F};
constexpr float kFireTurbulenceRadius = 0.006F;

struct ExplosionPulse {
    bool active = true;
    float age = 0.0F;
    float emission = 1.0F;
    float flash = 1.0F;
    float shell = 0.0F;
    float smoke = 0.0F;
    float radius = 1.0F;
    float travel = 0.0F;
};

[[nodiscard]] std::array<float, 3> add(std::array<float, 3> lhs, std::array<float, 3> rhs) {
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

[[nodiscard]] float mix(float lhs, float rhs, float amount) {
    return lhs + (rhs - lhs) * amount;
}

[[nodiscard]] float smoothstep(float edge0, float edge1, float value) {
    if (edge0 == edge1) {
        return value < edge0 ? 0.0F : 1.0F;
    }
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

[[nodiscard]] std::array<float, 3> normalize_or_zero(std::array<float, 3> value) {
    const float vector_length = length(value);
    if (vector_length <= 0.00001F) {
        return {};
    }
    return {value[0] / vector_length, value[1] / vector_length, value[2] / vector_length};
}

[[nodiscard]] std::array<float, 3> burner_disk_offset(std::uint32_t index, std::uint32_t count) {
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

[[nodiscard]] std::array<float, 3> explosion_direction(std::uint32_t index, std::uint32_t count) {
    if (count <= 1U) {
        return {0.0F, 1.0F, 0.0F};
    }
    constexpr float kGoldenAngle = 2.39996323F;
    if (index == 0U) {
        return {0.0F, 1.0F, 0.0F};
    }

    const float source_index = static_cast<float>(index - 1U);
    const float normalized_count = static_cast<float>(std::max(count - 1U, 1U));
    const float denominator = std::max(normalized_count - 1.0F, 1.0F);
    const float height = -0.30F + 0.95F * (source_index / denominator);
    const float radius = std::sqrt(std::max(1.0F - height * height, 0.0F));
    const float angle = kGoldenAngle * (source_index + 1.0F);
    return normalize_or_zero({std::cos(angle) * radius, height + 0.34F, std::sin(angle) * radius});
}

[[nodiscard]] Pyro3DSourceState create_explosion_source(const Pyro3DConfig& config,
                                                        std::uint32_t index, ExplosionPulse pulse) {
    const std::array<float, 3> direction = explosion_direction(index, config.source_count);
    const bool core_source = index == 0U;
    const float core_scale = core_source ? 1.0F : 0.76F;
    const float shell_scale = core_source ? 0.42F : 1.0F;
    const float travel = mix(0.012F, 0.115F, pulse.travel) * shell_scale;
    const float radius = config.source_radius * mix(core_source ? 1.70F : 0.95F,
                                                    core_source ? 2.45F : 1.45F, pulse.radius);
    const float speed_scale = mix(1.45F, 0.55F, pulse.age) * (core_source ? 0.68F : 1.0F);
    const std::array<float, 3> velocity = scale(
        normalize_or_zero(add(direction, {0.0F, 0.34F + pulse.smoke * 0.36F, 0.0F})), speed_scale);
    return {
        .position = clamp01(add(kExplosionCenter, scale(direction, travel))),
        .velocity = velocity,
        .material_amount =
            {
                config.source_smoke_amount *
                    ((0.16F * pulse.flash * core_scale) + (0.68F * pulse.shell * shell_scale) +
                     (1.10F * pulse.smoke * shell_scale)),
                config.source_heat_amount *
                    ((1.85F * pulse.flash * core_scale) + (1.20F * pulse.shell * shell_scale) +
                     (0.32F * pulse.smoke)),
                config.source_flame_amount *
                    ((1.75F * pulse.flash * core_scale) + (0.90F * pulse.shell * shell_scale)),
                0.0F,
            },
        .radius = radius,
    };
}

[[nodiscard]] Pyro3DSourceState create_fire_source(const Pyro3DConfig& config,
                                                   std::uint32_t index) {
    const std::array<float, 3> offset =
        scale(burner_disk_offset(index, config.source_count), 1.05F);
    const std::array<float, 3> swirl_direction = normalize_or_zero({offset[2], 0.0F, -offset[0]});
    const std::array<float, 3> velocity =
        normalize_or_zero(add({0.0F, 1.0F, 0.0F}, scale(swirl_direction, 0.22F)));
    return {
        .position = add(kFireCenter, offset),
        .velocity = velocity,
        .material_amount =
            {
                config.source_smoke_amount * 0.008F,
                config.source_heat_amount * 1.55F,
                0.0F,
                config.source_flame_amount * 1.15F,
            },
        .radius = config.source_radius,
    };
}

[[nodiscard]] Pyro3DSourceState create_source(const Pyro3DConfig& config, std::uint32_t index,
                                              ExplosionPulse explosion_pulse = {}) {
    switch (config.mode) {
    case Pyro3DMode::Fire:
        return create_fire_source(config, index);
    case Pyro3DMode::Explosion:
        return create_explosion_source(config, index, explosion_pulse);
    }
    return create_fire_source(config, index);
}

void apply_fire_turbulence(Pyro3DSourceState& source, std::uint32_t index, float time) {
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
        normalize_or_zero(add(source.velocity, {
                                                   std::sin(swell * 1.3F) * 0.09F,
                                                   std::sin(drift * 0.7F + 0.4F) * 0.025F,
                                                   std::cos(swell * 1.1F + 0.8F) * 0.09F,
                                               }));
    source.material_amount[1] *= 0.96F + swell_wave * 0.08F;
    source.material_amount[3] *= 0.92F + swell_wave * 0.12F + drift_wave * 0.04F;
    source.radius *= 0.96F + swell_wave * 0.08F;
}

[[nodiscard]] ExplosionPulse explosion_pulse(const Pyro3DConfig& config,
                                             const FrameTiming& timing) {
    const float time = static_cast<float>(timing.elapsed_seconds);
    const float interval = std::max(config.explosion_interval_seconds, 0.0001F);
    const float duration = std::clamp(config.explosion_duration_seconds, 0.0001F, interval);
    const float phase = std::fmod(std::max(time, 0.0F), interval);
    if (phase >= duration) {
        return {
            .active = false,
            .age = 1.0F,
            .emission = 0.0F,
            .flash = 0.0F,
            .shell = 0.0F,
            .smoke = 0.0F,
            .radius = 1.0F,
            .travel = 1.0F,
        };
    }

    const float age = std::clamp(phase / duration, 0.0F, 1.0F);
    const float flash = 1.0F - smoothstep(0.12F, 0.52F, age);
    const float shell = smoothstep(0.08F, 0.74F, age) * (1.0F - smoothstep(0.88F, 1.0F, age));
    const float smoke = smoothstep(0.34F, 1.0F, age);
    return {
        .active = true,
        .age = age,
        .emission = mix(1.0F, 0.50F, smoothstep(0.18F, 1.0F, age)),
        .flash = flash,
        .shell = shell,
        .smoke = smoke,
        .radius = smoothstep(0.0F, 0.92F, age),
        .travel = smoothstep(0.0F, 1.0F, age),
    };
}

[[nodiscard]] float source_emission_scale(const Pyro3DConfig& config, ExplosionPulse pulse) {
    switch (config.mode) {
    case Pyro3DMode::Fire:
        return 1.0F;
    case Pyro3DMode::Explosion:
        return pulse.active ? config.explosion_boost * pulse.emission : 0.0F;
    }
    return 1.0F;
}

void validate_source_count(const Pyro3DConfig& config) {
    if (config.source_count == 0 || config.source_count > kMaxPyro3DSourceCount) {
        throw std::runtime_error("pyro 3D source count must be 1..16");
    }
}

} // namespace

std::vector<Pyro3DSourceState> create_pyro_3d_sources(const Pyro3DConfig& config) {
    validate_source_count(config);
    std::vector<Pyro3DSourceState> sources;
    sources.reserve(config.source_count);
    for (std::uint32_t index = 0; index < config.source_count; ++index) {
        sources.push_back(create_source(config, index));
    }
    return sources;
}

std::vector<Pyro3DSourceGpu> pyro_3d_sources_to_gpu(const std::vector<Pyro3DSourceState>& sources,
                                                    const Pyro3DConfig& config,
                                                    float emission_scale) {
    std::vector<Pyro3DSourceGpu> gpu;
    gpu.reserve(sources.size());
    const float source_scale = std::max(emission_scale, 0.0F);
    for (const Pyro3DSourceState& source : sources) {
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

std::vector<Pyro3DSourceGpu> update_pyro_3d_sources(std::vector<Pyro3DSourceState>& sources,
                                                    const Pyro3DConfig& config,
                                                    const FrameTiming& timing) {
    if (sources.size() != config.source_count) {
        sources = create_pyro_3d_sources(config);
    }
    const ExplosionPulse pulse =
        config.mode == Pyro3DMode::Explosion ? explosion_pulse(config, timing) : ExplosionPulse{};
    for (std::uint32_t index = 0; index < config.source_count; ++index) {
        sources[index] = create_source(config, index, pulse);
        if (config.mode == Pyro3DMode::Fire) {
            apply_fire_turbulence(sources[index], index,
                                  static_cast<float>(timing.elapsed_seconds));
        }
    }
    return pyro_3d_sources_to_gpu(sources, config, source_emission_scale(config, pulse));
}

std::size_t pyro_3d_source_byte_size(const Pyro3DConfig& config) {
    validate_source_count(config);
    return sizeof(Pyro3DSourceGpu) * static_cast<std::size_t>(config.source_count);
}

std::size_t pyro_3d_source_capacity_byte_size() {
    return sizeof(Pyro3DSourceGpu) * static_cast<std::size_t>(kMaxPyro3DSourceCount);
}

} // namespace cubey::projects::fluid::pyro_3d
