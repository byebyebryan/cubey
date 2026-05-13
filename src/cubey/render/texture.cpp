#include <cubey/render/texture.h>

#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/immediate_commands.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace cubey::render {
namespace {

void validate_config(const Texture2DConfig& config) {
    if (config.extent.width == 0 || config.extent.height == 0) {
        throw std::runtime_error("texture extent must be nonzero");
    }
    if (config.format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("texture format must be defined");
    }
}

void validate_config(const TextureCubeConfig& config) {
    if (config.extent == 0) {
        throw std::runtime_error("texture cube extent must be nonzero");
    }
    if (config.mip_levels == 0) {
        throw std::runtime_error("texture cube mip level count must be nonzero");
    }
    if (config.format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("texture cube format must be defined");
    }
}

void validate_config(const DepthTextureConfig& config) {
    if (config.extent.width == 0 || config.extent.height == 0) {
        throw std::runtime_error("depth texture extent must be nonzero");
    }
    if (config.format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("depth texture format must be defined");
    }
}

[[nodiscard]] std::size_t checked_mul(std::size_t lhs, std::size_t rhs,
                                      const char* message) {
    if (lhs != 0 && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
        throw std::runtime_error(message);
    }
    return lhs * rhs;
}

} // namespace

cubey::vulkan::ImageConfig texture_2d_image_config(const Texture2DConfig& config) {
    validate_config(config);
    switch (config.usage) {
    case Texture2DUsage::StorageSampled:
        return cubey::vulkan::storage_sampled_image_config(config.extent, config.format);
    case Texture2DUsage::TransferSampled:
        return cubey::vulkan::transfer_sampled_image_config(config.extent, config.format);
    }
    throw std::runtime_error("unsupported texture usage");
}

cubey::vulkan::ImageConfig texture_cube_image_config(const TextureCubeConfig& config) {
    validate_config(config);
    return cubey::vulkan::transfer_sampled_cube_image_config(config.extent, config.mip_levels,
                                                             config.format);
}

cubey::vulkan::ImageConfig depth_texture_image_config(const DepthTextureConfig& config) {
    validate_config(config);
    cubey::vulkan::ImageConfig image_config =
        cubey::vulkan::depth_image_config(config.extent, config.format);
    image_config.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    return image_config;
}

std::size_t texture_format_byte_size(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
        return 4;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return 16;
    default:
        throw std::runtime_error("texture format byte size is not known");
    }
}

std::uint32_t texture_cube_mip_extent(std::uint32_t extent, std::uint32_t mip_level) {
    if (extent == 0) {
        throw std::runtime_error("texture cube mip extent requires a nonzero base extent");
    }
    return std::max(1U, extent >> mip_level);
}

std::size_t texture_cube_byte_size(std::uint32_t extent, std::uint32_t mip_levels,
                                   std::size_t texel_bytes) {
    if (extent == 0 || mip_levels == 0 || texel_bytes == 0) {
        throw std::runtime_error("texture cube byte size requires nonzero inputs");
    }
    std::size_t total = 0;
    for (std::uint32_t mip = 0; mip < mip_levels; ++mip) {
        const std::uint32_t mip_extent = texture_cube_mip_extent(extent, mip);
        const std::size_t texels = checked_mul(static_cast<std::size_t>(mip_extent),
                                               static_cast<std::size_t>(mip_extent),
                                               "texture cube mip texel count overflows");
        const std::size_t face_bytes =
            checked_mul(texels, texel_bytes, "texture cube face byte size overflows");
        const std::size_t mip_bytes =
            checked_mul(face_bytes, 6U, "texture cube mip byte size overflows");
        if (total > std::numeric_limits<std::size_t>::max() - mip_bytes) {
            throw std::runtime_error("texture cube byte size overflows");
        }
        total += mip_bytes;
    }
    return total;
}

Texture2D::Texture2D(const cubey::vulkan::Device& device, const Texture2DConfig& config)
    : image_(device, texture_2d_image_config(config)) {
    if (config.create_sampler) {
        sampler_.emplace(device, config.sampler);
    }
}

VkExtent2D Texture2D::extent() const {
    const VkExtent3D image_extent = image_.extent();
    return {image_extent.width, image_extent.height};
}

const cubey::vulkan::Sampler& Texture2D::sampler() const {
    if (!sampler_.has_value()) {
        throw std::runtime_error("texture has no sampler");
    }
    return sampler_.value();
}

TextureCube::TextureCube(const cubey::vulkan::Device& device, const TextureCubeConfig& config)
    : image_(device, texture_cube_image_config(config)) {
    if (config.create_sampler) {
        sampler_.emplace(device, config.sampler);
    }
}

const cubey::vulkan::Sampler& TextureCube::sampler() const {
    if (!sampler_.has_value()) {
        throw std::runtime_error("texture cube has no sampler");
    }
    return sampler_.value();
}

DepthTexture::DepthTexture(const cubey::vulkan::Device& device, const DepthTextureConfig& config)
    : image_(device, depth_texture_image_config(config)) {
    if (config.create_sampler) {
        sampler_.emplace(device, config.sampler);
    }
}

VkExtent2D DepthTexture::extent() const {
    const VkExtent3D image_extent = image_.extent();
    return {image_extent.width, image_extent.height};
}

const cubey::vulkan::Sampler& DepthTexture::sampler() const {
    if (!sampler_.has_value()) {
        throw std::runtime_error("depth texture has no sampler");
    }
    return sampler_.value();
}

Texture2D create_uploaded_texture_2d(const cubey::vulkan::Device& device,
                                     cubey::vulkan::GpuRuntime& gpu,
                                     const UploadedTexture2DConfig& config) {
    validate_config(Texture2DConfig{
        .extent = config.extent,
        .format = config.format,
        .usage = Texture2DUsage::TransferSampled,
        .create_sampler = config.create_sampler,
        .sampler = config.sampler,
    });
    if (config.format != VK_FORMAT_R8G8B8A8_UNORM &&
        config.format != VK_FORMAT_R8G8B8A8_SRGB) {
        throw std::runtime_error("uploaded texture helper currently requires RGBA8 format");
    }
    const std::size_t expected_size =
        static_cast<std::size_t>(config.extent.width) *
        static_cast<std::size_t>(config.extent.height) * 4U;
    if (config.rgba8.size() != expected_size) {
        throw std::runtime_error("uploaded texture byte count must match RGBA8 extent");
    }

    Texture2D texture(device, Texture2DConfig{
                                  .extent = config.extent,
                                  .format = config.format,
                                  .usage = Texture2DUsage::TransferSampled,
                                  .create_sampler = config.create_sampler,
                                  .sampler = config.sampler,
                              });
    cubey::vulkan::Buffer staging(device, cubey::vulkan::staging_buffer_config(
                                              static_cast<VkDeviceSize>(config.rgba8.size())));
    staging.upload(config.rgba8.data(), static_cast<VkDeviceSize>(config.rgba8.size()));

    static_cast<void>(gpu.submit_and_wait({
        .label = "upload texture 2D",
        .work =
            [source = staging.handle(), destination = texture.handle(),
             extent = texture.image().extent()](cubey::vulkan::GpuOwnerContext& context) {
                cubey::vulkan::ImmediateCommands commands(context);
                cubey::vulkan::transition_image_layout(
                    commands.command_buffer(),
                    cubey::vulkan::begin_transfer_dst_transition(destination));
                const VkBufferImageCopy copy = cubey::vulkan::buffer_image_copy(extent);
                vkCmdCopyBufferToImage(commands.command_buffer(), source, destination,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                cubey::vulkan::transition_image_layout(
                    commands.command_buffer(),
                    cubey::vulkan::finish_transfer_dst_for_sampling_transition(destination));
                commands.submit_and_wait();
            },
    }));
    return texture;
}

TextureCube create_uploaded_texture_cube(const cubey::vulkan::Device& device,
                                         cubey::vulkan::GpuRuntime& gpu,
                                         const UploadedTextureCubeConfig& config) {
    validate_config(TextureCubeConfig{
        .extent = config.extent,
        .mip_levels = config.mip_levels,
        .format = config.format,
        .create_sampler = config.create_sampler,
        .sampler = config.sampler,
    });
    const std::size_t texel_bytes = texture_format_byte_size(config.format);
    const std::size_t expected_size =
        texture_cube_byte_size(config.extent, config.mip_levels, texel_bytes);
    if (config.bytes.size() != expected_size) {
        throw std::runtime_error("uploaded texture cube byte count must match extent and mips");
    }

    TextureCube texture(device, TextureCubeConfig{
                                    .extent = config.extent,
                                    .mip_levels = config.mip_levels,
                                    .format = config.format,
                                    .create_sampler = config.create_sampler,
                                    .sampler = config.sampler,
                                });
    cubey::vulkan::Buffer staging(device, cubey::vulkan::staging_buffer_config(
                                              static_cast<VkDeviceSize>(config.bytes.size())));
    staging.upload(config.bytes.data(), static_cast<VkDeviceSize>(config.bytes.size()));

    std::vector<VkBufferImageCopy> copies;
    copies.reserve(static_cast<std::size_t>(config.mip_levels) * 6U);
    VkDeviceSize offset = 0;
    for (std::uint32_t mip = 0; mip < config.mip_levels; ++mip) {
        const std::uint32_t mip_extent = texture_cube_mip_extent(config.extent, mip);
        const VkDeviceSize face_bytes = static_cast<VkDeviceSize>(
            checked_mul(checked_mul(static_cast<std::size_t>(mip_extent),
                                    static_cast<std::size_t>(mip_extent),
                                    "texture cube upload face texel count overflows"),
                        texel_bytes, "texture cube upload face byte size overflows"));
        for (std::uint32_t face = 0; face < 6; ++face) {
            copies.push_back(cubey::vulkan::buffer_image_copy(
                cubey::vulkan::BufferImageCopyConfig{
                    .extent = {mip_extent, mip_extent, 1},
                    .buffer_offset = offset,
                    .mip_level = mip,
                    .base_array_layer = face,
                    .layer_count = 1,
                }));
            offset += face_bytes;
        }
    }

    static_cast<void>(gpu.submit_and_wait({
        .label = "upload texture cube",
        .work =
            [source = staging.handle(), destination = texture.handle(),
             mip_levels = config.mip_levels, copies](cubey::vulkan::GpuOwnerContext& context) {
                cubey::vulkan::ImmediateCommands commands(context);
                cubey::vulkan::transition_image_layout(
                    commands.command_buffer(),
                    cubey::vulkan::begin_transfer_dst_transition(destination, mip_levels, 6));
                vkCmdCopyBufferToImage(commands.command_buffer(), source, destination,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                       static_cast<std::uint32_t>(copies.size()), copies.data());
                cubey::vulkan::transition_image_layout(
                    commands.command_buffer(),
                    cubey::vulkan::finish_transfer_dst_for_sampling_transition(destination,
                                                                               mip_levels, 6));
                commands.submit_and_wait();
            },
    }));
    return texture;
}

} // namespace cubey::render
