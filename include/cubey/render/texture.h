#pragma once

#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/image.h>
#include <cubey/vulkan/sampler.h>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace cubey::render {

enum class Texture2DUsage : std::uint8_t {
    ColorAttachmentSampled,
    StorageSampled,
    TransferSampled,
};

struct Texture2DConfig {
    VkExtent2D extent{1, 1};
    std::uint32_t mip_levels = 1;
    VkFormat format = VK_FORMAT_UNDEFINED;
    Texture2DUsage usage = Texture2DUsage::TransferSampled;
    bool create_sampler = false;
    cubey::vulkan::SamplerConfig sampler;
};

struct Texture3DConfig {
    VkExtent3D extent{1, 1, 1};
    VkFormat format = VK_FORMAT_UNDEFINED;
    bool create_sampler = false;
    cubey::vulkan::SamplerConfig sampler;
};

enum class TextureCubeUsage : std::uint8_t {
    TransferSampled,
    ColorAttachmentSampled,
};

struct TextureFormatLayout {
    std::uint32_t block_width = 1;
    std::uint32_t block_height = 1;
    std::size_t bytes_per_block = 0;
    bool compressed = false;
};

struct TextureCubeConfig {
    std::uint32_t extent = 1;
    std::uint32_t mip_levels = 1;
    VkFormat format = VK_FORMAT_UNDEFINED;
    TextureCubeUsage usage = TextureCubeUsage::TransferSampled;
    bool create_sampler = false;
    cubey::vulkan::SamplerConfig sampler;
};

struct DepthTextureConfig {
    VkExtent2D extent{1, 1};
    VkFormat format = VK_FORMAT_UNDEFINED;
    bool create_sampler = false;
    cubey::vulkan::SamplerConfig sampler;
};

struct UploadedTexture2DMip {
    VkExtent2D extent{1, 1};
    VkDeviceSize byte_offset = 0;
    std::size_t byte_count = 0;
};

struct UploadedTexture2DConfig {
    VkExtent2D extent{1, 1};
    std::uint32_t mip_levels = 1;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    std::span<const std::uint8_t> rgba8{};
    std::span<const std::uint8_t> bytes{};
    std::span<const UploadedTexture2DMip> mips{};
    bool create_sampler = true;
    cubey::vulkan::SamplerConfig sampler;
};

struct UploadedTextureCubeConfig {
    std::uint32_t extent = 1;
    std::uint32_t mip_levels = 1;
    VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
    std::span<const std::uint8_t> bytes{};
    bool create_sampler = true;
    cubey::vulkan::SamplerConfig sampler;
};

[[nodiscard]] cubey::vulkan::ImageConfig texture_2d_image_config(const Texture2DConfig& config);
[[nodiscard]] cubey::vulkan::ImageConfig texture_3d_image_config(const Texture3DConfig& config);
[[nodiscard]] cubey::vulkan::ImageConfig texture_cube_image_config(const TextureCubeConfig& config);
[[nodiscard]] cubey::vulkan::ImageConfig
depth_texture_image_config(const DepthTextureConfig& config);
[[nodiscard]] TextureFormatLayout texture_format_layout(VkFormat format);
[[nodiscard]] std::size_t texture_format_byte_size(VkFormat format);
[[nodiscard]] VkExtent2D texture_2d_mip_extent(VkExtent2D extent, std::uint32_t mip_level);
[[nodiscard]] std::size_t texture_2d_byte_size(VkExtent2D extent, std::uint32_t mip_levels,
                                               VkFormat format);
[[nodiscard]] std::uint32_t texture_cube_mip_extent(std::uint32_t extent, std::uint32_t mip_level);
[[nodiscard]] std::size_t texture_cube_byte_size(std::uint32_t extent, std::uint32_t mip_levels,
                                                 std::size_t texel_bytes);

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

class Texture3D {
  public:
    Texture3D(const cubey::vulkan::Device& device, const Texture3DConfig& config);
    ~Texture3D() = default;

    Texture3D(const Texture3D&) = delete;
    Texture3D& operator=(const Texture3D&) = delete;
    Texture3D(Texture3D&& other) noexcept = default;
    Texture3D& operator=(Texture3D&& other) noexcept = default;

    [[nodiscard]] VkImage handle() const {
        return image_.handle();
    }
    [[nodiscard]] VkImageView view() const {
        return image_.view();
    }
    [[nodiscard]] VkFormat format() const {
        return image_.format();
    }
    [[nodiscard]] VkExtent3D extent() const {
        return image_.extent();
    }
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

class TextureCube {
  public:
    TextureCube(const cubey::vulkan::Device& device, const TextureCubeConfig& config);
    ~TextureCube() = default;

    TextureCube(const TextureCube&) = delete;
    TextureCube& operator=(const TextureCube&) = delete;
    TextureCube(TextureCube&& other) noexcept = default;
    TextureCube& operator=(TextureCube&& other) noexcept = default;

    [[nodiscard]] VkImage handle() const {
        return image_.handle();
    }
    [[nodiscard]] VkImageView view() const {
        return image_.view();
    }
    [[nodiscard]] VkFormat format() const {
        return image_.format();
    }
    [[nodiscard]] std::uint32_t extent() const {
        return image_.extent().width;
    }
    [[nodiscard]] std::uint32_t mip_levels() const {
        return image_.mip_levels();
    }
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

[[nodiscard]] Texture2D create_uploaded_texture_2d(const cubey::vulkan::Device& device,
                                                   cubey::vulkan::GpuRuntime& gpu,
                                                   const UploadedTexture2DConfig& config);
[[nodiscard]] TextureCube create_uploaded_texture_cube(const cubey::vulkan::Device& device,
                                                       cubey::vulkan::GpuRuntime& gpu,
                                                       const UploadedTextureCubeConfig& config);
[[nodiscard]] cubey::vulkan::ImageView create_texture_cube_face_view(
    const cubey::vulkan::Device& device, const TextureCube& texture, std::uint32_t mip_level,
    std::uint32_t face_index);

} // namespace cubey::render
