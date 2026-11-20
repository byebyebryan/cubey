#include "smoke_2d_injectors.h"

#include <cubey/render/color_space.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace cubey::projects::fluid::smoke_2d {
namespace {

constexpr float kTau = 6.28318530718F;
constexpr std::array<float, 2> kCenter{0.5F, 0.5F};
constexpr float kOrbitMinRadius = 0.04F;
constexpr float kOrbitMaxRadius = 0.42F;
constexpr std::array<float, 3> kBowedPlumesAmber{1.0F, 0.29F, 0.09F};
constexpr std::array<float, 3> kBowedPlumesCyan{0.12F, 0.66F, 0.74F};

struct InjectorGpuTuning {
    float radius_scale = 1.0F;
    float strength_scale = 1.0F;
    float carry_scale = 6.0F;
    float propulsion_scale = 1.25F;
};

[[nodiscard]] std::array<float, 2> add(std::array<float, 2> lhs, std::array<float, 2> rhs) {
    return {lhs[0] + rhs[0], lhs[1] + rhs[1]};
}

[[nodiscard]] std::array<float, 2> subtract(std::array<float, 2> lhs, std::array<float, 2> rhs) {
    return {lhs[0] - rhs[0], lhs[1] - rhs[1]};
}

[[nodiscard]] std::array<float, 2> scale(std::array<float, 2> value, float amount) {
    return {value[0] * amount, value[1] * amount};
}

[[nodiscard]] float length(std::array<float, 2> value) {
    return std::sqrt((value[0] * value[0]) + (value[1] * value[1]));
}

[[nodiscard]] std::array<float, 2> normalize_or_zero(std::array<float, 2> value) {
    const float vector_length = length(value);
    if (vector_length <= 0.00001F) {
        return {};
    }
    return {value[0] / vector_length, value[1] / vector_length};
}

void add_to(std::array<float, 2>& lhs, std::array<float, 2> rhs) {
    lhs[0] += rhs[0];
    lhs[1] += rhs[1];
}

void clamp_speed(std::array<float, 2>& velocity, float max_speed) {
    const float speed = length(velocity);
    if (speed <= max_speed || speed <= 0.00001F) {
        return;
    }
    velocity = scale(velocity, max_speed / speed);
}

void integrate_boundary(std::array<float, 2>& position, std::array<float, 2>& velocity) {
    constexpr float kMin = 0.055F;
    constexpr float kMax = 0.945F;
    for (std::size_t axis = 0; axis < 2; ++axis) {
        if (position[axis] < kMin) {
            position[axis] = kMin;
            velocity[axis] = std::max(velocity[axis], 0.0F);
        }
        if (position[axis] > kMax) {
            position[axis] = kMax;
            velocity[axis] = std::min(velocity[axis], 0.0F);
        }
    }
}

[[nodiscard]] float hash01(std::uint32_t value) {
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return static_cast<float>(value & 0x00ffffffU) / static_cast<float>(0x01000000U);
}

[[nodiscard]] float smoothstep(float edge0, float edge1, float value) {
    const float amount = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return amount * amount * (3.0F - (2.0F * amount));
}

[[nodiscard]] float pulse(float rise_begin, float rise_end, float fall_begin, float fall_end,
                          float time) {
    return smoothstep(rise_begin, rise_end, time) *
           (1.0F - smoothstep(fall_begin, fall_end, time));
}

[[nodiscard]] Smoke2DInjectorGpu make_bowed_plumes_source(float x, float y, float radius,
                                                           float strength, float force_x,
                                                           float force_y,
                                                           std::array<float, 3> dye,
                                                           float active) {
    return {
        .position_radius_strength = {x, y, radius, strength},
        .velocity_carry_propulsion = {force_x, force_y, 1.0F, 0.0F},
        .dye_active = {dye[0], dye[1], dye[2], active},
    };
}

[[nodiscard]] std::array<float, 2> clamp_uv(std::array<float, 2> value) {
    return {
        std::clamp(value[0], 0.08F, 0.92F),
        std::clamp(value[1], 0.08F, 0.92F),
    };
}

[[nodiscard]] float spread_t(const Smoke2DConfig& config, std::uint32_t index) {
    if (config.procedural_injector_count <= 1U) {
        return 0.0F;
    }
    const float count = static_cast<float>(config.procedural_injector_count);
    return (static_cast<float>(index) / (count - 1.0F)) * 2.0F - 1.0F;
}

[[nodiscard]] float angular_speed_spread_t(const Smoke2DConfig& config, std::uint32_t index) {
    const float t = spread_t(config, index);
    if (std::abs(t) > 0.0001F || config.injector_orbit_angular_speed_spread <= 0.0F) {
        return t;
    }
    const float count = static_cast<float>(config.procedural_injector_count);
    const float nearest_nonzero_step = 1.0F / std::max(count - 1.0F, 1.0F);
    return (index % 2U) == 0U ? nearest_nonzero_step : -nearest_nonzero_step;
}

[[nodiscard]] float orbit_radius(const Smoke2DConfig& config, std::uint32_t index) {
    const float count = static_cast<float>(std::max(config.procedural_injector_count, 1U));
    const float spacing = config.injector_orbit_radius_spread / count;
    const std::uint32_t seed = 0x9e3779b9U * (index + 1U);
    const float jitter = config.procedural_injector_count <= 1U
                             ? 0.0F
                             : (hash01(seed ^ 0x45d9f3bU) - 0.5F) * spacing * 0.35F;
    return std::clamp(config.injector_orbit_radius +
                          (spread_t(config, index) * config.injector_orbit_radius_spread * 0.5F) +
                          jitter,
                      kOrbitMinRadius, kOrbitMaxRadius);
}

[[nodiscard]] float orbit_angular_speed(const Smoke2DConfig& config, std::uint32_t index) {
    return config.injector_orbit_angular_speed +
           (angular_speed_spread_t(config, index) * config.injector_orbit_angular_speed_spread *
            0.5F);
}

[[nodiscard]] float orbit_phase(const Smoke2DConfig& config, std::uint32_t index) {
    if (config.procedural_injector_count <= 1U) {
        return 0.0F;
    }
    const float count = static_cast<float>(config.procedural_injector_count);
    return (static_cast<float>(index) / count) * config.injector_orbit_phase_spread * kTau;
}

void sync_orbit_parameters(Smoke2DInjectorState& injector, const Smoke2DConfig& config,
                           std::uint32_t index) {
    injector.anchor_angle = orbit_phase(config, index);
    injector.orbit_radius = orbit_radius(config, index);
    injector.angular_speed = orbit_angular_speed(config, index);
}

[[nodiscard]] std::array<float, 2>
target_for_injector(const Smoke2DConfig&, const Smoke2DInjectorState& injector, float time) {
    const float angle = injector.anchor_angle + (injector.angular_speed * time);
    return clamp_uv({
        kCenter[0] + (std::cos(angle) * injector.orbit_radius),
        kCenter[1] + (std::sin(angle) * injector.orbit_radius),
    });
}

[[nodiscard]] std::array<float, 2> tangent_for_injector(const Smoke2DConfig& config,
                                                        const Smoke2DInjectorState& injector,
                                                        float time) {
    constexpr float kDerivativeStep = 0.25F;
    const float previous_time = std::max(0.0F, time - kDerivativeStep);
    return normalize_or_zero(subtract(target_for_injector(config, injector, time + kDerivativeStep),
                                      target_for_injector(config, injector, previous_time)));
}

[[nodiscard]] float max_injector_speed(const Smoke2DConfig& config) {
    const float max_angular_speed = std::abs(config.injector_orbit_angular_speed) +
                                    (config.injector_orbit_angular_speed_spread * 0.5F);
    const float max_radius =
        config.injector_orbit_radius + (config.injector_orbit_radius_spread * 0.5F);
    return std::max(0.50F, max_angular_speed * max_radius * 2.0F);
}

[[nodiscard]] std::array<float, 2>
injector_acceleration(const std::vector<Smoke2DInjectorState>& injectors,
                      const Smoke2DConfig& config, std::size_t index, float time) {
    const Smoke2DInjectorState& injector = injectors[index];
    const std::array<float, 2> target = target_for_injector(config, injector, time);
    std::array<float, 2> acceleration =
        add(scale(subtract(target, injector.position), 11.0F),
            scale(tangent_for_injector(config, injector, time), 0.055F));
    add_to(acceleration, scale(injector.velocity, -3.2F));

    for (std::size_t other_index = 0; other_index < injectors.size(); ++other_index) {
        if (other_index == index) {
            continue;
        }
        const std::array<float, 2> offset =
            subtract(injector.position, injectors[other_index].position);
        const float distance = length(offset);
        constexpr float kSeparationRadius = 0.105F;
        if (distance > 0.00001F && distance < kSeparationRadius) {
            const float amount = (kSeparationRadius - distance) / kSeparationRadius;
            add_to(acceleration, scale(normalize_or_zero(offset), amount * 0.18F));
        }
    }

    constexpr float kWallMargin = 0.13F;
    if (injector.position[0] < kWallMargin) {
        acceleration[0] += (kWallMargin - injector.position[0]) * 1.5F;
    }
    if (injector.position[0] > 1.0F - kWallMargin) {
        acceleration[0] -= (injector.position[0] - (1.0F - kWallMargin)) * 1.5F;
    }
    if (injector.position[1] < kWallMargin) {
        acceleration[1] += (kWallMargin - injector.position[1]) * 1.5F;
    }
    if (injector.position[1] > 1.0F - kWallMargin) {
        acceleration[1] -= (injector.position[1] - (1.0F - kWallMargin)) * 1.5F;
    }

    return acceleration;
}

[[nodiscard]] std::array<float, 2>
initial_velocity_for_injector(const Smoke2DConfig& config, const Smoke2DInjectorState& injector) {
    const float target_speed =
        std::max(std::abs(injector.angular_speed) * std::max(injector.orbit_radius, 0.04F), 0.035F);
    return scale(tangent_for_injector(config, injector, 0.0F), target_speed);
}

[[nodiscard]] InjectorGpuTuning gpu_tuning_for_injector() {
    return {.radius_scale = 0.86F,
            .strength_scale = 0.88F,
            .carry_scale = 5.5F,
            .propulsion_scale = 1.15F};
}

} // namespace

std::vector<Smoke2DInjectorGpu> create_bowed_plumes_injectors(float scene_time_seconds) {
    const float warm = pulse(0.0F, 0.35F, 2.35F, 3.72F, scene_time_seconds);
    const float cool = pulse(0.44F, 0.89F, 2.95F, 4.25F, scene_time_seconds);
    const float warm_bend = smoothstep(0.42F, 2.90F, scene_time_seconds);
    const float cool_bend = smoothstep(0.88F, 3.29F, scene_time_seconds);

    std::vector<Smoke2DInjectorGpu> injectors;
    injectors.reserve(kBowedPlumesInjectorCount);
    injectors.push_back(make_bowed_plumes_source(
        0.242F + (0.115F * warm_bend), 0.707F - (0.059F * warm_bend),
        0.052F - (0.008F * warm_bend), 12.0F, 1.50F - (0.62F * warm_bend),
        -0.54F - (0.81F * warm_bend), kBowedPlumesAmber, warm));
    injectors.push_back(make_bowed_plumes_source(
        0.746F - (0.090F * cool_bend), 0.259F + (0.085F * cool_bend),
        0.036F + (0.010F * cool_bend), 12.0F, -1.08F - (0.36F * cool_bend),
        1.36F - (0.81F * cool_bend), kBowedPlumesCyan, cool));

    const float swirl = pulse(3.02F, 3.24F, 3.58F, 4.04F, scene_time_seconds);
    const float swirl_source_count = static_cast<float>(kBowedPlumesInjectorCount - 2U);
    for (std::uint32_t index = 0; index < kBowedPlumesInjectorCount - 2U; ++index) {
        const float angle = static_cast<float>(index) * kTau / swirl_source_count;
        const float x = std::cos(angle);
        const float y = std::sin(angle);
        injectors.push_back(make_bowed_plumes_source(
            0.524F + (0.091F * x), 0.498F + (0.103F * y), 0.056F,
            4.9F * (1.0F - (0.16F * x)), -1.2F * y, 1.2F * x, {}, swirl));
    }
    return injectors;
}

std::vector<Smoke2DInjectorState> create_smoke_2d_injectors(const Smoke2DConfig& config) {
    if (config.procedural_injector_count == 0 ||
        config.procedural_injector_count > kMaxProceduralInjectorCount) {
        throw std::runtime_error("smoke injector count must be 1..16");
    }

    std::vector<Smoke2DInjectorState> injectors;
    injectors.reserve(config.procedural_injector_count);
    for (std::uint32_t index = 0; index < config.procedural_injector_count; ++index) {
        const float hue =
            static_cast<float>(index) / static_cast<float>(config.procedural_injector_count);
        Smoke2DInjectorState injector{
            .position = {},
            .velocity = {},
            .hue = hue,
            .anchor_angle = {},
            .orbit_radius = {},
            .angular_speed = {},
            .seed = index,
        };
        sync_orbit_parameters(injector, config, index);
        injector.position = target_for_injector(config, injector, 0.0F);
        injector.velocity = initial_velocity_for_injector(config, injector);
        injectors.push_back(injector);
    }
    return injectors;
}

std::vector<Smoke2DInjectorGpu>
smoke_2d_injectors_to_gpu(const std::vector<Smoke2DInjectorState>& injectors,
                          const Smoke2DConfig& config) {
    std::vector<Smoke2DInjectorGpu> gpu_injectors;
    gpu_injectors.reserve(injectors.size());
    for (const Smoke2DInjectorState& injector : injectors) {
        const std::array<float, 3> dye = cubey::render::hsv_to_linear_rgb(
            {.hue = injector.hue, .saturation = 0.82F, .value = 1.08F});
        const InjectorGpuTuning tuning = gpu_tuning_for_injector();
        gpu_injectors.push_back({
            .position_radius_strength =
                {
                    injector.position[0],
                    injector.position[1],
                    config.injector_injection_radius * tuning.radius_scale,
                    config.injector_injection_strength * tuning.strength_scale,
                },
            .velocity_carry_propulsion =
                {
                    injector.velocity[0],
                    injector.velocity[1],
                    tuning.carry_scale,
                    tuning.propulsion_scale * config.injector_propulsion_strength,
                },
            .dye_active =
                {
                    dye[0],
                    dye[1],
                    dye[2],
                    1.0F,
                },
        });
    }
    return gpu_injectors;
}

std::vector<Smoke2DInjectorGpu>
update_smoke_2d_injectors(std::vector<Smoke2DInjectorState>& injectors, const Smoke2DConfig& config,
                          const cubey::FrameTiming& timing) {
    if (injectors.size() != config.procedural_injector_count) {
        injectors = create_smoke_2d_injectors(config);
    }

    const float dt = smoke_2d_clamped_step_seconds(config, timing.delta_seconds);
    const float time = static_cast<float>(timing.elapsed_seconds);
    for (std::size_t index = 0; index < injectors.size(); ++index) {
        sync_orbit_parameters(injectors[index], config, static_cast<std::uint32_t>(index));
        const std::array<float, 2> acceleration =
            injector_acceleration(injectors, config, index, time);
        add_to(injectors[index].velocity, scale(acceleration, dt));
        clamp_speed(injectors[index].velocity, max_injector_speed(config));
    }
    for (Smoke2DInjectorState& injector : injectors) {
        add_to(injector.position, scale(injector.velocity, dt));
        integrate_boundary(injector.position, injector.velocity);
    }

    return smoke_2d_injectors_to_gpu(injectors, config);
}

std::size_t smoke_2d_injector_byte_size(const Smoke2DConfig& config) {
    if (config.procedural_injector_count == 0 ||
        config.procedural_injector_count > kMaxProceduralInjectorCount) {
        throw std::runtime_error("smoke injector count must be 1..16");
    }
    return sizeof(Smoke2DInjectorGpu) * static_cast<std::size_t>(config.procedural_injector_count);
}

std::size_t smoke_2d_injector_capacity_byte_size() {
    return sizeof(Smoke2DInjectorGpu) * static_cast<std::size_t>(kMaxProceduralInjectorCount);
}

} // namespace cubey::projects::fluid::smoke_2d
