#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <span>
#include <vector>

namespace cubey::vulkan::detail {

inline constexpr VkDeviceSize kDeviceBufferUploadStagingChunkByteSize = 32ULL * 1024ULL * 1024ULL;

struct DeviceBufferUploadCopyPiece {
    std::size_t upload_index = 0;
    VkDeviceSize source_offset = 0;
    VkDeviceSize destination_offset = 0;
    VkDeviceSize byte_size = 0;
};

struct DeviceBufferUploadChunk {
    VkDeviceSize staging_byte_size = 0;
    std::vector<DeviceBufferUploadCopyPiece> pieces;
};

struct DeviceBufferUploadPlan {
    VkDeviceSize uploaded_byte_count = 0;
    std::vector<DeviceBufferUploadChunk> chunks;
};

[[nodiscard]] DeviceBufferUploadPlan plan_device_buffer_uploads(
    std::span<const VkDeviceSize> upload_byte_sizes,
    VkDeviceSize staging_chunk_byte_size = kDeviceBufferUploadStagingChunkByteSize);

} // namespace cubey::vulkan::detail
