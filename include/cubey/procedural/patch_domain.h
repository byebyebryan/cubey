#pragma once

#include <cubey/procedural/sample_domain.h>

#include <cstdint>
#include <string_view>

namespace cubey::procedural {

struct PatchAddress2D {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t level = 0;
};

struct PatchDomain2D {
    PatchAddress2D address{};
    Grid2DDesc interior_grid{};
    std::uint32_t border_samples = 0;
    std::uint64_t seed = 0;
    ProceduralDomainSpace space = ProceduralDomainSpace::World;
};

void validate_patch_domain(const PatchDomain2D& domain);
[[nodiscard]] std::uint64_t patch_address_hash(PatchAddress2D address);
[[nodiscard]] std::uint64_t derive_patch_seed(std::uint64_t base_seed,
                                              std::string_view domain,
                                              PatchAddress2D address);
[[nodiscard]] Grid2DDesc patch_sample_grid(const PatchDomain2D& domain);
[[nodiscard]] SampleDomain2D patch_sample_domain(const PatchDomain2D& domain);

} // namespace cubey::procedural
