#include "fluid_3d_injectors.h"

#include <cubey/render/color_space.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cubey::projects::fluid_3d {
namespace {

constexpr float kTau = 6.28318530718F;
constexpr std::array<float, 3> kCenter{0.5F, 0.5F, 0.5F};

[[nodiscard]] float length(std::array<float, 3> value) {
    return std::sqrt((value[0] * value[0]) + (value[1] * value[1]) + (value[2] * value[2]));
}

[[nodiscard]] std::array<float, 3> subtract(std::array<float, 3> lhs, std::array<float, 3> rhs) {
    return {lhs[0] - rhs[0], lhs[1] - rhs[1], lhs[2] - rhs[2]};
}

[[nodiscard]] std::array<float, 3> scale(std::array<float, 3> value, float amount) {
    return {value[0] * amount, value[1] * amount, value[2] * amount};
}

[[nodiscard]] float injector_t(const Fluid3DConfig& config, std::uint32_t index) {
    if (config.injector_count <= 1U) {
        return 0.0F;
    }
    return static_cast<float>(index) / static_cast<float>(config.injector_count - 1U);
}

[[nodiscard]] std::array<float, 3> injector_position(const Fluid3DInjectorState& injector,
                                                     float time) {
    const float angle = injector.phase + (injector.speed * time);
    const float vertical = 0.14F * std::sin((angle * 0.73F) + injector.phase);
    return {
        kCenter[0] + (std::cos(angle) * injector.radius),
        kCenter[1] + vertical,
        kCenter[2] + (std::sin(angle) * injector.radius),
    };
}

} // namespace

std::vector<Fluid3DInjectorState> create_fluid_3d_injectors(const Fluid3DConfig& config) {
    if (config.injector_count == 0 || config.injector_count > kMaxFluid3DInjectorCount) {
        throw std::runtime_error("fluid 3D injector count must be 1..16");
    }
    std::vector<Fluid3DInjectorState> injectors;
    injectors.reserve(config.injector_count);
    for (std::uint32_t index = 0; index < config.injector_count; ++index) {
        const float t = injector_t(config, index);
        const float hue = static_cast<float>(index) / static_cast<float>(config.injector_count);
        Fluid3DInjectorState injector{
            .position = {},
            .velocity = {},
            .color =
                cubey::render::hsv_to_linear_rgb({.hue = hue, .saturation = 0.82F, .value = 1.0F}),
            .phase = t * kTau,
            .radius = 0.18F + (0.16F * t),
            .speed = -0.42F + (0.84F * t),
        };
        if (std::abs(injector.speed) < 0.08F) {
            injector.speed = injector.speed < 0.0F ? -0.08F : 0.08F;
        }
        injector.position = injector_position(injector, 0.0F);
        injector.velocity = scale(subtract(injector_position(injector, 0.25F), injector.position),
                                  config.injector_velocity_scale / 0.25F);
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
                    injector.velocity[0],
                    injector.velocity[1],
                    injector.velocity[2],
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
    const float dt = std::max(static_cast<float>(timing.delta_seconds), 0.0001F);
    for (Fluid3DInjectorState& injector : injectors) {
        const std::array<float, 3> previous = injector.position;
        injector.position = injector_position(injector, time);
        injector.velocity =
            scale(subtract(injector.position, previous), config.injector_velocity_scale / dt);
        const float velocity_length = length(injector.velocity);
        if (velocity_length > 1.2F) {
            injector.velocity = scale(injector.velocity, 1.2F / velocity_length);
        }
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
