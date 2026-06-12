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
    if (config.mip_levels == 0) {
        throw std::runtime_error("texture mip level count must be nonzero");
    }
    if (config.format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("texture format must be defined");
    }
}

void validate_config(const Texture3DConfig& config) {
    if (config.extent.width == 0 || config.extent.height == 0 || config.extent.depth == 0) {
        throw std::runtime_error("texture 3D extent must be nonzero");
    }
    if (config.format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("texture 3D format must be defined");
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

[[nodiscard]] std::size_t checked_mul(std::size_t lhs, std::size_t rhs, const char* message) {
    if (lhs != 0 && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
        throw std::runtime_error(message);
    }
    return lhs * rhs;
}

[[nodiscard]] std::size_t checked_add(std::size_t lhs, std::size_t rhs, const char* message) {
    if (lhs > std::numeric_limits<std::size_t>::max() - rhs) {
        throw std::runtime_error(message);
    }
    return lhs + rhs;
}

[[nodiscard]] std::uint32_t div_round_up(std::uint32_t value, std::uint32_t divisor) {
    if (divisor == 0) {
        throw std::runtime_error("division by zero");
    }
    if (value == 0) {
        return 0;
    }
    return 1U + ((value - 1U) / divisor);
}

[[nodiscard]] std::uint32_t mip_extent(std::uint32_t extent, std::uint32_t mip_level) {
    if (mip_level >= std::numeric_limits<std::uint32_t>::digits) {
        return 1;
    }
    return std::max(1U, extent >> mip_level);
}

} // namespace

cubey::vulkan::ImageConfig texture_2d_image_config(const Texture2DConfig& config) {
    validate_config(config);
    cubey::vulkan::ImageConfig image_config{};
    switch (config.usage) {
    case Texture2DUsage::ColorAttachmentSampled:
        image_config = cubey::vulkan::color_attachment_sampled_image_config(config.extent,
                                                                            config.format);
        break;
    case Texture2DUsage::StorageSampled:
        image_config = cubey::vulkan::storage_sampled_image_config(config.extent, config.format);
        break;
    case Texture2DUsage::TransferSampled:
        image_config = cubey::vulkan::transfer_sampled_image_config(config.extent, config.format);
        break;
    default:
        throw std::runtime_error("unsupported texture usage");
    }
    image_config.mip_levels = config.mip_levels;
    return image_config;
}

cubey::vulkan::ImageConfig texture_3d_image_config(const Texture3DConfig& config) {
    validate_config(config);
    return cubey::vulkan::storage_sampled_volume_image_config(config.extent, config.format);
}

cubey::vulkan::ImageConfig texture_cube_image_config(const TextureCubeConfig& config) {
    validate_config(config);
    switch (config.usage) {
    case TextureCubeUsage::TransferSampled:
        return cubey::vulkan::transfer_sampled_cube_image_config(config.extent, config.mip_levels,
                                                                 config.format);
    case TextureCubeUsage::ColorAttachmentSampled:
        return cubey::vulkan::color_attachment_sampled_cube_image_config(
            config.extent, config.mip_levels, config.format);
    }
    throw std::runtime_error("unsupported texture cube usage");
}

cubey::vulkan::ImageConfig depth_texture_image_config(const DepthTextureConfig& config) {
    validate_config(config);
    cubey::vulkan::ImageConfig image_config =
        cubey::vulkan::depth_image_config(config.extent, config.format);
    image_config.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    return image_config;
}

TextureFormatLayout texture_format_layout(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
        return {
            .block_width = 1,
            .block_height = 1,
            .bytes_per_block = 4,
            .compressed = false,
        };
    case VK_FORMAT_R32_SFLOAT:
        return {
            .block_width = 1,
            .block_height = 1,
            .bytes_per_block = 4,
            .compressed = false,
        };
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return {
            .block_width = 1,
            .block_height = 1,
            .bytes_per_block = 8,
            .compressed = false,
        };
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return {
            .block_width = 1,
            .block_height = 1,
            .bytes_per_block = 16,
            .compressed = false,
        };
    case VK_FORMAT_BC7_UNORM_BLOCK:
    case VK_FORMAT_BC7_SRGB_BLOCK:
        return {
            .block_width = 4,
            .block_height = 4,
            .bytes_per_block = 16,
            .compressed = true,
        };
    default:
        throw std::runtime_error("texture format layout is not known");
    }
}

std::size_t texture_format_byte_size(VkFormat format) {
    return texture_format_layout(format).bytes_per_block;
}

VkExtent2D texture_2d_mip_extent(VkExtent2D extent, std::uint32_t mip_level) {
    if (extent.width == 0 || extent.height == 0) {
        throw std::runtime_error("texture 2D mip extent requires a nonzero base extent");
    }
    return {
        .width = mip_extent(extent.width, mip_level),
        .height = mip_extent(extent.height, mip_level),
    };
}

std::size_t texture_2d_byte_size(VkExtent2D extent, std::uint32_t mip_levels, VkFormat format) {
    if (extent.width == 0 || extent.height == 0 || mip_levels == 0) {
        throw std::runtime_error("texture 2D byte size requires nonzero inputs");
    }
    const TextureFormatLayout layout = texture_format_layout(format);
    std::size_t total = 0;
    for (std::uint32_t mip = 0; mip < mip_levels; ++mip) {
        const VkExtent2D mip_extent = texture_2d_mip_extent(extent, mip);
        const std::size_t blocks_x = div_round_up(mip_extent.width, layout.block_width);
        const std::size_t blocks_y = div_round_up(mip_extent.height, layout.block_height);
        const std::size_t blocks =
            checked_mul(blocks_x, blocks_y, "texture 2D mip block count overflows");
        const std::size_t mip_bytes =
            checked_mul(blocks, layout.bytes_per_block, "texture 2D mip byte size overflows");
        total = checked_add(total, mip_bytes, "texture 2D byte size overflows");
    }
    return total;
}

std::uint32_t texture_cube_mip_extent(std::uint32_t extent, std::uint32_t mip_level) {
    if (extent == 0) {
        throw std::runtime_error("texture cube mip extent requires a nonzero base extent");
    }
    return mip_extent(extent, mip_level);
}

std::size_t texture_cube_byte_size(std::uint32_t extent, std::uint32_t mip_levels,
                                   std::size_t texel_bytes) {
    if (extent == 0 || mip_levels == 0 || texel_bytes == 0) {
        throw std::runtime_error("texture cube byte size requires nonzero inputs");
    }
    std::size_t total = 0;
    for (std::uint32_t mip = 0; mip < mip_levels; ++mip) {
        const std::uint32_t mip_extent = texture_cube_mip_extent(extent, mip);
        const std::size_t texels =
            checked_mul(static_cast<std::size_t>(mip_extent), static_cast<std::size_t>(mip_extent),
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

Texture3D::Texture3D(const cubey::vulkan::Device& device, const Texture3DConfig& config)
    : image_(device, texture_3d_image_config(config)) {
    if (config.create_sampler) {
        sampler_.emplace(device, config.sampler);
    }
}

const cubey::vulkan::Sampler& Texture3D::sampler() const {
    if (!sampler_.has_value()) {
        throw std::runtime_error("texture 3D has no sampler");
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
        .mip_levels = config.mip_levels,
        .format = config.format,
        .usage = Texture2DUsage::TransferSampled,
        .create_sampler = config.create_sampler,
        .sampler = config.sampler,
    });
    if (!config.bytes.empty() && !config.rgba8.empty()) {
        throw std::runtime_error("uploaded texture helper accepts one source byte span");
    }
    const std::span<const std::uint8_t> source = config.bytes.empty() ? config.rgba8 : config.bytes;

    std::vector<UploadedTexture2DMip> mips;
    if (config.mips.empty()) {
        VkDeviceSize offset = 0;
        mips.reserve(config.mip_levels);
        for (std::uint32_t mip = 0; mip < config.mip_levels; ++mip) {
            const VkExtent2D mip_extent = texture_2d_mip_extent(config.extent, mip);
            const std::size_t mip_bytes = texture_2d_byte_size(mip_extent, 1, config.format);
            mips.push_back(UploadedTexture2DMip{
                .extent = mip_extent,
                .byte_offset = offset,
                .byte_count = mip_bytes,
            });
            offset += static_cast<VkDeviceSize>(mip_bytes);
        }
    } else {
        if (config.mips.size() != config.mip_levels) {
            throw std::runtime_error("uploaded texture mip region count must match mip levels");
        }
        mips.assign(config.mips.begin(), config.mips.end());
    }

    std::size_t total_mip_bytes = 0;
    for (std::uint32_t mip = 0; mip < config.mip_levels; ++mip) {
        const UploadedTexture2DMip& upload_mip = mips[mip];
        const VkExtent2D expected_extent = texture_2d_mip_extent(config.extent, mip);
        if (upload_mip.extent.width != expected_extent.width ||
            upload_mip.extent.height != expected_extent.height) {
            throw std::runtime_error("uploaded texture mip extent must match base extent");
        }
        const std::size_t expected_size = texture_2d_byte_size(upload_mip.extent, 1, config.format);
        if (upload_mip.byte_count != expected_size) {
            throw std::runtime_error("uploaded texture mip byte count must match format");
        }
        const std::size_t offset = static_cast<std::size_t>(upload_mip.byte_offset);
        const std::size_t end =
            checked_add(offset, upload_mip.byte_count, "uploaded texture mip offset overflows");
        if (end > source.size()) {
            throw std::runtime_error("uploaded texture mip region exceeds source bytes");
        }
        total_mip_bytes = checked_add(total_mip_bytes, upload_mip.byte_count,
                                      "uploaded texture mip byte total overflows");
    }
    if (config.mips.empty() && source.size() != total_mip_bytes) {
        throw std::runtime_error("uploaded texture byte count must match format, extent, and mips");
    }

    Texture2D texture(device, Texture2DConfig{
                                  .extent = config.extent,
                                  .mip_levels = config.mip_levels,
                                  .format = config.format,
                                  .usage = Texture2DUsage::TransferSampled,
                                  .create_sampler = config.create_sampler,
                                  .sampler = config.sampler,
                              });
    cubey::vulkan::Buffer staging(
        device, cubey::vulkan::staging_buffer_config(static_cast<VkDeviceSize>(source.size())));
    staging.upload(source.data(), static_cast<VkDeviceSize>(source.size()));

    std::vector<VkBufferImageCopy> copies;
    copies.reserve(config.mip_levels);
    for (std::uint32_t mip = 0; mip < config.mip_levels; ++mip) {
        const UploadedTexture2DMip& upload_mip = mips[mip];
        copies.push_back(cubey::vulkan::buffer_image_copy(cubey::vulkan::BufferImageCopyConfig{
            .extent = {upload_mip.extent.width, upload_mip.extent.height, 1},
            .buffer_offset = upload_mip.byte_offset,
            .mip_level = mip,
            .base_array_layer = 0,
            .layer_count = 1,
        }));
    }

    static_cast<void>(gpu.submit_and_wait({
        .label = "upload texture 2D",
        .work =
            [source = staging.handle(), destination = texture.handle(),
             mip_levels = config.mip_levels, copies](cubey::vulkan::GpuOwnerContext& context) {
                cubey::vulkan::ImmediateCommands commands(context);
                cubey::vulkan::transition_image_layout(
                    commands.command_buffer(),
                    cubey::vulkan::begin_transfer_dst_transition(destination, mip_levels, 1));
                vkCmdCopyBufferToImage(commands.command_buffer(), source, destination,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                       static_cast<std::uint32_t>(copies.size()), copies.data());
                cubey::vulkan::transition_image_layout(
                    commands.command_buffer(),
                    cubey::vulkan::finish_transfer_dst_for_sampling_transition(destination,
                                                                               mip_levels, 1));
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
        const VkDeviceSize face_bytes = static_cast<VkDeviceSize>(checked_mul(
            checked_mul(static_cast<std::size_t>(mip_extent), static_cast<std::size_t>(mip_extent),
                        "texture cube upload face texel count overflows"),
            texel_bytes, "texture cube upload face byte size overflows"));
        for (std::uint32_t face = 0; face < 6; ++face) {
            copies.push_back(cubey::vulkan::buffer_image_copy(cubey::vulkan::BufferImageCopyConfig{
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

cubey::vulkan::ImageView create_texture_cube_face_view(const cubey::vulkan::Device& device,
                                                       const TextureCube& texture,
                                                       std::uint32_t mip_level,
                                                       std::uint32_t face_index) {
    if (mip_level >= texture.mip_levels()) {
        throw std::runtime_error("texture cube face view mip level is out of range");
    }
    if (face_index >= 6U) {
        throw std::runtime_error("texture cube face view face index is out of range");
    }
    return cubey::vulkan::ImageView(
        device, cubey::vulkan::ImageViewConfig{
                    .image = texture.handle(),
                    .format = texture.format(),
                    .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                    .view_type = VK_IMAGE_VIEW_TYPE_2D,
                    .base_mip_level = mip_level,
                    .level_count = 1,
                    .base_array_layer = face_index,
                    .layer_count = 1,
                });
}

} // namespace cubey::render
