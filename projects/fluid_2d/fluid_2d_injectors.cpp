#include "fluid_2d_injectors.h"

#include <cubey/render/color_space.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace cubey::projects::fluid_2d {
namespace {

constexpr float kTau = 6.28318530718F;
constexpr std::array<float, 2> kCenter{0.5F, 0.5F};

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

[[nodiscard]] std::array<float, 2> target_for_injector(const Fluid2DInjectorState& injector,
                                                       float time) {
    const float orbit_speed = injector.orbit_radius < 0.2F ? 0.23F : 0.17F;
    const float angle = injector.anchor_angle + (injector.orbit_direction * time * orbit_speed);
    const float breath = 0.018F * std::sin((time * 0.42F) + (injector.anchor_angle * 2.0F));
    return {
        kCenter[0] + (std::cos(angle) * (injector.orbit_radius + breath)),
        kCenter[1] + (std::sin(angle) * (injector.orbit_radius + breath)),
    };
}

[[nodiscard]] std::array<float, 2> tangent_for_injector(const Fluid2DInjectorState& injector,
                                                        float time) {
    const float orbit_speed = injector.orbit_radius < 0.2F ? 0.23F : 0.17F;
    const float angle = injector.anchor_angle + (injector.orbit_direction * time * orbit_speed);
    const std::array<float, 2> radial{std::cos(angle), std::sin(angle)};
    return scale({-radial[1], radial[0]}, injector.orbit_direction);
}

[[nodiscard]] std::array<float, 2>
injector_acceleration(const std::vector<Fluid2DInjectorState>& injectors, std::size_t index,
                      float time) {
    const Fluid2DInjectorState& injector = injectors[index];
    const std::array<float, 2> target = target_for_injector(injector, time);
    std::array<float, 2> acceleration =
        add(scale(subtract(target, injector.position), 16.0F),
            scale(tangent_for_injector(injector, time), 0.055F));
    add_to(acceleration, scale(injector.velocity, -4.2F));

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

} // namespace

std::vector<Fluid2DInjectorState> create_fluid_2d_injectors(const Fluid2DConfig& config) {
    if (config.procedural_injector_count == 0 ||
        config.procedural_injector_count > kMaxProceduralInjectorCount) {
        throw std::runtime_error("fluid injector count must be 1..16");
    }

    std::vector<Fluid2DInjectorState> injectors;
    injectors.reserve(config.procedural_injector_count);
    for (std::uint32_t index = 0; index < config.procedural_injector_count; ++index) {
        const float hue = static_cast<float>(index) /
                          static_cast<float>(config.procedural_injector_count);
        const bool inner_ring = config.procedural_injector_count > 4U && (index % 2U) == 1U;
        const float orbit_radius = inner_ring ? 0.18F : 0.29F;
        const float orbit_direction = inner_ring ? -1.0F : 1.0F;
        const float angle = hue * kTau;
        injectors.push_back({
            .position =
                {
                    kCenter[0] + (std::cos(angle) * orbit_radius),
                    kCenter[1] + (std::sin(angle) * orbit_radius),
                },
            .velocity =
                {
                    -std::sin(angle) * orbit_direction * 0.045F,
                    std::cos(angle) * orbit_direction * 0.045F,
                },
            .hue = hue,
            .anchor_angle = angle,
            .orbit_radius = orbit_radius,
            .orbit_direction = orbit_direction,
        });
    }
    return injectors;
}

std::vector<Fluid2DInjectorGpu>
fluid_2d_injectors_to_gpu(const std::vector<Fluid2DInjectorState>& injectors,
                          const Fluid2DConfig& config) {
    std::vector<Fluid2DInjectorGpu> gpu_injectors;
    gpu_injectors.reserve(injectors.size());
    for (const Fluid2DInjectorState& injector : injectors) {
        const std::array<float, 3> dye =
            cubey::render::hsv_to_linear_rgb({.hue = injector.hue,
                                              .saturation = 1.0F,
                                              .value = 1.0F});
        const bool inner_ring = injector.orbit_radius < 0.2F;
        gpu_injectors.push_back({
            .position_radius_strength =
                {
                    injector.position[0],
                    injector.position[1],
                    config.fallback_injection_radius * (inner_ring ? 0.92F : 0.84F),
                    config.fallback_injection_strength * (inner_ring ? 0.90F : 0.84F),
                },
            .velocity_carry_propulsion =
                {
                    injector.velocity[0],
                    injector.velocity[1],
                    6.0F,
                    inner_ring ? 1.15F : 1.35F,
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

std::vector<Fluid2DInjectorGpu>
update_fluid_2d_injectors(std::vector<Fluid2DInjectorState>& injectors,
                          const Fluid2DConfig& config, const cubey::FrameTiming& timing) {
    if (injectors.size() != config.procedural_injector_count) {
        injectors = create_fluid_2d_injectors(config);
    }

    const float dt =
        std::min(static_cast<float>(timing.delta_seconds), config.fixed_delta_seconds);
    const float time = static_cast<float>(timing.elapsed_seconds);
    for (std::size_t index = 0; index < injectors.size(); ++index) {
        const std::array<float, 2> acceleration = injector_acceleration(injectors, index, time);
        add_to(injectors[index].velocity, scale(acceleration, dt));
        clamp_speed(injectors[index].velocity, 0.38F);
    }
    for (Fluid2DInjectorState& injector : injectors) {
        add_to(injector.position, scale(injector.velocity, dt));
        integrate_boundary(injector.position, injector.velocity);
    }

    return fluid_2d_injectors_to_gpu(injectors, config);
}

std::size_t fluid_2d_injector_byte_size(const Fluid2DConfig& config) {
    if (config.procedural_injector_count == 0 ||
        config.procedural_injector_count > kMaxProceduralInjectorCount) {
        throw std::runtime_error("fluid injector count must be 1..16");
    }
    return sizeof(Fluid2DInjectorGpu) *
           static_cast<std::size_t>(config.procedural_injector_count);
}

} // namespace cubey::projects::fluid_2d
