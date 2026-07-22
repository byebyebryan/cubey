#include <cubey/vulkan/detail/buffer_upload_plan.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace cubey::vulkan::detail {
namespace {

constexpr VkDeviceSize kBufferCopyOffsetAlignment = 4;

VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

} // namespace

DeviceBufferUploadPlan plan_device_buffer_uploads(std::span<const VkDeviceSize> upload_byte_sizes,
                                                  VkDeviceSize staging_chunk_byte_size) {
    if (staging_chunk_byte_size == 0 || staging_chunk_byte_size % kBufferCopyOffsetAlignment != 0) {
        throw std::runtime_error(
            "device buffer upload staging chunk size must be positive and copy-aligned");
    }

    DeviceBufferUploadPlan result;
    for (const VkDeviceSize byte_size : upload_byte_sizes) {
        if (byte_size == 0) {
            throw std::runtime_error("device buffer upload size must be positive");
        }
        if (byte_size > std::numeric_limits<VkDeviceSize>::max() - result.uploaded_byte_count) {
            throw std::runtime_error("device buffer upload batch is too large");
        }
        result.uploaded_byte_count += byte_size;
    }

    std::size_t upload_index = 0;
    VkDeviceSize upload_offset = 0;
    while (upload_index < upload_byte_sizes.size()) {
        DeviceBufferUploadChunk chunk;
        while (upload_index < upload_byte_sizes.size()) {
            const VkDeviceSize source_offset =
                align_up(chunk.staging_byte_size, kBufferCopyOffsetAlignment);
            if (source_offset >= staging_chunk_byte_size) {
                break;
            }

            const VkDeviceSize available = staging_chunk_byte_size - source_offset;
            const VkDeviceSize remaining = upload_byte_sizes[upload_index] - upload_offset;
            VkDeviceSize piece_byte_size = std::min(available, remaining);
            if (piece_byte_size < remaining) {
                piece_byte_size -= piece_byte_size % kBufferCopyOffsetAlignment;
            }
            if (piece_byte_size == 0) {
                break;
            }

            chunk.pieces.push_back({
                .upload_index = upload_index,
                .source_offset = source_offset,
                .destination_offset = upload_offset,
                .byte_size = piece_byte_size,
            });
            chunk.staging_byte_size = source_offset + piece_byte_size;
            upload_offset += piece_byte_size;
            if (upload_offset == upload_byte_sizes[upload_index]) {
                ++upload_index;
                upload_offset = 0;
            }
        }

        if (chunk.pieces.empty()) {
            throw std::runtime_error("device buffer upload batch could not make progress");
        }
        if (result.chunks.size() >= std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("device buffer upload batch requires too many chunks");
        }
        result.chunks.push_back(std::move(chunk));
    }
    return result;
}

} // namespace cubey::vulkan::detail
