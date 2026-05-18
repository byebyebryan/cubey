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
constexpr float kVariedOrbitMinRadius = 0.14F;
constexpr float kVariedOrbitMaxRadius = 0.36F;

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

[[nodiscard]] std::array<float, 2> clamp_uv(std::array<float, 2> value) {
    return {
        std::clamp(value[0], 0.08F, 0.92F),
        std::clamp(value[1], 0.08F, 0.92F),
    };
}

[[nodiscard]] float injector_speed_scale(const Fluid2DConfig& config) {
    return std::max(config.injector_speed, 0.01F);
}

[[nodiscard]] bool varied_orbit_motion(Fluid2DInjectorMotion motion) {
    return motion == Fluid2DInjectorMotion::SameDirectionOrbits ||
           motion == Fluid2DInjectorMotion::AlternatingDirectionOrbits;
}

[[nodiscard]] float varied_orbit_radius(const Fluid2DConfig& config, std::uint32_t index) {
    const float count = static_cast<float>(std::max(config.procedural_injector_count, 1U));
    const float band = kVariedOrbitMaxRadius - kVariedOrbitMinRadius;
    const float spacing = band / count;
    const float band_t = (static_cast<float>(index) + 0.5F) / count;
    const float centered_radius = kVariedOrbitMinRadius + (band_t * band);
    const std::uint32_t seed = 0x9e3779b9U * (index + 1U);
    const float jitter = (hash01(seed ^ 0x45d9f3bU) - 0.5F) * spacing * 0.35F;
    return std::clamp(centered_radius + jitter, kVariedOrbitMinRadius,
                      kVariedOrbitMaxRadius);
}

[[nodiscard]] std::array<float, 2> ring_target(const Fluid2DConfig& config,
                                               const Fluid2DInjectorState& injector,
                                               float time) {
    const float speed_scale = injector_speed_scale(config);
    const float orbit_speed = injector.orbit_radius < 0.2F ? 0.36F : 0.28F;
    const float angle =
        injector.anchor_angle + (injector.orbit_direction * time * orbit_speed * speed_scale);
    const float breath =
        0.018F * std::sin((time * 0.42F * speed_scale) + (injector.anchor_angle * 2.0F));
    return {
        kCenter[0] + (std::cos(angle) * (injector.orbit_radius + breath)),
        kCenter[1] + (std::sin(angle) * (injector.orbit_radius + breath)),
    };
}

[[nodiscard]] std::array<float, 2> varied_orbit_target(const Fluid2DConfig& config,
                                                       const Fluid2DInjectorState& injector,
                                                       float time) {
    const std::uint32_t seed = 0x9e3779b9U * (injector.seed + 1U);
    const float band = kVariedOrbitMaxRadius - kVariedOrbitMinRadius;
    const float base_radius =
        std::clamp(injector.orbit_radius, kVariedOrbitMinRadius, kVariedOrbitMaxRadius);
    const float radius_t = (base_radius - kVariedOrbitMinRadius) / band;
    const float aspect_variation = (hash01(seed ^ 0x119de1f3U) - 0.5F) * 0.18F;
    const float x_radius = base_radius * (1.0F + aspect_variation);
    const float y_radius = base_radius * (1.0F - aspect_variation);
    const float phase = injector.anchor_angle + (hash01(seed ^ 0x3449b1dU) * kTau);
    const float speed = (0.42F - (radius_t * 0.16F)) *
                        (0.88F + (hash01(seed ^ 0x9f3a179U) * 0.24F));
    const float direction = injector.orbit_direction;
    const float angle = phase + (direction * time * speed * injector_speed_scale(config));
    return clamp_uv({
        kCenter[0] + (std::cos(angle) * x_radius),
        kCenter[1] + (std::sin(angle) * y_radius),
    });
}

[[nodiscard]] std::array<float, 2> lissajous_target(const Fluid2DConfig& config,
                                                    const Fluid2DInjectorState& injector,
                                                    float time) {
    const float scaled_time = time * injector_speed_scale(config);
    const float phase = injector.anchor_angle;
    return clamp_uv({
        0.5F + (0.34F * std::sin((scaled_time * 0.42F) + phase)) +
            (0.045F * std::sin((scaled_time * 1.02F) + (phase * 0.7F))),
        0.5F + (0.29F * std::sin((scaled_time * 0.62F) + (phase * 1.7F))),
    });
}

[[nodiscard]] std::array<float, 2> target_for_injector(const Fluid2DConfig& config,
                                                       const Fluid2DInjectorState& injector,
                                                       float time) {
    switch (config.injector_motion) {
    case Fluid2DInjectorMotion::OneRing:
    case Fluid2DInjectorMotion::TwoRings:
        return ring_target(config, injector, time);
    case Fluid2DInjectorMotion::SameDirectionOrbits:
    case Fluid2DInjectorMotion::AlternatingDirectionOrbits:
        return varied_orbit_target(config, injector, time);
    case Fluid2DInjectorMotion::Lissajous:
        return lissajous_target(config, injector, time);
    }
    return ring_target(config, injector, time);
}

[[nodiscard]] std::array<float, 2> tangent_for_injector(const Fluid2DConfig& config,
                                                        const Fluid2DInjectorState& injector,
                                                        float time) {
    constexpr float kDerivativeStep = 0.25F;
    const float previous_time = std::max(0.0F, time - kDerivativeStep);
    return normalize_or_zero(
        subtract(target_for_injector(config, injector, time + kDerivativeStep),
                 target_for_injector(config, injector, previous_time)));
}

[[nodiscard]] float stiffness_for_motion(Fluid2DInjectorMotion motion) {
    switch (motion) {
    case Fluid2DInjectorMotion::OneRing:
    case Fluid2DInjectorMotion::TwoRings:
        return 16.0F;
    case Fluid2DInjectorMotion::SameDirectionOrbits:
    case Fluid2DInjectorMotion::AlternatingDirectionOrbits:
        return 11.0F;
    case Fluid2DInjectorMotion::Lissajous:
        return 15.0F;
    }
    return 16.0F;
}

[[nodiscard]] float tangent_drive_for_motion(Fluid2DInjectorMotion motion) {
    switch (motion) {
    case Fluid2DInjectorMotion::OneRing:
    case Fluid2DInjectorMotion::TwoRings:
        return 0.055F;
    case Fluid2DInjectorMotion::SameDirectionOrbits:
    case Fluid2DInjectorMotion::AlternatingDirectionOrbits:
        return 0.055F;
    case Fluid2DInjectorMotion::Lissajous:
        return 0.080F;
    }
    return 0.055F;
}

[[nodiscard]] float damping_for_motion(Fluid2DInjectorMotion motion) {
    switch (motion) {
    case Fluid2DInjectorMotion::SameDirectionOrbits:
    case Fluid2DInjectorMotion::AlternatingDirectionOrbits:
        return 3.2F;
    case Fluid2DInjectorMotion::OneRing:
    case Fluid2DInjectorMotion::TwoRings:
    case Fluid2DInjectorMotion::Lissajous:
        return 4.2F;
    }
    return 4.2F;
}

[[nodiscard]] float max_speed_for_motion(Fluid2DInjectorMotion motion) {
    switch (motion) {
    case Fluid2DInjectorMotion::SameDirectionOrbits:
    case Fluid2DInjectorMotion::AlternatingDirectionOrbits:
        return 0.46F;
    case Fluid2DInjectorMotion::OneRing:
    case Fluid2DInjectorMotion::TwoRings:
    case Fluid2DInjectorMotion::Lissajous:
        return 0.50F;
    }
    return 0.50F;
}

[[nodiscard]] std::array<float, 2>
injector_acceleration(const std::vector<Fluid2DInjectorState>& injectors,
                      const Fluid2DConfig& config, std::size_t index, float time) {
    const Fluid2DInjectorState& injector = injectors[index];
    const std::array<float, 2> target = target_for_injector(config, injector, time);
    const float speed_scale = injector_speed_scale(config);
    std::array<float, 2> acceleration =
        add(scale(subtract(target, injector.position),
                  stiffness_for_motion(config.injector_motion)),
            scale(tangent_for_injector(config, injector, time),
                  tangent_drive_for_motion(config.injector_motion) * speed_scale));
    add_to(acceleration, scale(injector.velocity, -damping_for_motion(config.injector_motion)));

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

[[nodiscard]] bool two_ring_inner(const Fluid2DConfig& config, std::uint32_t index) {
    if (config.injector_motion != Fluid2DInjectorMotion::TwoRings ||
        config.procedural_injector_count < 2U) {
        return false;
    }
    const std::uint32_t outer_count = (config.procedural_injector_count + 1U) / 2U;
    return index >= outer_count;
}

[[nodiscard]] float initial_anchor_angle(const Fluid2DConfig& config, std::uint32_t index) {
    if (config.injector_motion != Fluid2DInjectorMotion::TwoRings ||
        config.procedural_injector_count < 2U) {
        const float hue = static_cast<float>(index) /
                          static_cast<float>(config.procedural_injector_count);
        return hue * kTau;
    }

    const std::uint32_t outer_count = (config.procedural_injector_count + 1U) / 2U;
    const bool inner = two_ring_inner(config, index);
    const std::uint32_t ring_count =
        inner ? config.procedural_injector_count - outer_count : outer_count;
    const std::uint32_t ring_index = inner ? index - outer_count : index;
    const float ring_angle =
        (static_cast<float>(ring_index) / static_cast<float>(ring_count)) * kTau;
    const float inner_offset = inner ? (0.5F / static_cast<float>(ring_count)) * kTau : 0.0F;
    return ring_angle + inner_offset;
}

[[nodiscard]] float initial_orbit_radius(const Fluid2DConfig& config, std::uint32_t index) {
    if (config.injector_motion == Fluid2DInjectorMotion::OneRing) {
        return 0.31F;
    }
    if (two_ring_inner(config, index)) {
        return 0.13F;
    }
    if (config.injector_motion == Fluid2DInjectorMotion::TwoRings) {
        return 0.34F;
    }
    if (varied_orbit_motion(config.injector_motion)) {
        return varied_orbit_radius(config, index);
    }
    return 0.0F;
}

[[nodiscard]] float initial_orbit_direction(const Fluid2DConfig& config, std::uint32_t index) {
    if (config.injector_motion == Fluid2DInjectorMotion::TwoRings && two_ring_inner(config, index)) {
        return -1.0F;
    }
    if (config.injector_motion == Fluid2DInjectorMotion::AlternatingDirectionOrbits &&
        (index % 2U) == 1U) {
        return -1.0F;
    }
    return 1.0F;
}

[[nodiscard]] std::array<float, 2> initial_velocity_for_injector(
    const Fluid2DConfig& config, const Fluid2DInjectorState& injector) {
    switch (config.injector_motion) {
    case Fluid2DInjectorMotion::OneRing:
    case Fluid2DInjectorMotion::TwoRings:
        return scale(
            {
                -std::sin(injector.anchor_angle) * injector.orbit_direction * 0.070F,
                std::cos(injector.anchor_angle) * injector.orbit_direction * 0.070F,
            },
            injector_speed_scale(config));
    case Fluid2DInjectorMotion::SameDirectionOrbits:
    case Fluid2DInjectorMotion::AlternatingDirectionOrbits:
    case Fluid2DInjectorMotion::Lissajous:
        return scale(tangent_for_injector(config, injector, 0.0F),
                     0.075F * injector_speed_scale(config));
    }
    return {};
}

[[nodiscard]] InjectorGpuTuning gpu_tuning_for_injector(const Fluid2DConfig& config,
                                                        const Fluid2DInjectorState& injector) {
    switch (config.injector_motion) {
    case Fluid2DInjectorMotion::OneRing:
        return {.radius_scale = 0.86F, .strength_scale = 0.88F,
                .carry_scale = 6.0F, .propulsion_scale = 1.25F};
    case Fluid2DInjectorMotion::TwoRings:
        if (injector.orbit_radius < 0.2F) {
            return {.radius_scale = 0.92F, .strength_scale = 0.90F,
                    .carry_scale = 6.0F, .propulsion_scale = 1.15F};
        }
        return {.radius_scale = 0.84F, .strength_scale = 0.84F,
                .carry_scale = 6.0F, .propulsion_scale = 1.35F};
    case Fluid2DInjectorMotion::SameDirectionOrbits:
    case Fluid2DInjectorMotion::AlternatingDirectionOrbits:
        return {.radius_scale = 0.82F, .strength_scale = 0.86F,
                .carry_scale = 5.5F, .propulsion_scale = 1.15F};
    case Fluid2DInjectorMotion::Lissajous:
        return {.radius_scale = 0.84F, .strength_scale = 0.88F,
                .carry_scale = 6.0F, .propulsion_scale = 1.25F};
    }
    return {};
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
        const float angle = initial_anchor_angle(config, index);
        Fluid2DInjectorState injector{
            .position = {},
            .velocity = {},
            .hue = hue,
            .anchor_angle = angle,
            .orbit_radius = initial_orbit_radius(config, index),
            .orbit_direction = initial_orbit_direction(config, index),
            .motion = config.injector_motion,
            .seed = index,
        };
        injector.position = target_for_injector(config, injector, 0.0F);
        injector.velocity = initial_velocity_for_injector(config, injector);
        injectors.push_back(injector);
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
        const InjectorGpuTuning tuning = gpu_tuning_for_injector(config, injector);
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

std::vector<Fluid2DInjectorGpu>
update_fluid_2d_injectors(std::vector<Fluid2DInjectorState>& injectors,
                          const Fluid2DConfig& config, const cubey::FrameTiming& timing) {
    const bool stale_motion = std::any_of(
        injectors.begin(), injectors.end(), [&config](const Fluid2DInjectorState& injector) {
            return injector.motion != config.injector_motion;
        });
    if (injectors.size() != config.procedural_injector_count || stale_motion) {
        injectors = create_fluid_2d_injectors(config);
    }

    const float dt =
        std::min(static_cast<float>(timing.delta_seconds), config.fixed_delta_seconds);
    const float time = static_cast<float>(timing.elapsed_seconds);
    for (std::size_t index = 0; index < injectors.size(); ++index) {
        const std::array<float, 2> acceleration =
            injector_acceleration(injectors, config, index, time);
        add_to(injectors[index].velocity, scale(acceleration, dt));
        clamp_speed(injectors[index].velocity,
                    max_speed_for_motion(config.injector_motion) * injector_speed_scale(config));
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

std::size_t fluid_2d_injector_capacity_byte_size() {
    return sizeof(Fluid2DInjectorGpu) *
           static_cast<std::size_t>(kMaxProceduralInjectorCount);
}

} // namespace cubey::projects::fluid_2d
