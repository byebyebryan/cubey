#include <cubey/vulkan/descriptors.h>

#include <cubey/vulkan/vk_check.h>

#include <stdexcept>

namespace cubey::vulkan {

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
