#include <cubey/render/texture.h>

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

} // namespace cubey::render
