#include <cubey/render/generated_texture.h>
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

void test_texture_3d_config_maps_storage_sampled_volume_usage() {
    const cubey::render::Texture3DConfig config{
        .extent = {32, 24, 16},
        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
        .create_sampler = true,
        .sampler =
            {
                .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            },
    };

    const cubey::vulkan::ImageConfig image_config = cubey::render::texture_3d_image_config(config);
    require(image_config.extent.width == 32, "texture 3D config should preserve width");
    require(image_config.extent.height == 24, "texture 3D config should preserve height");
    require(image_config.extent.depth == 16, "texture 3D config should preserve depth");
    require(image_config.format == VK_FORMAT_R32G32B32A32_SFLOAT,
            "texture 3D config should preserve format");
    require((image_config.usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0,
            "texture 3D should support storage writes");
    require((image_config.usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0,
            "texture 3D should support sampling");
    require(image_config.image_type == VK_IMAGE_TYPE_3D, "texture 3D should request a 3D image");
    require(image_config.view_type == VK_IMAGE_VIEW_TYPE_3D,
            "texture 3D should request a 3D image view");

    static_assert(!std::is_copy_constructible_v<cubey::render::Texture3D>);
    static_assert(std::is_move_constructible_v<cubey::render::Texture3D>);
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

void test_texture_2d_config_preserves_mip_count() {
    const cubey::render::Texture2DConfig config{
        .extent = {128, 64},
        .mip_levels = 4,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .usage = cubey::render::Texture2DUsage::TransferSampled,
        .create_sampler = false,
        .sampler = {},
    };

    const cubey::vulkan::ImageConfig image_config = cubey::render::texture_2d_image_config(config);

    require(image_config.mip_levels == 4, "texture image config should preserve mip count");
}

void test_texture_2d_byte_size_uses_compressed_blocks() {
    const cubey::render::TextureFormatLayout bc7 =
        cubey::render::texture_format_layout(VK_FORMAT_BC7_UNORM_BLOCK);
    require(bc7.block_width == 4, "BC7 should use four-wide blocks");
    require(bc7.block_height == 4, "BC7 should use four-high blocks");
    require(bc7.bytes_per_block == 16, "BC7 should use sixteen-byte blocks");
    require(bc7.compressed, "BC7 should be marked compressed");

    const VkExtent2D mip2 = cubey::render::texture_2d_mip_extent({8, 4}, 2);
    require(mip2.width == 2 && mip2.height == 1, "2D mip extent should clamp to one texel");
    const VkExtent2D far_mip = cubey::render::texture_2d_mip_extent({8, 4}, 40);
    require(far_mip.width == 1 && far_mip.height == 1,
            "2D mip extent should avoid overshifting and clamp distant mips");
    require(cubey::render::texture_cube_mip_extent(8, 40) == 1,
            "cube mip extent should avoid overshifting and clamp distant mips");
    require(cubey::render::texture_2d_byte_size({8, 4}, 3, VK_FORMAT_BC7_UNORM_BLOCK) == 64,
            "BC7 byte size should sum compressed block sizes across mips");
}

void test_depth_texture_config_maps_sampled_depth_usage() {
    const cubey::render::DepthTextureConfig config{
        .extent = {1024, 512},
        .format = VK_FORMAT_D32_SFLOAT,
        .create_sampler = true,
        .sampler =
            {
                .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                .border_color = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
            },
    };

    const cubey::vulkan::ImageConfig image_config =
        cubey::render::depth_texture_image_config(config);
    require(image_config.extent.width == 1024, "depth texture config should preserve width");
    require(image_config.extent.height == 512, "depth texture config should preserve height");
    require(image_config.extent.depth == 1, "depth texture config should be 2D");
    require(image_config.format == VK_FORMAT_D32_SFLOAT,
            "depth texture config should preserve format");
    require((image_config.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0,
            "depth texture should support depth attachment writes");
    require((image_config.usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0,
            "depth texture should support shader sampling");
    require(image_config.aspect == VK_IMAGE_ASPECT_DEPTH_BIT,
            "depth texture should use depth aspect");

    static_assert(!std::is_copy_constructible_v<cubey::render::DepthTexture>);
    static_assert(std::is_move_constructible_v<cubey::render::DepthTexture>);
}

void test_texture_cube_config_maps_transfer_sampled_cube_usage() {
    const cubey::render::TextureCubeConfig config{
        .extent = 64,
        .mip_levels = 5,
        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
        .create_sampler = true,
        .sampler =
            {
                .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .max_lod = 4.0F,
            },
    };

    const cubey::vulkan::ImageConfig image_config =
        cubey::render::texture_cube_image_config(config);
    require(image_config.extent.width == 64, "cube texture should preserve width");
    require(image_config.extent.height == 64, "cube texture should preserve height");
    require(image_config.mip_levels == 5, "cube texture should preserve mip count");
    require(image_config.array_layers == 6, "cube texture should use six array layers");
    require((image_config.flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) != 0,
            "cube texture should be cube compatible");
    require(image_config.view_type == VK_IMAGE_VIEW_TYPE_CUBE,
            "cube texture should use a cube view");
    require(cubey::render::texture_format_byte_size(VK_FORMAT_R32G32B32A32_SFLOAT) == 16,
            "RGBA32F texture format should report sixteen bytes per texel");
    require(cubey::render::texture_cube_mip_extent(64, 0) == 64,
            "cube mip 0 should preserve base extent");
    require(cubey::render::texture_cube_mip_extent(64, 4) == 4,
            "cube mip extent should halve by mip");
    require(cubey::render::texture_cube_mip_extent(2, 4) == 1,
            "cube mip extent should clamp to one");
    require(cubey::render::texture_cube_byte_size(4, 3, 4) ==
                ((4U * 4U * 6U) + (2U * 2U * 6U) + (1U * 1U * 6U)) * 4U,
            "cube byte size should include all faces and mips");

    static_assert(!std::is_copy_constructible_v<cubey::render::TextureCube>);
    static_assert(std::is_move_constructible_v<cubey::render::TextureCube>);
}

void test_compute_generated_texture_config_validates_dispatch_shape() {
    require(cubey::render::compute_dispatch_group_count(64, 8) == 8,
            "compute generated texture dispatch should divide exact groups");
    require(cubey::render::compute_dispatch_group_count(65, 8) == 9,
            "compute generated texture dispatch should round up partial groups");

    cubey::render::ComputeGeneratedTexture2DConfig config{
        .extent = {64, 32},
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .shader = cubey::render::compute_shader_file("generated.comp.spv"),
        .group_size_x = 8,
        .group_size_y = 8,
        .group_size_z = 1,
    };
    cubey::render::validate_compute_generated_texture_config(config);

    config.group_size_x = 0;
    bool threw = false;
    try {
        cubey::render::validate_compute_generated_texture_config(config);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    require(threw, "compute generated texture config should reject zero group sizes");
}
