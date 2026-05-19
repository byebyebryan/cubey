#include "fluid_3d_injectors.h"

#include <cubey/render/color_space.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace cubey::projects::fluid_3d {
namespace {

constexpr float kTau = 6.28318530718F;
constexpr float kDegreesToRadians = kTau / 360.0F;
constexpr std::array<float, 3> kCenter{0.5F, 0.5F, 0.5F};
constexpr float kOrbitMinRadius = 0.06F;
constexpr float kOrbitMaxRadius = 0.42F;

[[nodiscard]] std::array<float, 3> add(std::array<float, 3> lhs, std::array<float, 3> rhs) {
    return {lhs[0] + rhs[0], lhs[1] + rhs[1], lhs[2] + rhs[2]};
}

[[nodiscard]] float length(std::array<float, 3> value) {
    return std::sqrt((value[0] * value[0]) + (value[1] * value[1]) + (value[2] * value[2]));
}

[[nodiscard]] std::array<float, 3> subtract(std::array<float, 3> lhs, std::array<float, 3> rhs) {
    return {lhs[0] - rhs[0], lhs[1] - rhs[1], lhs[2] - rhs[2]};
}

[[nodiscard]] std::array<float, 3> scale(std::array<float, 3> value, float amount) {
    return {value[0] * amount, value[1] * amount, value[2] * amount};
}

[[nodiscard]] std::array<float, 3> normalize_or_zero(std::array<float, 3> value) {
    const float vector_length = length(value);
    if (vector_length <= 0.00001F) {
        return {};
    }
    return {value[0] / vector_length, value[1] / vector_length, value[2] / vector_length};
}

void add_to(std::array<float, 3>& lhs, std::array<float, 3> rhs) {
    lhs[0] += rhs[0];
    lhs[1] += rhs[1];
    lhs[2] += rhs[2];
}

void clamp_speed(std::array<float, 3>& velocity, float max_speed) {
    const float speed = length(velocity);
    if (speed <= max_speed || speed <= 0.00001F) {
        return;
    }
    velocity = scale(velocity, max_speed / speed);
}

void integrate_boundary(std::array<float, 3>& position, std::array<float, 3>& velocity) {
    constexpr float kMin = 0.055F;
    constexpr float kMax = 0.945F;
    for (std::size_t axis = 0; axis < 3; ++axis) {
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

[[nodiscard]] std::array<float, 3> clamp_uv(std::array<float, 3> value) {
    return {
        std::clamp(value[0], 0.08F, 0.92F),
        std::clamp(value[1], 0.08F, 0.92F),
        std::clamp(value[2], 0.08F, 0.92F),
    };
}

[[nodiscard]] float spread_t(const Fluid3DConfig& config, std::uint32_t index) {
    if (config.injector_count <= 1U) {
        return 0.0F;
    }
    const float count = static_cast<float>(config.injector_count);
    return (static_cast<float>(index) / (count - 1.0F)) * 2.0F - 1.0F;
}

[[nodiscard]] float orbit_radius(const Fluid3DConfig& config, std::uint32_t index) {
    const float count = static_cast<float>(std::max(config.injector_count, 1U));
    const float spacing = config.injector_orbit_radius_spread / count;
    const std::uint32_t seed = 0x9e3779b9U * (index + 1U);
    const float jitter = config.injector_count <= 1U
                             ? 0.0F
                             : (hash01(seed ^ 0x45d9f3bU) - 0.5F) * spacing * 0.35F;
    return std::clamp(config.injector_orbit_radius +
                          (spread_t(config, index) * config.injector_orbit_radius_spread * 0.5F) +
                          jitter,
                      kOrbitMinRadius, kOrbitMaxRadius);
}

[[nodiscard]] float orbit_angular_speed(const Fluid3DConfig& config, std::uint32_t index) {
    return config.injector_orbit_angular_speed +
           (spread_t(config, index) * config.injector_orbit_angular_speed_spread * 0.5F);
}

[[nodiscard]] float orbit_phase(const Fluid3DConfig& config, std::uint32_t index) {
    if (config.injector_count <= 1U) {
        return 0.0F;
    }
    const float count = static_cast<float>(config.injector_count);
    return (static_cast<float>(index) / count) * config.injector_orbit_phase_spread * kTau;
}

[[nodiscard]] float orbit_inclination_radians(const Fluid3DConfig& config, std::uint32_t index) {
    const float degrees =
        std::clamp(config.injector_orbit_inclination_degrees +
                       (spread_t(config, index) *
                        config.injector_orbit_inclination_spread_degrees * 0.5F),
                   -80.0F, 80.0F);
    return degrees * kDegreesToRadians;
}

void sync_orbit_parameters(Fluid3DInjectorState& injector, const Fluid3DConfig& config,
                           std::uint32_t index) {
    injector.phase = orbit_phase(config, index);
    injector.radius = orbit_radius(config, index);
    injector.speed = orbit_angular_speed(config, index);
    injector.inclination_radians = orbit_inclination_radians(config, index);
}

[[nodiscard]] std::array<float, 3>
target_for_injector(const Fluid3DConfig&, const Fluid3DInjectorState& injector, float time) {
    const float angle = injector.phase + (injector.speed * time);
    const float sin_inclination = std::sin(injector.inclination_radians);
    const float cos_inclination = std::cos(injector.inclination_radians);
    return clamp_uv({
        kCenter[0] + (std::cos(angle) * injector.radius),
        kCenter[1] + (std::sin(angle) * injector.radius * sin_inclination),
        kCenter[2] + (std::sin(angle) * injector.radius * cos_inclination),
    });
}

[[nodiscard]] std::array<float, 3> tangent_for_injector(const Fluid3DConfig& config,
                                                        const Fluid3DInjectorState& injector,
                                                        float time) {
    constexpr float kDerivativeStep = 0.25F;
    const float previous_time = std::max(0.0F, time - kDerivativeStep);
    return normalize_or_zero(subtract(target_for_injector(config, injector, time + kDerivativeStep),
                                      target_for_injector(config, injector, previous_time)));
}

[[nodiscard]] float max_injector_speed(const Fluid3DConfig& config) {
    const float max_angular_speed = std::abs(config.injector_orbit_angular_speed) +
                                    (config.injector_orbit_angular_speed_spread * 0.5F);
    const float max_radius =
        config.injector_orbit_radius + (config.injector_orbit_radius_spread * 0.5F);
    return std::max(0.45F, max_angular_speed * max_radius * 2.2F);
}

[[nodiscard]] std::array<float, 3>
injector_acceleration(const std::vector<Fluid3DInjectorState>& injectors,
                      const Fluid3DConfig& config, std::size_t index, float time) {
    const Fluid3DInjectorState& injector = injectors[index];
    const std::array<float, 3> target = target_for_injector(config, injector, time);
    std::array<float, 3> acceleration =
        add(scale(subtract(target, injector.position), 10.0F),
            scale(tangent_for_injector(config, injector, time), 0.070F));
    add_to(acceleration, scale(injector.velocity, -3.0F));

    for (std::size_t other_index = 0; other_index < injectors.size(); ++other_index) {
        if (other_index == index) {
            continue;
        }
        const std::array<float, 3> offset =
            subtract(injector.position, injectors[other_index].position);
        const float distance = length(offset);
        constexpr float kSeparationRadius = 0.14F;
        if (distance > 0.00001F && distance < kSeparationRadius) {
            const float amount = (kSeparationRadius - distance) / kSeparationRadius;
            add_to(acceleration, scale(normalize_or_zero(offset), amount * 0.24F));
        }
    }

    constexpr float kWallMargin = 0.13F;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (injector.position[axis] < kWallMargin) {
            acceleration[axis] += (kWallMargin - injector.position[axis]) * 1.5F;
        }
        if (injector.position[axis] > 1.0F - kWallMargin) {
            acceleration[axis] -= (injector.position[axis] - (1.0F - kWallMargin)) * 1.5F;
        }
    }

    return acceleration;
}

[[nodiscard]] std::array<float, 3>
initial_velocity_for_injector(const Fluid3DConfig& config, const Fluid3DInjectorState& injector) {
    const float target_speed =
        std::max(std::abs(injector.speed) * std::max(injector.radius, 0.04F), 0.035F);
    return scale(tangent_for_injector(config, injector, 0.0F), target_speed);
}

[[nodiscard]] std::array<float, 3>
gpu_force_for_injector(const Fluid3DInjectorState& injector, const Fluid3DConfig& config) {
    const std::array<float, 3> direction = normalize_or_zero(injector.velocity);
    return add(scale(injector.velocity, config.injector_velocity_scale),
               scale(direction, -config.injector_propulsion_strength));
}

} // namespace

std::vector<Fluid3DInjectorState> create_fluid_3d_injectors(const Fluid3DConfig& config) {
    if (config.injector_count == 0 || config.injector_count > kMaxFluid3DInjectorCount) {
        throw std::runtime_error("fluid 3D injector count must be 1..16");
    }
    std::vector<Fluid3DInjectorState> injectors;
    injectors.reserve(config.injector_count);
    for (std::uint32_t index = 0; index < config.injector_count; ++index) {
        const float hue = static_cast<float>(index) / static_cast<float>(config.injector_count);
        Fluid3DInjectorState injector{
            .position = {},
            .velocity = {},
            .color =
                cubey::render::hsv_to_linear_rgb({.hue = hue, .saturation = 0.82F, .value = 1.0F}),
            .phase = {},
            .radius = {},
            .speed = {},
            .inclination_radians = {},
        };
        sync_orbit_parameters(injector, config, index);
        injector.position = target_for_injector(config, injector, 0.0F);
        injector.velocity = initial_velocity_for_injector(config, injector);
        injectors.push_back(injector);
    }
    return injectors;
}

std::vector<Fluid3DInjectorGpu>
fluid_3d_injectors_to_gpu(const std::vector<Fluid3DInjectorState>& injectors,
                          const Fluid3DConfig& config) {
    std::vector<Fluid3DInjectorGpu> gpu;
    gpu.reserve(injectors.size());
    for (const Fluid3DInjectorState& injector : injectors) {
        const std::array<float, 3> force = gpu_force_for_injector(injector, config);
        gpu.push_back({
            .position_radius =
                {
                    injector.position[0],
                    injector.position[1],
                    injector.position[2],
                    config.injector_radius,
                },
            .velocity_strength =
                {
                    force[0],
                    force[1],
                    force[2],
                    config.injector_strength,
                },
            .color_active =
                {
                    injector.color[0],
                    injector.color[1],
                    injector.color[2],
                    1.0F,
                },
        });
    }
    return gpu;
}

std::vector<Fluid3DInjectorGpu>
update_fluid_3d_injectors(std::vector<Fluid3DInjectorState>& injectors, const Fluid3DConfig& config,
                          const FrameTiming& timing) {
    if (injectors.size() != config.injector_count) {
        injectors = create_fluid_3d_injectors(config);
    }
    const float time = static_cast<float>(timing.elapsed_seconds);
    const float dt = std::min(static_cast<float>(timing.delta_seconds), config.fixed_delta_seconds);
    for (std::size_t index = 0; index < injectors.size(); ++index) {
        sync_orbit_parameters(injectors[index], config, static_cast<std::uint32_t>(index));
        const std::array<float, 3> acceleration =
            injector_acceleration(injectors, config, index, time);
        add_to(injectors[index].velocity, scale(acceleration, dt));
        clamp_speed(injectors[index].velocity, max_injector_speed(config));
    }
    for (Fluid3DInjectorState& injector : injectors) {
        add_to(injector.position, scale(injector.velocity, dt));
        integrate_boundary(injector.position, injector.velocity);
    }
    return fluid_3d_injectors_to_gpu(injectors, config);
}

std::size_t fluid_3d_injector_byte_size(const Fluid3DConfig& config) {
    if (config.injector_count == 0 || config.injector_count > kMaxFluid3DInjectorCount) {
        throw std::runtime_error("fluid 3D injector count must be 1..16");
    }
    return sizeof(Fluid3DInjectorGpu) * static_cast<std::size_t>(config.injector_count);
}

std::size_t fluid_3d_injector_capacity_byte_size() {
    return sizeof(Fluid3DInjectorGpu) * static_cast<std::size_t>(kMaxFluid3DInjectorCount);
}

} // namespace cubey::projects::fluid_3d
