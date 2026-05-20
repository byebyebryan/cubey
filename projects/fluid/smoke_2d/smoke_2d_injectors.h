#pragma once

#include "smoke_2d_config.h"

#include <cubey/core/frame_clock.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cubey::projects::fluid::smoke_2d {

struct Smoke2DInjectorState {
    std::array<float, 2> position{};
    std::array<float, 2> velocity{};
    float hue = 0.0F;
    float anchor_angle = 0.0F;
    float orbit_radius = 0.0F;
    float angular_speed = 0.0F;
    std::uint32_t seed = 0;
};

struct Smoke2DInjectorGpu {
    std::array<float, 4> position_radius_strength{};
    std::array<float, 4> velocity_carry_propulsion{};
    std::array<float, 4> dye_active{};
};

static_assert(sizeof(Smoke2DInjectorGpu) == sizeof(float) * 12U);

[[nodiscard]] std::vector<Smoke2DInjectorState>
create_smoke_2d_injectors(const Smoke2DConfig& config);
[[nodiscard]] std::vector<Smoke2DInjectorGpu>
smoke_2d_injectors_to_gpu(const std::vector<Smoke2DInjectorState>& injectors,
                          const Smoke2DConfig& config);
[[nodiscard]] std::vector<Smoke2DInjectorGpu>
update_smoke_2d_injectors(std::vector<Smoke2DInjectorState>& injectors, const Smoke2DConfig& config,
                          const cubey::FrameTiming& timing);
[[nodiscard]] std::size_t smoke_2d_injector_byte_size(const Smoke2DConfig& config);
[[nodiscard]] std::size_t smoke_2d_injector_capacity_byte_size();

} // namespace cubey::projects::fluid::smoke_2d
