#include <cubey/procedural/patch_domain.h>

#include <cubey/procedural/seed.h>

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace cubey::procedural {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void append_byte(std::uint64_t& hash, std::uint8_t value) {
    hash ^= value;
    hash *= kFnvPrime;
}

void append_u32(std::uint64_t& hash, std::uint32_t value) {
    for (std::uint32_t byte_index = 0; byte_index < 4U; ++byte_index) {
        append_byte(hash, static_cast<std::uint8_t>((value >> (byte_index * 8U)) & 0xffU));
    }
}

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
    std::uint64_t hash = kFnvOffset;
    append_u32(hash, 0x6375'6265U);
    append_u32(hash, address.level);
    append_u32(hash, static_cast<std::uint32_t>(address.x));
    append_u32(hash, static_cast<std::uint32_t>(address.y));
    return hash;
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
