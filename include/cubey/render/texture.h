#pragma once

#include <cubey/vulkan/device.h>
#include <cubey/vulkan/image.h>
#include <cubey/vulkan/sampler.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>

namespace cubey::render {

enum class Texture2DUsage : std::uint8_t {
    StorageSampled,
    TransferSampled,
};

struct Texture2DConfig {
    VkExtent2D extent{1, 1};
    VkFormat format = VK_FORMAT_UNDEFINED;
    Texture2DUsage usage = Texture2DUsage::TransferSampled;
    bool create_sampler = false;
    cubey::vulkan::SamplerConfig sampler;
};

struct DepthTextureConfig {
    VkExtent2D extent{1, 1};
    VkFormat format = VK_FORMAT_UNDEFINED;
    bool create_sampler = false;
    cubey::vulkan::SamplerConfig sampler;
};

[[nodiscard]] cubey::vulkan::ImageConfig texture_2d_image_config(const Texture2DConfig& config);
[[nodiscard]] cubey::vulkan::ImageConfig
depth_texture_image_config(const DepthTextureConfig& config);

class Texture2D {
  public:
    Texture2D(const cubey::vulkan::Device& device, const Texture2DConfig& config);
    ~Texture2D() = default;

    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;
    Texture2D(Texture2D&& other) noexcept = default;
    Texture2D& operator=(Texture2D&& other) noexcept = default;

    [[nodiscard]] VkImage handle() const {
        return image_.handle();
    }
    [[nodiscard]] VkImageView view() const {
        return image_.view();
    }
    [[nodiscard]] VkFormat format() const {
        return image_.format();
    }
    [[nodiscard]] VkExtent2D extent() const;
    [[nodiscard]] bool has_sampler() const {
        return sampler_.has_value();
    }
    [[nodiscard]] const cubey::vulkan::Sampler& sampler() const;
    [[nodiscard]] const cubey::vulkan::Image& image() const {
        return image_;
    }

  private:
    cubey::vulkan::Image image_;
    std::optional<cubey::vulkan::Sampler> sampler_;
};

class DepthTexture {
  public:
    DepthTexture(const cubey::vulkan::Device& device, const DepthTextureConfig& config);
    ~DepthTexture() = default;

    DepthTexture(const DepthTexture&) = delete;
    DepthTexture& operator=(const DepthTexture&) = delete;
    DepthTexture(DepthTexture&& other) noexcept = default;
    DepthTexture& operator=(DepthTexture&& other) noexcept = default;

    [[nodiscard]] VkImage handle() const {
        return image_.handle();
    }
    [[nodiscard]] VkImageView view() const {
        return image_.view();
    }
    [[nodiscard]] VkFormat format() const {
        return image_.format();
    }
    [[nodiscard]] VkExtent2D extent() const;
    [[nodiscard]] bool has_sampler() const {
        return sampler_.has_value();
    }
    [[nodiscard]] const cubey::vulkan::Sampler& sampler() const;
    [[nodiscard]] const cubey::vulkan::Image& image() const {
        return image_;
    }

  private:
    cubey::vulkan::Image image_;
    std::optional<cubey::vulkan::Sampler> sampler_;
};

} // namespace cubey::render
