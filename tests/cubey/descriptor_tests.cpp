#include <cubey/vulkan/descriptors.h>

#include <vulkan/vulkan.h>

#include <array>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_descriptor_helpers_describe_layout_pool_and_writes() {
    const VkDescriptorSet descriptor_set = reinterpret_cast<VkDescriptorSet>(0x10);
    const VkBuffer uniform_buffer = reinterpret_cast<VkBuffer>(0x20);
    const VkImageView storage_view = reinterpret_cast<VkImageView>(0x30);
    const VkSampler sampler = reinterpret_cast<VkSampler>(0x40);
    const VkImageView sampled_view = reinterpret_cast<VkImageView>(0x50);

    const VkDescriptorSetLayoutBinding uniform_binding = cubey::vulkan::descriptor_binding(
        0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);
    require(uniform_binding.binding == 0, "descriptor binding should preserve binding index");
    require(uniform_binding.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            "descriptor binding should preserve descriptor type");
    require(uniform_binding.descriptorCount == 1,
            "descriptor binding should default to one descriptor");
    require(uniform_binding.stageFlags == VK_SHADER_STAGE_VERTEX_BIT,
            "descriptor binding should preserve shader stages");

    const VkDescriptorSetLayoutBinding image_binding = cubey::vulkan::descriptor_binding(
        1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 3);
    require(image_binding.descriptorCount == 3,
            "descriptor binding should preserve descriptor count");

    const std::array<VkDescriptorSetLayoutBinding, 2> bindings{
        uniform_binding,
        image_binding,
    };
    const VkDescriptorSetLayoutCreateInfo layout_info =
        cubey::vulkan::descriptor_set_layout_info(bindings);
    require(layout_info.sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            "descriptor layout info should use descriptor set layout create info");
    require(layout_info.bindingCount == bindings.size(),
            "descriptor layout info should preserve binding count");
    require(layout_info.pBindings == bindings.data(),
            "descriptor layout info should reference caller-owned bindings");

    const VkDescriptorPoolSize pool_size =
        cubey::vulkan::descriptor_pool_size(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2);
    require(pool_size.type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            "descriptor pool size should preserve descriptor type");
    require(pool_size.descriptorCount == 2, "descriptor pool size should preserve count");

    const std::array<VkDescriptorPoolSize, 1> pool_sizes{pool_size};
    const VkDescriptorPoolCreateInfo pool_info = cubey::vulkan::descriptor_pool_info(4, pool_sizes);
    require(pool_info.sType == VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            "descriptor pool info should use descriptor pool create info");
    require(pool_info.maxSets == 4, "descriptor pool info should preserve max sets");
    require(pool_info.poolSizeCount == pool_sizes.size(),
            "descriptor pool info should preserve pool size count");
    require(pool_info.pPoolSizes == pool_sizes.data(),
            "descriptor pool info should reference caller-owned pool sizes");

    const cubey::vulkan::DescriptorBufferWrite uniform_write =
        cubey::vulkan::uniform_buffer_descriptor(descriptor_set, 0, uniform_buffer, 64, 16);
    const VkWriteDescriptorSet uniform_descriptor = uniform_write.descriptor_write();
    require(uniform_write.buffer_info.buffer == uniform_buffer,
            "uniform write should preserve buffer");
    require(uniform_write.buffer_info.offset == 16, "uniform write should preserve offset");
    require(uniform_write.buffer_info.range == 64, "uniform write should preserve range");
    require(uniform_descriptor.dstSet == descriptor_set,
            "uniform descriptor should preserve descriptor set");
    require(uniform_descriptor.dstBinding == 0, "uniform descriptor should preserve binding");
    require(uniform_descriptor.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            "uniform write should use uniform-buffer type");
    require(uniform_descriptor.pBufferInfo == &uniform_write.buffer_info,
            "uniform write should point at owned buffer info");

    const cubey::vulkan::DescriptorImageWrite storage_write =
        cubey::vulkan::storage_image_descriptor(descriptor_set, 1, storage_view);
    const VkWriteDescriptorSet storage_descriptor = storage_write.descriptor_write();
    require(storage_write.image_info.imageView == storage_view,
            "storage write should preserve image view");
    require(storage_write.image_info.imageLayout == VK_IMAGE_LAYOUT_GENERAL,
            "storage write should default to general layout");
    require(storage_descriptor.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            "storage write should use storage-image type");
    require(storage_descriptor.pImageInfo == &storage_write.image_info,
            "storage write should point at owned image info");

    const cubey::vulkan::DescriptorImageWrite sampler_write =
        cubey::vulkan::combined_image_sampler_descriptor(descriptor_set, 2, sampler, sampled_view,
                                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    const VkWriteDescriptorSet sampler_descriptor = sampler_write.descriptor_write();
    require(sampler_write.image_info.sampler == sampler,
            "combined sampler write should preserve sampler");
    require(sampler_write.image_info.imageView == sampled_view,
            "combined sampler write should preserve image view");
    require(sampler_descriptor.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            "combined sampler write should use combined-image-sampler type");
}
