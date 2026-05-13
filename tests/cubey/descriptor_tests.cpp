#include <cubey/vulkan/descriptors.h>

#include <vulkan/vulkan.h>

#include <array>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename T>
concept RvalueDescriptorWriteCallable = requires(T value) { std::move(value).descriptor_write(); };

} // namespace

void test_descriptor_helpers_describe_layout_pool_and_writes() {
    static_assert(!RvalueDescriptorWriteCallable<cubey::vulkan::DescriptorBufferWrite>);
    static_assert(!RvalueDescriptorWriteCallable<cubey::vulkan::DescriptorImageWrite>);

    const VkDescriptorSet descriptor_set = reinterpret_cast<VkDescriptorSet>(0x10);
    const VkBuffer uniform_buffer = reinterpret_cast<VkBuffer>(0x20);
    const VkImageView storage_view = reinterpret_cast<VkImageView>(0x30);
    const VkSampler sampler = reinterpret_cast<VkSampler>(0x40);
    const VkImageView sampled_view = reinterpret_cast<VkImageView>(0x50);
    const VkBuffer storage_buffer = reinterpret_cast<VkBuffer>(0x60);

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

    const cubey::vulkan::DescriptorBufferWrite storage_buffer_write =
        cubey::vulkan::storage_buffer_descriptor(descriptor_set, 1, storage_buffer, 128, 32);
    const VkWriteDescriptorSet storage_buffer_descriptor = storage_buffer_write.descriptor_write();
    require(storage_buffer_write.buffer_info.buffer == storage_buffer,
            "storage buffer write should preserve buffer");
    require(storage_buffer_write.buffer_info.offset == 32,
            "storage buffer write should preserve offset");
    require(storage_buffer_write.buffer_info.range == 128,
            "storage buffer write should preserve range");
    require(storage_buffer_descriptor.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            "storage buffer write should use storage-buffer type");
    require(storage_buffer_descriptor.pBufferInfo == &storage_buffer_write.buffer_info,
            "storage buffer write should point at owned buffer info");

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

void test_descriptor_write_batch_owns_write_storage_and_preserves_order() {
    static_assert(!std::is_copy_constructible_v<cubey::vulkan::DescriptorWriteBatch>);
    static_assert(!std::is_copy_assignable_v<cubey::vulkan::DescriptorWriteBatch>);
    static_assert(!std::is_move_constructible_v<cubey::vulkan::DescriptorWriteBatch>);
    static_assert(!std::is_move_assignable_v<cubey::vulkan::DescriptorWriteBatch>);

    const VkDescriptorSet first_set = reinterpret_cast<VkDescriptorSet>(0xA0);
    const VkDescriptorSet second_set = reinterpret_cast<VkDescriptorSet>(0xB0);
    const VkBuffer uniform_buffer = reinterpret_cast<VkBuffer>(0xC0);
    const VkBuffer storage_buffer = reinterpret_cast<VkBuffer>(0xD0);
    const VkSampler sampler = reinterpret_cast<VkSampler>(0xE0);
    const VkImageView sampled_view = reinterpret_cast<VkImageView>(0xF0);
    const VkImageView storage_view = reinterpret_cast<VkImageView>(0x100);

    cubey::vulkan::DescriptorWriteBatch batch;
    require(batch.empty(), "descriptor write batch should start empty");
    require(batch.size() == 0, "descriptor write batch should start with zero size");

    batch.uniform_buffer(first_set, 0, uniform_buffer, 64, 16)
        .combined_image_sampler(first_set, 1, sampler, sampled_view,
                                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
        .storage_buffer(second_set, 2, storage_buffer, 128, 32)
        .storage_image(second_set, 3, storage_view, VK_IMAGE_LAYOUT_GENERAL);

    require(!batch.empty(), "descriptor write batch should report writes after append");
    require(batch.size() == 4, "descriptor write batch should count appended writes");

    const std::span<const VkWriteDescriptorSet> writes = batch.writes();
    require(writes.size() == 4, "descriptor write batch should expose every write");

    require(writes[0].dstSet == first_set, "first batch write should preserve descriptor set");
    require(writes[0].dstBinding == 0, "first batch write should preserve binding order");
    require(writes[0].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            "first batch write should be a uniform buffer");
    require(writes[0].pBufferInfo != nullptr,
            "first batch write should point at owned buffer info");
    require(writes[0].pBufferInfo->buffer == uniform_buffer,
            "first batch write should preserve uniform buffer");
    require(writes[0].pBufferInfo->offset == 16,
            "first batch write should preserve uniform offset");
    require(writes[0].pBufferInfo->range == 64, "first batch write should preserve uniform range");

    require(writes[1].dstSet == first_set, "second batch write should preserve descriptor set");
    require(writes[1].dstBinding == 1, "second batch write should preserve binding order");
    require(writes[1].descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            "second batch write should be a combined image sampler");
    require(writes[1].pImageInfo != nullptr, "second batch write should point at owned image info");
    require(writes[1].pImageInfo->sampler == sampler, "second batch write should preserve sampler");
    require(writes[1].pImageInfo->imageView == sampled_view,
            "second batch write should preserve image view");
    require(writes[1].pImageInfo->imageLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            "second batch write should preserve image layout");

    require(writes[2].dstSet == second_set, "third batch write should preserve descriptor set");
    require(writes[2].dstBinding == 2, "third batch write should preserve binding order");
    require(writes[2].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            "third batch write should be a storage buffer");
    require(writes[2].pBufferInfo != nullptr,
            "third batch write should point at owned buffer info");
    require(writes[2].pBufferInfo->buffer == storage_buffer,
            "third batch write should preserve storage buffer");
    require(writes[2].pBufferInfo->offset == 32,
            "third batch write should preserve storage offset");
    require(writes[2].pBufferInfo->range == 128, "third batch write should preserve storage range");

    require(writes[3].dstSet == second_set, "fourth batch write should preserve descriptor set");
    require(writes[3].dstBinding == 3, "fourth batch write should preserve binding order");
    require(writes[3].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            "fourth batch write should be a storage image");
    require(writes[3].pImageInfo != nullptr, "fourth batch write should point at owned image info");
    require(writes[3].pImageInfo->imageView == storage_view,
            "fourth batch write should preserve storage image view");
    require(writes[3].pImageInfo->imageLayout == VK_IMAGE_LAYOUT_GENERAL,
            "fourth batch write should preserve storage image layout");

    const std::span<const VkWriteDescriptorSet> rebuilt_writes = batch.writes();
    require(rebuilt_writes.size() == 4, "descriptor write batch should rebuild a stable count");
    require(rebuilt_writes[0].pBufferInfo->buffer == uniform_buffer,
            "descriptor write batch rebuilt writes should still point at owned storage");

    batch.clear();
    require(batch.empty(), "descriptor write batch should become empty after clear");
    require(batch.size() == 0, "descriptor write batch should reset size after clear");
    require(batch.writes().empty(), "descriptor write batch writes should be empty after clear");
}

void test_descriptor_set_info_copies_bindings_and_aggregates_pool_sizes() {
    static_assert(!std::is_copy_constructible_v<cubey::vulkan::DescriptorSetBundle>);
    static_assert(!std::is_copy_assignable_v<cubey::vulkan::DescriptorSetBundle>);

    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 3> configs{{
        {
            .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stage_flags = VK_SHADER_STAGE_VERTEX_BIT,
        },
        {
            .binding = 1,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 2,
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .descriptor_count = 2,
        },
    }};

    const cubey::vulkan::DescriptorSetInfo info(configs, 2);

    require(info.bindings().size() == configs.size(),
            "descriptor set info should expose one binding per config");
    require(info.bindings()[0].binding == 0, "descriptor set info should preserve binding index");
    require(info.bindings()[2].descriptorCount == 2,
            "descriptor set info should preserve descriptor count");
    require(info.bindings().data() !=
                reinterpret_cast<const VkDescriptorSetLayoutBinding*>(configs.data()),
            "descriptor set info should own binding storage");

    const VkDescriptorSetLayoutCreateInfo& layout_info = info.layout_info();
    require(layout_info.sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            "descriptor set info should expose layout create info");
    require(layout_info.bindingCount == configs.size(),
            "descriptor set info should preserve layout binding count");
    require(layout_info.pBindings == info.bindings().data(),
            "descriptor set info layout info should point at owned bindings");

    require(info.pool_sizes().size() == 2,
            "descriptor set info should aggregate duplicate descriptor types");
    require(info.pool_sizes()[0].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            "descriptor set info should preserve first descriptor type order");
    require(info.pool_sizes()[0].descriptorCount == 6,
            "descriptor set info should multiply uniform descriptors by max sets");
    require(info.pool_sizes()[1].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            "descriptor set info should preserve sampler descriptor type");
    require(info.pool_sizes()[1].descriptorCount == 2,
            "descriptor set info should multiply sampler descriptors by max sets");

    const VkDescriptorPoolCreateInfo& pool_info = info.pool_info();
    require(info.max_sets() == 2, "descriptor set info should expose max set count");
    require(pool_info.sType == VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            "descriptor set info should expose descriptor pool create info");
    require(pool_info.maxSets == 2, "descriptor set info should preserve max set count");
    require(pool_info.poolSizeCount == info.pool_sizes().size(),
            "descriptor set info should preserve pool size count");
    require(pool_info.pPoolSizes == info.pool_sizes().data(),
            "descriptor set info pool info should point at owned pool sizes");

    static_assert(!std::is_copy_constructible_v<cubey::vulkan::DescriptorSetArray>);
    static_assert(!std::is_copy_assignable_v<cubey::vulkan::DescriptorSetArray>);
    static_assert(std::is_same_v<decltype(&cubey::vulkan::DescriptorPool::allocate_many),
                                 std::vector<VkDescriptorSet> (cubey::vulkan::DescriptorPool::*)(
                                     VkDescriptorSetLayout, std::uint32_t) const>);
    static_assert(std::is_same_v<decltype(&cubey::vulkan::DescriptorSetArray::set),
                                 VkDescriptorSet (cubey::vulkan::DescriptorSetArray::*)(
                                     std::uint32_t) const>);
    static_assert(std::is_same_v<decltype(&cubey::vulkan::DescriptorSetArray::sets),
                                 std::span<const VkDescriptorSet> (
                                     cubey::vulkan::DescriptorSetArray::*)() const>);
    static_assert(std::is_same_v<decltype(&cubey::vulkan::DescriptorSetArray::size),
                                 std::uint32_t (cubey::vulkan::DescriptorSetArray::*)() const>);
}

void test_descriptor_set_allocate_info_describes_multiple_sets() {
    const VkDescriptorPool pool = reinterpret_cast<VkDescriptorPool>(0x70);
    const std::array<VkDescriptorSetLayout, 3> layouts{
        reinterpret_cast<VkDescriptorSetLayout>(0x80),
        reinterpret_cast<VkDescriptorSetLayout>(0x90),
        reinterpret_cast<VkDescriptorSetLayout>(0xA0),
    };

    const VkDescriptorSetAllocateInfo info =
        cubey::vulkan::descriptor_set_allocate_info(pool, layouts);

    require(info.sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            "descriptor allocate info should use descriptor set allocate info");
    require(info.descriptorPool == pool, "descriptor allocate info should preserve pool");
    require(info.descriptorSetCount == layouts.size(),
            "descriptor allocate info should preserve layout count");
    require(info.pSetLayouts == layouts.data(),
            "descriptor allocate info should point at caller-owned layouts");
}
