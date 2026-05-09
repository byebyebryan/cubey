#pragma once

#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>
#include <vector>

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
[[nodiscard]] VkDescriptorSetAllocateInfo
descriptor_set_allocate_info(VkDescriptorPool pool,
                             std::span<const VkDescriptorSetLayout> layouts);

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
[[nodiscard]] DescriptorBufferWrite storage_buffer_descriptor(VkDescriptorSet set,
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

struct DescriptorSetBindingConfig {
    std::uint32_t binding = 0;
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    VkShaderStageFlags stage_flags = 0;
    std::uint32_t descriptor_count = 1;
};

class DescriptorSetInfo {
  public:
    explicit DescriptorSetInfo(std::span<const DescriptorSetBindingConfig> bindings,
                               std::uint32_t max_sets = 1);

    DescriptorSetInfo(const DescriptorSetInfo&) = delete;
    DescriptorSetInfo& operator=(const DescriptorSetInfo&) = delete;

    [[nodiscard]] std::span<const VkDescriptorSetLayoutBinding> bindings() const {
        return bindings_;
    }

    [[nodiscard]] std::span<const VkDescriptorPoolSize> pool_sizes() const {
        return pool_sizes_;
    }

    [[nodiscard]] const VkDescriptorSetLayoutCreateInfo& layout_info() const {
        return layout_info_;
    }

    [[nodiscard]] const VkDescriptorPoolCreateInfo& pool_info() const {
        return pool_info_;
    }
    [[nodiscard]] std::uint32_t max_sets() const {
        return max_sets_;
    }

  private:
    std::vector<VkDescriptorSetLayoutBinding> bindings_;
    std::vector<VkDescriptorPoolSize> pool_sizes_;
    VkDescriptorSetLayoutCreateInfo layout_info_{};
    VkDescriptorPoolCreateInfo pool_info_{};
    std::uint32_t max_sets_ = 0;
};

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
    std::vector<VkDescriptorSet> allocate_many(VkDescriptorSetLayout layout,
                                               std::uint32_t count) const;

  private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
};

class DescriptorSetBundle {
  public:
    DescriptorSetBundle(const Device& device, const DescriptorSetInfo& info);

    DescriptorSetBundle(const DescriptorSetBundle&) = delete;
    DescriptorSetBundle& operator=(const DescriptorSetBundle&) = delete;

    [[nodiscard]] VkDescriptorSetLayout layout() const {
        return layout_.handle();
    }

    [[nodiscard]] VkDescriptorPool pool() const {
        return pool_.handle();
    }

    [[nodiscard]] VkDescriptorSet set() const {
        return set_;
    }

  private:
    DescriptorSetLayout layout_;
    DescriptorPool pool_;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
};

class DescriptorSetArray {
  public:
    DescriptorSetArray(const Device& device, const DescriptorSetInfo& info);

    DescriptorSetArray(const DescriptorSetArray&) = delete;
    DescriptorSetArray& operator=(const DescriptorSetArray&) = delete;

    [[nodiscard]] VkDescriptorSetLayout layout() const {
        return layout_.handle();
    }

    [[nodiscard]] VkDescriptorPool pool() const {
        return pool_.handle();
    }

    [[nodiscard]] std::span<const VkDescriptorSet> sets() const {
        return sets_;
    }

    [[nodiscard]] VkDescriptorSet set(std::uint32_t index) const;

    [[nodiscard]] std::uint32_t size() const {
        return static_cast<std::uint32_t>(sets_.size());
    }

  private:
    DescriptorSetLayout layout_;
    DescriptorPool pool_;
    std::vector<VkDescriptorSet> sets_;
};

} // namespace cubey::vulkan
