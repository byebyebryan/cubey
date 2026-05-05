#include <cubey/vulkan/buffer.h>

#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/vk_check.h>

#include <cstring>
#include <stdexcept>

namespace cubey::vulkan {

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

void copy_buffer(const Device& device, VkBuffer source, VkBuffer destination,
                 VkDeviceSize byte_size) {
    if (source == VK_NULL_HANDLE || destination == VK_NULL_HANDLE) {
        throw std::runtime_error("buffer copy requires valid source and destination buffers");
    }
    if (byte_size == 0) {
        throw std::runtime_error("buffer copy size must be positive");
    }

    ImmediateCommands commands(device);
    VkBufferCopy copy{};
    copy.size = byte_size;
    vkCmdCopyBuffer(commands.command_buffer(), source, destination, 1, &copy);
    commands.submit_and_wait();
}

Buffer upload_device_buffer(const Device& device, const void* data, VkDeviceSize byte_size,
                            VkBufferUsageFlags usage) {
    if (data == nullptr) {
        throw std::runtime_error("device buffer upload requires data");
    }
    if (byte_size == 0) {
        throw std::runtime_error("device buffer upload size must be positive");
    }

    Buffer staging(device, staging_buffer_config(byte_size));
    staging.upload(data, byte_size);

    Buffer destination(device, device_local_buffer_config(byte_size, usage));
    copy_buffer(device, staging.handle(), destination.handle(), byte_size);
    return destination;
}

} // namespace cubey::vulkan
