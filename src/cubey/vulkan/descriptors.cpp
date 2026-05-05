#include <cubey/vulkan/descriptors.h>

#include <cubey/vulkan/vk_check.h>

#include <stdexcept>

namespace cubey::vulkan {

VkWriteDescriptorSet DescriptorBufferWrite::descriptor_write() const& {
    auto result = vk_struct<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    result.dstSet = set;
    result.dstBinding = binding;
    result.descriptorCount = 1;
    result.descriptorType = descriptor_type;
    result.pBufferInfo = &buffer_info;
    return result;
}

VkWriteDescriptorSet DescriptorImageWrite::descriptor_write() const& {
    auto result = vk_struct<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    result.dstSet = set;
    result.dstBinding = binding;
    result.descriptorCount = 1;
    result.descriptorType = descriptor_type;
    result.pImageInfo = &image_info;
    return result;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
VkDescriptorSetLayoutBinding descriptor_binding(std::uint32_t binding, VkDescriptorType type,
                                                VkShaderStageFlags stage_flags,
                                                std::uint32_t descriptor_count) {
    if (descriptor_count == 0) {
        throw std::runtime_error("descriptor binding count must be positive");
    }

    VkDescriptorSetLayoutBinding result{};
    result.binding = binding;
    result.descriptorType = type;
    result.descriptorCount = descriptor_count;
    result.stageFlags = stage_flags;
    return result;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

VkDescriptorSetLayoutCreateInfo
descriptor_set_layout_info(std::span<const VkDescriptorSetLayoutBinding> bindings) {
    auto info = vk_struct<VkDescriptorSetLayoutCreateInfo>(
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    info.bindingCount = static_cast<std::uint32_t>(bindings.size());
    info.pBindings = bindings.data();
    return info;
}

VkDescriptorPoolSize descriptor_pool_size(VkDescriptorType type, std::uint32_t descriptor_count) {
    if (descriptor_count == 0) {
        throw std::runtime_error("descriptor pool size count must be positive");
    }

    VkDescriptorPoolSize result{};
    result.type = type;
    result.descriptorCount = descriptor_count;
    return result;
}

VkDescriptorPoolCreateInfo descriptor_pool_info(std::uint32_t max_sets,
                                                std::span<const VkDescriptorPoolSize> pool_sizes) {
    if (max_sets == 0) {
        throw std::runtime_error("descriptor pool max set count must be positive");
    }
    if (pool_sizes.empty()) {
        throw std::runtime_error("descriptor pool requires at least one pool size");
    }

    auto info =
        vk_struct<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    info.maxSets = max_sets;
    info.poolSizeCount = static_cast<std::uint32_t>(pool_sizes.size());
    info.pPoolSizes = pool_sizes.data();
    return info;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
DescriptorBufferWrite uniform_buffer_descriptor(VkDescriptorSet set, std::uint32_t binding,
                                                VkBuffer buffer, VkDeviceSize range,
                                                VkDeviceSize offset) {
    if (set == VK_NULL_HANDLE || buffer == VK_NULL_HANDLE) {
        throw std::runtime_error("uniform buffer descriptor requires valid set and buffer");
    }
    if (range == 0) {
        throw std::runtime_error("uniform buffer descriptor range must be positive");
    }

    DescriptorBufferWrite result{};
    result.buffer_info.buffer = buffer;
    result.buffer_info.offset = offset;
    result.buffer_info.range = range;
    result.set = set;
    result.binding = binding;
    result.descriptor_type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    return result;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

DescriptorImageWrite storage_image_descriptor(VkDescriptorSet set, std::uint32_t binding,
                                              VkImageView image_view, VkImageLayout layout) {
    if (set == VK_NULL_HANDLE || image_view == VK_NULL_HANDLE) {
        throw std::runtime_error("storage image descriptor requires valid set and image view");
    }

    DescriptorImageWrite result{};
    result.image_info.imageView = image_view;
    result.image_info.imageLayout = layout;
    result.set = set;
    result.binding = binding;
    result.descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    return result;
}

DescriptorImageWrite combined_image_sampler_descriptor(VkDescriptorSet set, std::uint32_t binding,
                                                       VkSampler sampler, VkImageView image_view,
                                                       VkImageLayout layout) {
    if (set == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE || image_view == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "combined image sampler descriptor requires valid set, sampler, and image view");
    }

    DescriptorImageWrite result{};
    result.image_info.sampler = sampler;
    result.image_info.imageView = image_view;
    result.image_info.imageLayout = layout;
    result.set = set;
    result.binding = binding;
    result.descriptor_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    return result;
}

void update_descriptor_sets(const Device& device, std::span<const VkWriteDescriptorSet> writes) {
    if (device.handle() == VK_NULL_HANDLE) {
        throw std::runtime_error("descriptor update requires a valid Vulkan device");
    }
    if (writes.empty()) {
        throw std::runtime_error("descriptor update requires at least one write");
    }

    vkUpdateDescriptorSets(device.handle(), static_cast<std::uint32_t>(writes.size()),
                           writes.data(), 0, nullptr);
}

DescriptorSetLayout::DescriptorSetLayout(const Device& device,
                                         const VkDescriptorSetLayoutCreateInfo& info)
    : device_(device.handle()) {
    if (device_ == VK_NULL_HANDLE) {
        throw std::runtime_error("descriptor set layout creation requires a valid Vulkan device");
    }

    check(vkCreateDescriptorSetLayout(device_, &info, nullptr, &layout_),
          "vkCreateDescriptorSetLayout");
}

DescriptorSetLayout::~DescriptorSetLayout() {
    if (layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, layout_, nullptr);
    }
}

DescriptorPool::DescriptorPool(const Device& device, const VkDescriptorPoolCreateInfo& info)
    : device_(device.handle()) {
    if (device_ == VK_NULL_HANDLE) {
        throw std::runtime_error("descriptor pool creation requires a valid Vulkan device");
    }

    check(vkCreateDescriptorPool(device_, &info, nullptr, &pool_), "vkCreateDescriptorPool");
}

DescriptorPool::~DescriptorPool() {
    if (pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, pool_, nullptr);
    }
}

VkDescriptorSet DescriptorPool::allocate(VkDescriptorSetLayout layout) const {
    if (layout == VK_NULL_HANDLE) {
        throw std::runtime_error("descriptor set allocation requires a valid layout");
    }

    auto alloc =
        vk_struct<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    alloc.descriptorPool = pool_;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &layout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    check(vkAllocateDescriptorSets(device_, &alloc, &set), "vkAllocateDescriptorSets");
    return set;
}

} // namespace cubey::vulkan
