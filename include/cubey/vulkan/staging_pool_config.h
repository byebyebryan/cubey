#pragma once

#include <vulkan/vulkan.h>

namespace cubey::vulkan {

inline constexpr VkDeviceSize kDefaultGpuStagingInitialBlockByteSize = 32ULL * 1024ULL * 1024ULL;
inline constexpr VkDeviceSize kDefaultGpuStagingGrowthBlockByteSize = 32ULL * 1024ULL * 1024ULL;
inline constexpr VkDeviceSize kDefaultGpuStagingMaxTotalByteSize = 128ULL * 1024ULL * 1024ULL;
inline constexpr VkDeviceSize kDefaultGpuUploadStepByteCap = 8ULL * 1024ULL * 1024ULL;

struct GpuStagingPoolConfig {
    VkDeviceSize initial_block_byte_size = kDefaultGpuStagingInitialBlockByteSize;
    VkDeviceSize growth_block_byte_size = kDefaultGpuStagingGrowthBlockByteSize;
    VkDeviceSize max_total_byte_size = kDefaultGpuStagingMaxTotalByteSize;
};

} // namespace cubey::vulkan
