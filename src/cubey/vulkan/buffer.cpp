#include <cubey/vulkan/buffer.h>

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

} // namespace cubey::vulkan
