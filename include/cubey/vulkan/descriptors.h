#pragma once

#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>

namespace cubey::vulkan {

[[nodiscard]] VkDescriptorSetLayoutBinding descriptor_binding(std::uint32_t binding,
                                                              VkDescriptorType type,
                                                              VkShaderStageFlags stage_flags,
                                                              std::uint32_t descriptor_count = 1);
[[nodiscard]] VkDescriptorSetLayoutCreateInfo
descriptor_set_layout_info(std::span<const VkDescriptorSetLayoutBinding> bindings);
[[nodiscard]] VkDescriptorPoolSize descriptor_pool_size(VkDescriptorType type,
                                                        std::uint32_t descriptor_count);
[[nodiscard]] VkDescriptorPoolCreateInfo
descriptor_pool_info(std::uint32_t max_sets, std::span<const VkDescriptorPoolSize> pool_sizes);

struct DescriptorBufferWrite {
    VkDescriptorBufferInfo buffer_info{};
    VkDescriptorSet set = VK_NULL_HANDLE;
    std::uint32_t binding = 0;
    VkDescriptorType descriptor_type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

    // The returned Vulkan write points at this object's info member.
    [[nodiscard]] VkWriteDescriptorSet descriptor_write() const&;
    VkWriteDescriptorSet descriptor_write() const&& = delete;
};

struct DescriptorImageWrite {
    VkDescriptorImageInfo image_info{};
    VkDescriptorSet set = VK_NULL_HANDLE;
    std::uint32_t binding = 0;
    VkDescriptorType descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

    // The returned Vulkan write points at this object's info member.
    [[nodiscard]] VkWriteDescriptorSet descriptor_write() const&;
    VkWriteDescriptorSet descriptor_write() const&& = delete;
};

[[nodiscard]] DescriptorBufferWrite uniform_buffer_descriptor(VkDescriptorSet set,
                                                              std::uint32_t binding,
                                                              VkBuffer buffer, VkDeviceSize range,
                                                              VkDeviceSize offset = 0);
[[nodiscard]] DescriptorImageWrite
storage_image_descriptor(VkDescriptorSet set, std::uint32_t binding, VkImageView image_view,
                         VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL);
[[nodiscard]] DescriptorImageWrite
combined_image_sampler_descriptor(VkDescriptorSet set, std::uint32_t binding, VkSampler sampler,
                                  VkImageView image_view,
                                  VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
void update_descriptor_sets(const Device& device, std::span<const VkWriteDescriptorSet> writes);

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
