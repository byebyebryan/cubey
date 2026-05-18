#pragma once

#include "fluid_2d_config.h"

#include <cubey/core/frame_clock.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cubey::projects::fluid_2d {

struct Fluid2DInjectorState {
    std::array<float, 2> position{};
    std::array<float, 2> velocity{};
    float hue = 0.0F;
    float anchor_angle = 0.0F;
    float orbit_radius = 0.0F;
    float orbit_direction = 1.0F;
    Fluid2DInjectorMotion motion = Fluid2DInjectorMotion::TwoRings;
    std::uint32_t seed = 0;
};

struct Fluid2DInjectorGpu {
    std::array<float, 4> position_radius_strength{};
    std::array<float, 4> velocity_carry_propulsion{};
    std::array<float, 4> dye_active{};
};

static_assert(sizeof(Fluid2DInjectorGpu) == sizeof(float) * 12U);

[[nodiscard]] std::vector<Fluid2DInjectorState>
create_fluid_2d_injectors(const Fluid2DConfig& config);
[[nodiscard]] std::vector<Fluid2DInjectorGpu>
fluid_2d_injectors_to_gpu(const std::vector<Fluid2DInjectorState>& injectors,
                          const Fluid2DConfig& config);
[[nodiscard]] std::vector<Fluid2DInjectorGpu>
update_fluid_2d_injectors(std::vector<Fluid2DInjectorState>& injectors,
                          const Fluid2DConfig& config, const cubey::FrameTiming& timing);
[[nodiscard]] std::size_t fluid_2d_injector_byte_size(const Fluid2DConfig& config);
[[nodiscard]] std::size_t fluid_2d_injector_capacity_byte_size();

} // namespace cubey::projects::fluid_2d
