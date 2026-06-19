#include <cubey/procedural/patch_domain.h>

#include <cubey/procedural/hash.h>
#include <cubey/procedural/seed.h>

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

} // namespace

void validate_patch_domain(const PatchDomain2D& domain) {
    if (domain.interior_grid.width == 0U || domain.interior_grid.height == 0U) {
        throw std::runtime_error("procedural patch dimensions must be non-zero");
    }
    (void)expanded_dimension(domain.interior_grid.width, domain.border_samples);
    (void)expanded_dimension(domain.interior_grid.height, domain.border_samples);
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
