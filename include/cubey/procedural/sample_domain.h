#pragma once

#include <cubey/procedural/field_2d.h>

#include <cstddef>
#include <cstdint>

namespace cubey::procedural {

enum class ProceduralDomainSpace {
    Local,
    World,
    Unit,
    Atlas,
    Volume,
    Spherical,
};

struct SampleDomain2D {
    Grid2DDesc grid{};
    std::uint64_t seed = 0;
    ProceduralDomainSpace space = ProceduralDomainSpace::Local;
};

struct SampleDomain3D {
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    std::uint32_t depth = 1;
    float cell_size = 1.0F;
    float origin_x = 0.0F;
    float origin_y = 0.0F;
    float origin_z = 0.0F;
    std::uint64_t seed = 0;
    ProceduralDomainSpace space = ProceduralDomainSpace::Volume;
};

void validate_sample_domain(const SampleDomain2D& domain);
void validate_sample_domain(const SampleDomain3D& domain);
[[nodiscard]] std::size_t sample_count(const SampleDomain2D& domain);
[[nodiscard]] std::size_t sample_count(const SampleDomain3D& domain);
[[nodiscard]] std::size_t sample_index(const SampleDomain3D& domain, std::uint32_t x,
                                       std::uint32_t y, std::uint32_t z);
[[nodiscard]] float sample_domain_x(const SampleDomain3D& domain, std::uint32_t x);
[[nodiscard]] float sample_domain_y(const SampleDomain3D& domain, std::uint32_t y);
[[nodiscard]] float sample_domain_z(const SampleDomain3D& domain, std::uint32_t z);

} // namespace cubey::procedural
