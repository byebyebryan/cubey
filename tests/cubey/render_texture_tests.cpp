#include <cubey/render/texture.h>

#include <vulkan/vulkan.h>

#include <stdexcept>
#include <type_traits>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_texture_2d_config_maps_storage_sampled_usage() {
    const cubey::render::Texture2DConfig config{
        .extent = {64, 32},
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .usage = cubey::render::Texture2DUsage::StorageSampled,
        .create_sampler = false,
        .sampler = {},
    };

    const cubey::vulkan::ImageConfig image_config = cubey::render::texture_2d_image_config(config);
    require(image_config.extent.width == 64, "texture image config should preserve width");
    require(image_config.extent.height == 32, "texture image config should preserve height");
    require(image_config.extent.depth == 1, "texture image config should be 2D");
    require(image_config.format == VK_FORMAT_R8G8B8A8_UNORM,
            "texture image config should preserve format");
    require((image_config.usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0,
            "storage sampled texture should support storage writes");
    require((image_config.usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0,
            "storage sampled texture should support sampling");
    require((image_config.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0,
            "storage sampled texture should support readback copies");
}

void test_texture_2d_config_maps_transfer_sampled_usage() {
    const cubey::render::Texture2DConfig config{
        .extent = {128, 72},
        .format = VK_FORMAT_B8G8R8A8_UNORM,
        .usage = cubey::render::Texture2DUsage::TransferSampled,
        .create_sampler = false,
        .sampler = {},
    };

    const cubey::vulkan::ImageConfig image_config = cubey::render::texture_2d_image_config(config);
    require((image_config.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0,
            "transfer sampled texture should support transfer uploads");
    require((image_config.usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0,
            "transfer sampled texture should support sampling");

    static_assert(!std::is_copy_constructible_v<cubey::render::Texture2D>);
    static_assert(std::is_move_constructible_v<cubey::render::Texture2D>);
}
