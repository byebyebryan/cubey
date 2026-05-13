#include <cubey/render/texture.h>

#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/immediate_commands.h>

#include <stdexcept>

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

void validate_config(const DepthTextureConfig& config) {
    if (config.extent.width == 0 || config.extent.height == 0) {
        throw std::runtime_error("depth texture extent must be nonzero");
    }
    if (config.format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("depth texture format must be defined");
    }
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

cubey::vulkan::ImageConfig depth_texture_image_config(const DepthTextureConfig& config) {
    validate_config(config);
    cubey::vulkan::ImageConfig image_config =
        cubey::vulkan::depth_image_config(config.extent, config.format);
    image_config.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    return image_config;
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

} // namespace cubey::render
