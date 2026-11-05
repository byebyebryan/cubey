#pragma once
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace cubey::projects::raytracing {
inline std::uint64_t align_address(std::uint64_t value, std::uint64_t alignment) {
    if (alignment == 0 || (alignment & (alignment - 1)) ||
        value > std::numeric_limits<std::uint64_t>::max() - (alignment - 1))
        throw std::invalid_argument("invalid or overflowing RT alignment");
    return (value + alignment - 1) & ~(alignment - 1);
}
inline std::uint64_t sbt_stride(std::uint32_t handle_size, std::uint32_t handle_alignment,
                                std::uint32_t base_alignment, std::uint32_t maximum_stride) {
    if (!handle_size)
        throw std::invalid_argument("empty SBT handle");
    auto stride = align_address(align_address(handle_size, handle_alignment), base_alignment);
    if (stride > maximum_stride)
        throw std::invalid_argument("SBT stride exceeds device limit");
    return stride;
}
} // namespace cubey::projects::raytracing
