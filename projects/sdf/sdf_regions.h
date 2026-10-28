#pragma once
#include "sdf_authored.h"
#include <cubey/vulkan/buffer.h>

namespace cubey::projects::sdf {
inline constexpr std::size_t region_reference_limit = 32U * 1024U * 1024U;
[[nodiscard]] float authored_evaluation_guard(const AuthoredScene& scene);
// Disposable, exact-distance postfix references. High bit complements a subtree.
// Appearance continues to use the complete authored program.
struct RegionPrograms {
    struct Header {
        std::uint32_t offset, count;
    };
    std::vector<Header> headers;
    std::vector<std::uint32_t> references;
    std::uint32_t axis = 0, fallback_cells = 0;
    std::uint64_t scratch_peak_bytes = 0;
    float arithmetic_guard = 0;
    double build_ms = 0;
};
[[nodiscard]] RegionPrograms
build_region_programs(const AuthoredScene& scene, std::uint32_t axis,
                      std::size_t reference_limit = region_reference_limit);
[[nodiscard]] float region_distance(math::Vec3 p, const AuthoredScene& scene,
                                    const RegionPrograms& regions);
struct GpuRegionPrograms {
    GpuRegionPrograms(vulkan::Device& device, const AuthoredScene& scene, std::uint32_t axis,
                      std::size_t reference_limit = region_reference_limit,
                      bool device_local = false);
    std::unique_ptr<vulkan::Buffer> cells, references;
    std::uint64_t geometry_hash = 0, allocation_bytes = 0, scratch_peak_bytes = 0;
    std::uint32_t axis = 0, fallback_cells = 0;
    std::size_t reference_count = 0;
    double build_upload_ms = 0;
};
} // namespace cubey::projects::sdf
