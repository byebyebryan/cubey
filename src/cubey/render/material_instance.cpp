#include <cubey/render/material_instance.h>

#include <stdexcept>

namespace cubey::render {

cubey::vulkan::DescriptorSetInfo
material_instance_descriptor_set_info(const MaterialInstanceConfig& config) {
    if (config.set_count == 0) {
        throw std::runtime_error("material instance set count must be positive");
    }
    return material_descriptor_set_info(config.material_pass, config.descriptor_set,
                                        config.set_count);
}

MaterialInstance::MaterialInstance(const cubey::vulkan::Device& device,
                                   const MaterialInstanceConfig& config)
    : descriptor_set_(config.descriptor_set), set_count_(config.set_count) {
    const cubey::vulkan::DescriptorSetInfo descriptor_info =
        material_instance_descriptor_set_info(config);
    if (set_count_ == 1) {
        single_set_.emplace(device, descriptor_info);
    } else {
        frame_sets_.emplace(device, descriptor_info);
    }
}

VkDescriptorSetLayout MaterialInstance::layout() const {
    if (single_set_.has_value()) {
        return single_set_->layout();
    }
    if (frame_sets_.has_value()) {
        return frame_sets_->layout();
    }
    throw std::runtime_error("material instance descriptors are not initialized");
}

VkDescriptorSet MaterialInstance::set() const {
    if (!single_set_.has_value()) {
        throw std::runtime_error("material instance does not own a single descriptor set");
    }
    return single_set_->set();
}

VkDescriptorSet MaterialInstance::set(FrameSlot frame_slot) const {
    validate_frame_slot(frame_slot);
    if (frame_slot.count != set_count_) {
        throw std::runtime_error("material instance frame slot count mismatch");
    }
    if (single_set_.has_value()) {
        return single_set_->set();
    }
    if (frame_sets_.has_value()) {
        return frame_sets_->set(frame_slot.index);
    }
    throw std::runtime_error("material instance descriptors are not initialized");
}

MaterialDescriptorWriter::MaterialDescriptorWriter(VkDescriptorSet set) : set_(set) {
    if (set_ == VK_NULL_HANDLE) {
        throw std::runtime_error("material descriptor writer requires a descriptor set");
    }
}

MaterialDescriptorWriter& MaterialDescriptorWriter::uniform_buffer(std::uint32_t binding,
                                                                   VkBuffer buffer,
                                                                   VkDeviceSize range,
                                                                   VkDeviceSize offset) {
    writes_.uniform_buffer(set_, binding, buffer, range, offset);
    return *this;
}

MaterialDescriptorWriter& MaterialDescriptorWriter::storage_buffer(std::uint32_t binding,
                                                                   VkBuffer buffer,
                                                                   VkDeviceSize range,
                                                                   VkDeviceSize offset) {
    writes_.storage_buffer(set_, binding, buffer, range, offset);
    return *this;
}

MaterialDescriptorWriter& MaterialDescriptorWriter::storage_image(std::uint32_t binding,
                                                                  VkImageView image_view,
                                                                  VkImageLayout layout) {
    writes_.storage_image(set_, binding, image_view, layout);
    return *this;
}

MaterialDescriptorWriter& MaterialDescriptorWriter::combined_image_sampler(std::uint32_t binding,
                                                                           VkSampler sampler,
                                                                           VkImageView image_view,
                                                                           VkImageLayout layout) {
    writes_.combined_image_sampler(set_, binding, sampler, image_view, layout);
    return *this;
}

void bind_material_instance(const cubey::vulkan::CommandRecorder& recorder,
                            const GraphicsPipelineResource& pipeline,
                            const MaterialInstance& material) {
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(),
                                 material.descriptor_set_index(), material.set());
}

void bind_material_instance(const cubey::vulkan::CommandRecorder& recorder,
                            const GraphicsPipelineResource& pipeline,
                            const MaterialInstance& material, FrameSlot frame_slot) {
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(),
                                 material.descriptor_set_index(), material.set(frame_slot));
}

} // namespace cubey::render
