#include <cubey/vulkan/buffer.h>

#include <cubey/vulkan/detail/buffer_upload_plan.h>
#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/vk_check.h>

#include <cstddef>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cubey::vulkan {
namespace {

detail::DeviceBufferUploadPlan
validate_and_plan_device_buffer_uploads(std::span<const DeviceBufferUpload> uploads) {
    std::vector<VkDeviceSize> upload_byte_sizes;
    upload_byte_sizes.reserve(uploads.size());
    for (const DeviceBufferUpload& upload : uploads) {
        if (upload.data == nullptr) {
            throw std::runtime_error("device buffer upload requires data");
        }
        if (upload.usage == 0) {
            throw std::runtime_error("device buffer upload usage must be nonzero");
        }
        upload_byte_sizes.push_back(upload.byte_size);
    }
    return detail::plan_device_buffer_uploads(upload_byte_sizes);
}

} // namespace

Buffer::Buffer(const Device& device, const BufferConfig& config)
    : physical_device_(device.physical_device()), device_(device.handle()) {
    if (physical_device_ == VK_NULL_HANDLE || device_ == VK_NULL_HANDLE) {
        throw std::runtime_error("buffer creation requires a valid Vulkan device");
    }

    try {
        create(config);
    } catch (...) {
        destroy();
        throw;
    }
}

Buffer::~Buffer() {
    destroy();
}

Buffer::Buffer(Buffer&& other) noexcept {
    move_from(other);
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        destroy();
        move_from(other);
    }
    return *this;
}

void Buffer::upload(const void* data, VkDeviceSize byte_size, VkDeviceSize offset) const {
    if (data == nullptr) {
        throw std::runtime_error("buffer upload requires data");
    }
    if ((memory_properties_ & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0 ||
        (memory_properties_ & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
        throw std::runtime_error("buffer upload requires host-visible coherent memory");
    }
    if (byte_size == 0 || offset > size_ || byte_size > size_ - offset) {
        throw std::runtime_error("buffer upload range is outside the buffer");
    }

    void* mapped = nullptr;
    check(vkMapMemory(device_, memory_, offset, byte_size, 0, &mapped), "vkMapMemory buffer");
    std::memcpy(mapped, data, static_cast<std::size_t>(byte_size));
    vkUnmapMemory(device_, memory_);
}

void Buffer::download(void* data, VkDeviceSize byte_size, VkDeviceSize offset) const {
    if (data == nullptr) {
        throw std::runtime_error("buffer download requires destination data");
    }
    if ((memory_properties_ & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0 ||
        (memory_properties_ & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
        throw std::runtime_error("buffer download requires host-visible coherent memory");
    }
    if (byte_size == 0 || offset > size_ || byte_size > size_ - offset) {
        throw std::runtime_error("buffer download range is outside the buffer");
    }

    void* mapped = nullptr;
    check(vkMapMemory(device_, memory_, offset, byte_size, 0, &mapped), "vkMapMemory buffer");
    std::memcpy(data, mapped, static_cast<std::size_t>(byte_size));
    vkUnmapMemory(device_, memory_);
}

void Buffer::create(const BufferConfig& config) {
    if (config.size == 0) {
        throw std::runtime_error("buffer size must be positive");
    }
    if (config.usage == 0) {
        throw std::runtime_error("buffer usage must be nonzero");
    }
    if (config.memory_properties == 0) {
        throw std::runtime_error("buffer memory properties must be nonzero");
    }

    size_ = config.size;
    memory_properties_ = config.memory_properties;

    auto buffer_info = vk_struct<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    buffer_info.size = size_;
    buffer_info.usage = config.usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(device_, &buffer_info, nullptr, &buffer_), "vkCreateBuffer");

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, buffer_, &requirements);

    VkPhysicalDeviceMemoryProperties memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &memory_properties);

    std::uint32_t memory_type_index = UINT32_MAX;
    for (std::uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        const bool type_matches = (requirements.memoryTypeBits & (1U << i)) != 0;
        const bool flags_match = (memory_properties.memoryTypes[i].propertyFlags &
                                  config.memory_properties) == config.memory_properties;
        if (type_matches && flags_match) {
            memory_type_index = i;
            break;
        }
    }
    if (memory_type_index == UINT32_MAX) {
        throw std::runtime_error("no compatible Vulkan memory type found for buffer");
    }

    auto alloc = vk_struct<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    alloc.allocationSize = requirements.size;
    alloc.memoryTypeIndex = memory_type_index;
    check(vkAllocateMemory(device_, &alloc, nullptr, &memory_), "vkAllocateMemory buffer");
    check(vkBindBufferMemory(device_, buffer_, memory_, 0), "vkBindBufferMemory");
}

void Buffer::destroy() {
    if (buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
    }
    if (memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }
}

void Buffer::move_from(Buffer& other) noexcept {
    physical_device_ = other.physical_device_;
    device_ = other.device_;
    buffer_ = other.buffer_;
    memory_ = other.memory_;
    size_ = other.size_;
    memory_properties_ = other.memory_properties_;

    other.physical_device_ = VK_NULL_HANDLE;
    other.device_ = VK_NULL_HANDLE;
    other.buffer_ = VK_NULL_HANDLE;
    other.memory_ = VK_NULL_HANDLE;
    other.size_ = 0;
    other.memory_properties_ = 0;
}

BufferConfig staging_buffer_config(VkDeviceSize byte_size) {
    return {
        .size = byte_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .memory_properties =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    };
}

BufferConfig readback_buffer_config(VkDeviceSize byte_size) {
    return {
        .size = byte_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .memory_properties =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    };
}

BufferConfig device_local_buffer_config(VkDeviceSize byte_size, VkBufferUsageFlags usage) {
    return {
        .size = byte_size,
        .usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };
}

void copy_buffer(GpuOwnerContext& context, VkBuffer source, VkBuffer destination,
                 VkDeviceSize byte_size) {
    if (source == VK_NULL_HANDLE || destination == VK_NULL_HANDLE) {
        throw std::runtime_error("buffer copy requires valid source and destination buffers");
    }
    if (byte_size == 0) {
        throw std::runtime_error("buffer copy size must be positive");
    }

    ImmediateCommands commands(context);
    VkBufferCopy copy{};
    copy.size = byte_size;
    vkCmdCopyBuffer(commands.command_buffer(), source, destination, 1, &copy);
    commands.submit_and_wait();
}

Buffer upload_device_buffer(GpuOwnerContext& context, const void* data, VkDeviceSize byte_size,
                            VkBufferUsageFlags usage) {
    const DeviceBufferUpload upload{
        .data = data,
        .byte_size = byte_size,
        .usage = usage,
    };
    DeviceBufferUploadBatch batch = upload_device_buffers(context, std::span{&upload, 1U});
    return std::move(batch.buffers.front());
}

Buffer upload_device_buffer(GpuRuntime& gpu, const void* data, VkDeviceSize byte_size,
                            VkBufferUsageFlags usage) {
    const DeviceBufferUpload upload{
        .data = data,
        .byte_size = byte_size,
        .usage = usage,
    };
    DeviceBufferUploadBatch batch =
        upload_device_buffers(gpu, std::span{&upload, 1U}, "upload device buffer");
    return std::move(batch.buffers.front());
}

DeviceBufferUploadBatch upload_device_buffers(GpuOwnerContext& context,
                                              std::span<const DeviceBufferUpload> uploads) {
    const detail::DeviceBufferUploadPlan plan = validate_and_plan_device_buffer_uploads(uploads);
    DeviceBufferUploadBatch result;
    result.uploaded_byte_count = plan.uploaded_byte_count;
    if (uploads.empty()) {
        return result;
    }

    result.buffers.reserve(uploads.size());
    for (const DeviceBufferUpload& upload : uploads) {
        result.buffers.emplace_back(context.device(),
                                    device_local_buffer_config(upload.byte_size, upload.usage));
    }

    for (const detail::DeviceBufferUploadChunk& chunk : plan.chunks) {
        std::vector<std::byte> staging_bytes(static_cast<std::size_t>(chunk.staging_byte_size));
        for (const detail::DeviceBufferUploadCopyPiece& piece : chunk.pieces) {
            const DeviceBufferUpload& upload = uploads[piece.upload_index];
            const auto* source =
                static_cast<const std::byte*>(upload.data) + piece.destination_offset;
            std::memcpy(staging_bytes.data() + static_cast<std::size_t>(piece.source_offset),
                        source, static_cast<std::size_t>(piece.byte_size));
        }

        Buffer staging(context.device(), staging_buffer_config(chunk.staging_byte_size));
        staging.upload(staging_bytes.data(), chunk.staging_byte_size);

        ImmediateCommands commands(context);
        for (const detail::DeviceBufferUploadCopyPiece& piece : chunk.pieces) {
            VkBufferCopy copy{
                .srcOffset = piece.source_offset,
                .dstOffset = piece.destination_offset,
                .size = piece.byte_size,
            };
            vkCmdCopyBuffer(commands.command_buffer(), staging.handle(),
                            result.buffers[piece.upload_index].handle(), 1, &copy);
        }
        commands.submit_and_wait();
        ++result.transfer_submission_count;
    }

    return result;
}

DeviceBufferUploadBatch upload_device_buffers(GpuRuntime& gpu,
                                              std::span<const DeviceBufferUpload> uploads,
                                              std::string label) {
    const detail::DeviceBufferUploadPlan plan = validate_and_plan_device_buffer_uploads(uploads);
    if (uploads.empty()) {
        return {
            .buffers = {},
            .uploaded_byte_count = plan.uploaded_byte_count,
            .transfer_submission_count = 0,
        };
    }

    std::optional<DeviceBufferUploadBatch> uploaded;
    static_cast<void>(gpu.submit_and_wait({
        .label = std::move(label),
        .work =
            [&uploaded, uploads](GpuOwnerContext& context) {
                uploaded.emplace(upload_device_buffers(context, uploads));
            },
    }));
    return std::move(uploaded.value());
}

} // namespace cubey::vulkan
