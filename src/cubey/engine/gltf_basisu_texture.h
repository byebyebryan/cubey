#pragma once

#include <cubey/asset/gltf_asset.h>
#include <cubey/render/texture.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace cubey {

struct GltfBasisuTextureUpload {
    VkExtent2D extent{1, 1};
    std::uint32_t mip_levels = 1;
    VkFormat format = VK_FORMAT_UNDEFINED;
    std::vector<std::uint8_t> bytes{};
    std::vector<render::UploadedTexture2DMip> mips{};
};

[[nodiscard]] VkFormat gltf_basisu_texture_upload_format(asset::GltfTextureColorSpace color_space,
                                                         bool use_bc_compression);

[[nodiscard]] GltfBasisuTextureUpload transcode_gltf_basisu_texture(
    const asset::GltfImage& image, asset::GltfTextureColorSpace color_space,
    bool use_bc_compression);

} // namespace cubey
