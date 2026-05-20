#pragma once

#include "fluid_3d_config.h"

#include <cubey/core/frame_clock.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cubey::projects::fluid_3d {

struct Fluid3DSourceState {
    std::array<float, 3> position{};
    std::array<float, 3> velocity{};
    std::array<float, 3> material_amount{};
    float radius = 0.0F;
};

struct Fluid3DSourceGpu {
    std::array<float, 4> position_radius{};
    std::array<float, 4> velocity_strength{};
    std::array<float, 4> material_amount{};
};

static_assert(sizeof(Fluid3DSourceGpu) == sizeof(float) * 12U);

[[nodiscard]] std::vector<Fluid3DSourceState> create_fluid_3d_sources(
    const Fluid3DConfig& config);
[[nodiscard]] std::vector<Fluid3DSourceGpu>
fluid_3d_sources_to_gpu(const std::vector<Fluid3DSourceState>& sources,
                        const Fluid3DConfig& config, float emission_scale = 1.0F);
[[nodiscard]] std::vector<Fluid3DSourceGpu>
update_fluid_3d_sources(std::vector<Fluid3DSourceState>& sources, const Fluid3DConfig& config,
                        const FrameTiming& timing);
[[nodiscard]] std::size_t fluid_3d_source_byte_size(const Fluid3DConfig& config);
[[nodiscard]] std::size_t fluid_3d_source_capacity_byte_size();

} // namespace cubey::projects::fluid_3d
