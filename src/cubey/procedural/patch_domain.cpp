#include <cubey/procedural/patch_domain.h>

#include <cubey/procedural/hash.h>
#include <cubey/procedural/seed.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace cubey::procedural {
namespace {

std::uint32_t expanded_dimension(std::uint32_t dimension, std::uint32_t border_samples) {
    const std::uint64_t expanded = static_cast<std::uint64_t>(dimension) +
                                   (static_cast<std::uint64_t>(border_samples) * 2ULL);
    if (expanded > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("procedural patch border exceeds grid dimensions");
    }
    return static_cast<std::uint32_t>(expanded);
}

void validate_sample_axis(float origin, float cell_size, std::uint32_t dimension,
                          std::uint32_t border_samples) {
    if (!std::isfinite(origin) || !std::isfinite(cell_size) || cell_size <= 0.0F) {
        throw std::runtime_error(
            "procedural patch grid coordinates must be finite with positive spacing");
    }
    const std::uint32_t expanded = expanded_dimension(dimension, border_samples);
    const double half_extent = static_cast<double>(expanded - 1U) * 0.5 * cell_size;
    const double first = static_cast<double>(origin) - half_extent;
    const double last = static_cast<double>(origin) + half_extent;
    if (!std::isfinite(first) || !std::isfinite(last) ||
        first < -std::numeric_limits<float>::max() || first > std::numeric_limits<float>::max() ||
        last < -std::numeric_limits<float>::max() || last > std::numeric_limits<float>::max()) {
        throw std::runtime_error("procedural patch bordered grid coordinates overflow");
    }
}

} // namespace

void validate_patch_domain(const PatchDomain2D& domain) {
    if (domain.interior_grid.width == 0U || domain.interior_grid.height == 0U) {
        throw std::runtime_error("procedural patch dimensions must be non-zero");
    }
    (void)expanded_dimension(domain.interior_grid.width, domain.border_samples);
    (void)expanded_dimension(domain.interior_grid.height, domain.border_samples);
    validate_sample_axis(domain.interior_grid.origin_x, domain.interior_grid.cell_size,
                         domain.interior_grid.width, domain.border_samples);
    validate_sample_axis(domain.interior_grid.origin_y, domain.interior_grid.cell_size,
                         domain.interior_grid.height, domain.border_samples);
}

std::uint64_t patch_address_hash(PatchAddress2D address) {
    ProceduralHashBuilder hash;
    hash.append_u32(0x6375'6265U);
    hash.append_u32(address.level);
    hash.append_u32(static_cast<std::uint32_t>(address.x));
    hash.append_u32(static_cast<std::uint32_t>(address.y));
    return hash.value();
}

std::uint64_t derive_patch_seed(std::uint64_t base_seed, std::string_view domain,
                                PatchAddress2D address) {
    return derive_seed(base_seed, domain, patch_address_hash(address));
}

Grid2DDesc patch_sample_grid(const PatchDomain2D& domain) {
    validate_patch_domain(domain);
    Grid2DDesc grid = domain.interior_grid;
    grid.width = expanded_dimension(domain.interior_grid.width, domain.border_samples);
    grid.height = expanded_dimension(domain.interior_grid.height, domain.border_samples);
    return grid;
}

SampleDomain2D patch_sample_domain(const PatchDomain2D& domain) {
    return SampleDomain2D{
        .grid = patch_sample_grid(domain),
        .seed = domain.seed,
        .space = domain.space,
    };
}

} // namespace cubey::procedural
