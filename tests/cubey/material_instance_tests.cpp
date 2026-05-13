#include <cubey/render/material_instance.h>

#include <vulkan/vulkan.h>

#include <array>
#include <span>
#include <stdexcept>
#include <type_traits>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_throws(auto&& action, const char* message) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

[[nodiscard]] cubey::render::MaterialPassInfo textured_pass_info() {
    return cubey::render::MaterialPassInfo{
        .label = "material instance textured pass",
        .descriptor_sets =
            {
                cubey::render::MaterialDescriptorSetLayout{
                    .set = 1,
                    .bindings =
                        {
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = 0,
                                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                .stage_flags =
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = 1,
                                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                        },
                },
            },
    };
}

} // namespace

void test_material_instance_config_builds_descriptor_set_info() {
    static_assert(!std::is_copy_constructible_v<cubey::render::MaterialInstance>);
    static_assert(!std::is_copy_assignable_v<cubey::render::MaterialInstance>);

    const cubey::render::MaterialInstanceConfig config{
        .material_pass = textured_pass_info(),
        .descriptor_set = 1,
        .set_count = 3,
    };

    const cubey::vulkan::DescriptorSetInfo descriptor_info =
        cubey::render::material_instance_descriptor_set_info(config);

    require(descriptor_info.max_sets() == 3, "material instance config should preserve set count");
    require(descriptor_info.bindings().size() == 2,
            "material instance config should use declared material pass bindings");
    require(descriptor_info.bindings()[0].binding == 0,
            "material instance config should preserve uniform binding");
    require(descriptor_info.bindings()[1].descriptorType ==
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            "material instance config should preserve sampler binding type");
    require(descriptor_info.pool_sizes().size() == 2,
            "material instance config should aggregate descriptor pool sizes");
    require(descriptor_info.pool_sizes()[0].descriptorCount == 3,
            "material instance config should scale uniform descriptors by set count");
    require(descriptor_info.pool_sizes()[1].descriptorCount == 3,
            "material instance config should scale sampler descriptors by set count");

    require_throws(
        [] {
            static_cast<void>(cubey::render::material_instance_descriptor_set_info(
                cubey::render::MaterialInstanceConfig{
                    .material_pass = textured_pass_info(),
                    .descriptor_set = 1,
                    .set_count = 0,
                }));
        },
        "material instance config should reject zero set count");
    require_throws(
        [] {
            static_cast<void>(cubey::render::material_instance_descriptor_set_info(
                cubey::render::MaterialInstanceConfig{
                    .material_pass = textured_pass_info(),
                    .descriptor_set = 0,
                    .set_count = 1,
                }));
        },
        "material instance config should reject missing material pass descriptor sets");
}

void test_material_descriptor_writer_preserves_set_and_write_order() {
    static_assert(!std::is_copy_constructible_v<cubey::render::MaterialDescriptorWriter>);
    static_assert(!std::is_copy_assignable_v<cubey::render::MaterialDescriptorWriter>);

    const VkDescriptorSet descriptor_set = reinterpret_cast<VkDescriptorSet>(0x10);
    const VkBuffer uniform_buffer = reinterpret_cast<VkBuffer>(0x20);
    const VkBuffer storage_buffer = reinterpret_cast<VkBuffer>(0x30);
    const VkImageView storage_view = reinterpret_cast<VkImageView>(0x40);
    const VkSampler sampler = reinterpret_cast<VkSampler>(0x50);
    const VkImageView sampled_view = reinterpret_cast<VkImageView>(0x60);

    cubey::render::MaterialDescriptorWriter writer(descriptor_set);
    writer.uniform_buffer(0, uniform_buffer, 64, 16)
        .combined_image_sampler(1, sampler, sampled_view,
                                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
        .storage_buffer(2, storage_buffer, 128, 32)
        .storage_image(3, storage_view);

    const std::span<const VkWriteDescriptorSet> writes = writer.writes();
    require(writer.set() == descriptor_set, "material descriptor writer should preserve set");
    require(writes.size() == 4, "material descriptor writer should expose every write");
    require(writes[0].dstSet == descriptor_set && writes[0].dstBinding == 0,
            "material descriptor writer should preserve first write set and binding");
    require(writes[0].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            "material descriptor writer first write should be a uniform buffer");
    require(writes[0].pBufferInfo->buffer == uniform_buffer,
            "material descriptor writer should preserve uniform buffer");
    require(writes[1].descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            "material descriptor writer second write should be a combined image sampler");
    require(writes[1].pImageInfo->sampler == sampler,
            "material descriptor writer should preserve sampler");
    require(writes[1].pImageInfo->imageView == sampled_view,
            "material descriptor writer should preserve sampled image view");
    require(writes[2].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            "material descriptor writer third write should be a storage buffer");
    require(writes[2].pBufferInfo->buffer == storage_buffer,
            "material descriptor writer should preserve storage buffer");
    require(writes[3].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            "material descriptor writer fourth write should be a storage image");
    require(writes[3].pImageInfo->imageView == storage_view,
            "material descriptor writer should preserve storage image view");

    require_throws([] { cubey::render::MaterialDescriptorWriter invalid_writer(VK_NULL_HANDLE); },
                   "material descriptor writer should reject a null descriptor set");
}
