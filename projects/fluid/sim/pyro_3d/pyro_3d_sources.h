#pragma once

#include "pyro_3d_config.h"

#include <cubey/core/frame_clock.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cubey::projects::fluid::pyro_3d {

struct Pyro3DSourceState {
    std::array<float, 3> position{};
    std::array<float, 3> velocity{};
    std::array<float, 4> material_amount{};
    float radius = 0.0F;
};

struct Pyro3DSourceGpu {
    std::array<float, 4> position_radius{};
    std::array<float, 4> velocity_strength{};
    std::array<float, 4> material_amount{};
};

static_assert(sizeof(Pyro3DSourceGpu) == sizeof(float) * 12U);

[[nodiscard]] std::vector<Pyro3DSourceState> create_pyro_3d_sources(
    const Pyro3DConfig& config);
[[nodiscard]] std::vector<Pyro3DSourceGpu>
pyro_3d_sources_to_gpu(const std::vector<Pyro3DSourceState>& sources,
                        const Pyro3DConfig& config, float emission_scale = 1.0F);
[[nodiscard]] std::vector<Pyro3DSourceGpu>
update_pyro_3d_sources(std::vector<Pyro3DSourceState>& sources, const Pyro3DConfig& config,
                        const FrameTiming& timing);
[[nodiscard]] std::size_t pyro_3d_source_byte_size(const Pyro3DConfig& config);
[[nodiscard]] std::size_t pyro_3d_source_capacity_byte_size();

} // namespace cubey::projects::fluid::pyro_3d
