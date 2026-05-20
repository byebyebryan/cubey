#include "fluid_3d_sources.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace cubey::projects::fluid_3d {
namespace {

constexpr float kTau = 6.28318530718F;
constexpr std::array<float, 3> kPlumeCenter{0.5F, 0.10F, 0.5F};

[[nodiscard]] std::array<float, 3> add(std::array<float, 3> lhs,
                                       std::array<float, 3> rhs) {
    return {lhs[0] + rhs[0], lhs[1] + rhs[1], lhs[2] + rhs[2]};
}

[[nodiscard]] std::array<float, 3> scale(std::array<float, 3> value, float amount) {
    return {value[0] * amount, value[1] * amount, value[2] * amount};
}

[[nodiscard]] float length(std::array<float, 3> value) {
    return std::sqrt((value[0] * value[0]) + (value[1] * value[1]) + (value[2] * value[2]));
}

[[nodiscard]] std::array<float, 3> normalize_or_zero(std::array<float, 3> value) {
    const float vector_length = length(value);
    if (vector_length <= 0.00001F) {
        return {};
    }
    return {value[0] / vector_length, value[1] / vector_length, value[2] / vector_length};
}

[[nodiscard]] std::array<float, 3> plume_disk_offset(std::uint32_t index,
                                                     std::uint32_t count) {
    if (count <= 1U) {
        return {};
    }
    constexpr float kGoldenAngle = 2.39996323F;
    const float rank = static_cast<float>(index + 1U);
    const float normalized_count = static_cast<float>(count);
    const float radius = 0.075F * std::sqrt(rank / normalized_count);
    const float angle = kGoldenAngle * rank;
    return {std::cos(angle) * radius, 0.0F, std::sin(angle) * radius};
}

[[nodiscard]] Fluid3DSourceState create_plume_source(const Fluid3DConfig& config,
                                                     std::uint32_t index) {
    const std::array<float, 3> offset = plume_disk_offset(index, config.source_count);
    const std::array<float, 3> swirl_direction = normalize_or_zero({-offset[2], 0.0F, offset[0]});
    const float source_index = static_cast<float>(index);
    const std::array<float, 3> velocity =
        normalize_or_zero(add({0.0F, 1.0F, 0.0F},
                              scale(swirl_direction,
                                    0.18F + 0.08F * std::sin(source_index * kTau * 0.37F))));
    return {
        .position = add(kPlumeCenter, offset),
        .velocity = velocity,
        .material_amount = {config.source_smoke_amount, config.source_heat_amount, 0.0F},
        .radius = config.source_radius,
    };
}

[[nodiscard]] Fluid3DSourceState create_source(const Fluid3DConfig& config, std::uint32_t index) {
    switch (config.scenario) {
    case Fluid3DScenario::SmokePlume:
        return create_plume_source(config, index);
    }
    return create_plume_source(config, index);
}

void validate_source_count(const Fluid3DConfig& config) {
    if (config.source_count == 0 || config.source_count > kMaxFluid3DSourceCount) {
        throw std::runtime_error("fluid 3D source count must be 1..16");
    }
}

} // namespace

std::vector<Fluid3DSourceState> create_fluid_3d_sources(const Fluid3DConfig& config) {
    validate_source_count(config);
    std::vector<Fluid3DSourceState> sources;
    sources.reserve(config.source_count);
    for (std::uint32_t index = 0; index < config.source_count; ++index) {
        sources.push_back(create_source(config, index));
    }
    return sources;
}

std::vector<Fluid3DSourceGpu>
fluid_3d_sources_to_gpu(const std::vector<Fluid3DSourceState>& sources,
                        const Fluid3DConfig& config) {
    std::vector<Fluid3DSourceGpu> gpu;
    gpu.reserve(sources.size());
    for (const Fluid3DSourceState& source : sources) {
        gpu.push_back({
            .position_radius =
                {
                    source.position[0],
                    source.position[1],
                    source.position[2],
                    source.radius,
                },
            .velocity_strength =
                {
                    source.velocity[0],
                    source.velocity[1],
                    source.velocity[2],
                    config.source_velocity_strength,
                },
            .material_amount =
                {
                    source.material_amount[0],
                    source.material_amount[1],
                    source.material_amount[2],
                    0.0F,
                },
        });
    }
    return gpu;
}

std::vector<Fluid3DSourceGpu>
update_fluid_3d_sources(std::vector<Fluid3DSourceState>& sources, const Fluid3DConfig& config,
                        const FrameTiming& timing) {
    (void)timing;
    if (sources.size() != config.source_count) {
        sources = create_fluid_3d_sources(config);
    }
    for (std::uint32_t index = 0; index < config.source_count; ++index) {
        sources[index] = create_source(config, index);
    }
    return fluid_3d_sources_to_gpu(sources, config);
}

std::size_t fluid_3d_source_byte_size(const Fluid3DConfig& config) {
    validate_source_count(config);
    return sizeof(Fluid3DSourceGpu) * static_cast<std::size_t>(config.source_count);
}

std::size_t fluid_3d_source_capacity_byte_size() {
    return sizeof(Fluid3DSourceGpu) * static_cast<std::size_t>(kMaxFluid3DSourceCount);
}

} // namespace cubey::projects::fluid_3d
