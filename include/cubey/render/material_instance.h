#pragma once

#include <cubey/render/frame_data.h>
#include <cubey/render/material.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <span>

namespace cubey::render {

struct MaterialInstanceConfig {
    MaterialPassInfo material_pass{};
    std::uint32_t descriptor_set = 0;
    std::uint32_t set_count = 1;
};

[[nodiscard]] cubey::vulkan::DescriptorSetInfo
material_instance_descriptor_set_info(const MaterialInstanceConfig& config);

class MaterialInstance {
  public:
    MaterialInstance(const cubey::vulkan::Device& device, const MaterialInstanceConfig& config);

    MaterialInstance(const MaterialInstance&) = delete;
    MaterialInstance& operator=(const MaterialInstance&) = delete;
    MaterialInstance(MaterialInstance&&) = delete;
    MaterialInstance& operator=(MaterialInstance&&) = delete;

    [[nodiscard]] VkDescriptorSetLayout layout() const;
    [[nodiscard]] VkDescriptorSet set() const;
    [[nodiscard]] VkDescriptorSet set(FrameSlot frame_slot) const;
    [[nodiscard]] std::uint32_t descriptor_set_index() const noexcept {
        return descriptor_set_;
    }
    [[nodiscard]] std::uint32_t set_count() const noexcept {
        return set_count_;
    }

  private:
    std::uint32_t descriptor_set_ = 0;
    std::uint32_t set_count_ = 1;
    std::optional<cubey::vulkan::DescriptorSetBundle> single_set_;
    std::optional<cubey::vulkan::DescriptorSetArray> frame_sets_;
};

class MaterialDescriptorWriter {
  public:
    explicit MaterialDescriptorWriter(VkDescriptorSet set);

    MaterialDescriptorWriter(const MaterialDescriptorWriter&) = delete;
    MaterialDescriptorWriter& operator=(const MaterialDescriptorWriter&) = delete;
    MaterialDescriptorWriter(MaterialDescriptorWriter&&) = delete;
    MaterialDescriptorWriter& operator=(MaterialDescriptorWriter&&) = delete;

    MaterialDescriptorWriter& uniform_buffer(std::uint32_t binding, VkBuffer buffer,
                                             VkDeviceSize range, VkDeviceSize offset = 0);
    MaterialDescriptorWriter& storage_buffer(std::uint32_t binding, VkBuffer buffer,
                                             VkDeviceSize range, VkDeviceSize offset = 0);
    MaterialDescriptorWriter& storage_image(std::uint32_t binding, VkImageView image_view,
                                            VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL);
    MaterialDescriptorWriter&
    combined_image_sampler(std::uint32_t binding, VkSampler sampler, VkImageView image_view,
                           VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    [[nodiscard]] VkDescriptorSet set() const noexcept {
        return set_;
    }
    [[nodiscard]] std::span<const VkWriteDescriptorSet> writes() const {
        return writes_.writes();
    }
    void update(const cubey::vulkan::Device& device) const {
        writes_.update(device);
    }

  private:
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    cubey::vulkan::DescriptorWriteBatch writes_{};
};

void bind_material_instance(const cubey::vulkan::CommandRecorder& recorder,
                            const GraphicsPipelineResource& pipeline,
                            const MaterialInstance& material);
void bind_material_instance(const cubey::vulkan::CommandRecorder& recorder,
                            const GraphicsPipelineResource& pipeline,
                            const MaterialInstance& material, FrameSlot frame_slot);

} // namespace cubey::render
