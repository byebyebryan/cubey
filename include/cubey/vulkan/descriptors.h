#pragma once

#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

namespace cubey::vulkan {

class DescriptorSetLayout {
  public:
    DescriptorSetLayout(const Device& device, const VkDescriptorSetLayoutCreateInfo& info);
    ~DescriptorSetLayout();

    DescriptorSetLayout(const DescriptorSetLayout&) = delete;
    DescriptorSetLayout& operator=(const DescriptorSetLayout&) = delete;

    VkDescriptorSetLayout handle() const {
        return layout_;
    }

  private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
};

class DescriptorPool {
  public:
    DescriptorPool(const Device& device, const VkDescriptorPoolCreateInfo& info);
    ~DescriptorPool();

    DescriptorPool(const DescriptorPool&) = delete;
    DescriptorPool& operator=(const DescriptorPool&) = delete;

    VkDescriptorPool handle() const {
        return pool_;
    }

    VkDescriptorSet allocate(VkDescriptorSetLayout layout) const;

  private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
};

} // namespace cubey::vulkan
