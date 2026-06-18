#include <cubey/procedural/sample_domain.h>

#include <stdexcept>

namespace cubey::procedural {

void validate_sample_domain(const SampleDomain2D& domain) {
    if (domain.grid.width == 0U || domain.grid.height == 0U) {
        throw std::runtime_error("procedural 2D sample domain dimensions must be non-zero");
    }
}

void validate_sample_domain(const SampleDomain3D& domain) {
    if (domain.width == 0U || domain.height == 0U || domain.depth == 0U) {
        throw std::runtime_error("procedural 3D sample domain dimensions must be non-zero");
    }
}

std::size_t sample_count(const SampleDomain2D& domain) {
    validate_sample_domain(domain);
    return sample_count(domain.grid);
}

std::size_t sample_count(const SampleDomain3D& domain) {
    validate_sample_domain(domain);
    return static_cast<std::size_t>(domain.width) * static_cast<std::size_t>(domain.height) *
           static_cast<std::size_t>(domain.depth);
}

std::size_t sample_index(const SampleDomain3D& domain, std::uint32_t x, std::uint32_t y,
                         std::uint32_t z) {
    validate_sample_domain(domain);
    if (x >= domain.width || y >= domain.height || z >= domain.depth) {
        throw std::runtime_error("procedural 3D sample domain index is out of bounds");
    }
    return (static_cast<std::size_t>(z) * static_cast<std::size_t>(domain.height) *
            static_cast<std::size_t>(domain.width)) +
           (static_cast<std::size_t>(y) * static_cast<std::size_t>(domain.width)) + x;
}

float sample_domain_x(const SampleDomain3D& domain, std::uint32_t x) {
    validate_sample_domain(domain);
    return domain.origin_x + grid_centered_offset(x, domain.width, domain.cell_size);
}

float sample_domain_y(const SampleDomain3D& domain, std::uint32_t y) {
    validate_sample_domain(domain);
    return domain.origin_y + grid_centered_offset(y, domain.height, domain.cell_size);
}

float sample_domain_z(const SampleDomain3D& domain, std::uint32_t z) {
    validate_sample_domain(domain);
    return domain.origin_z + grid_centered_offset(z, domain.depth, domain.cell_size);
}

} // namespace cubey::procedural
