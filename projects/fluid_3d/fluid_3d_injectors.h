#pragma once

#include "fluid_3d_config.h"

#include <cubey/core/frame_clock.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cubey::projects::fluid_3d {

struct Fluid3DInjectorState {
    std::array<float, 3> position{};
    std::array<float, 3> velocity{};
    std::array<float, 3> color{};
    float phase = 0.0F;
    float radius = 0.0F;
    float speed = 0.0F;
};

struct Fluid3DInjectorGpu {
    std::array<float, 4> position_radius{};
    std::array<float, 4> velocity_strength{};
    std::array<float, 4> color_active{};
};

static_assert(sizeof(Fluid3DInjectorGpu) == sizeof(float) * 12U);

[[nodiscard]] std::vector<Fluid3DInjectorState>
create_fluid_3d_injectors(const Fluid3DConfig& config);
[[nodiscard]] std::vector<Fluid3DInjectorGpu>
fluid_3d_injectors_to_gpu(const std::vector<Fluid3DInjectorState>& injectors,
                          const Fluid3DConfig& config);
[[nodiscard]] std::vector<Fluid3DInjectorGpu>
update_fluid_3d_injectors(std::vector<Fluid3DInjectorState>& injectors, const Fluid3DConfig& config,
                          const FrameTiming& timing);
[[nodiscard]] std::size_t fluid_3d_injector_byte_size(const Fluid3DConfig& config);
[[nodiscard]] std::size_t fluid_3d_injector_capacity_byte_size();

} // namespace cubey::projects::fluid_3d
