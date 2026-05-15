#include "gltf_basisu_texture.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include <basisu_transcoder.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cstddef>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>

namespace cubey {
namespace {

void ensure_basisu_transcoder_initialized() {
    static std::once_flag once;
    std::call_once(once, [] { basist::basisu_transcoder_init(); });
}

[[nodiscard]] basist::transcoder_texture_format basisu_target_format(bool use_bc_upload) {
    return use_bc_upload ? basist::transcoder_texture_format::cTFBC7_RGBA
                         : basist::transcoder_texture_format::cTFRGBA32;
}

[[nodiscard]] std::uint32_t checked_u32_size(std::size_t value, const char* label) {
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error(std::string(label) + " is too large for BasisU");
    }
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::uint32_t checked_u32_mul(std::uint32_t lhs, std::uint32_t rhs,
                                            const char* label) {
    if (lhs != 0 && rhs > std::numeric_limits<std::uint32_t>::max() / lhs) {
        throw std::runtime_error(label);
    }
    return lhs * rhs;
}

[[nodiscard]] std::size_t checked_size_add(std::size_t lhs, std::size_t rhs,
                                           const char* label) {
    if (lhs > std::numeric_limits<std::size_t>::max() - rhs) {
        throw std::runtime_error(label);
    }
    return lhs + rhs;
}

} // namespace

VkFormat gltf_basisu_texture_upload_format(asset::GltfTextureColorSpace color_space,
                                           bool use_bc_compression) {
    if (use_bc_compression) {
        return color_space == asset::GltfTextureColorSpace::Srgb ? VK_FORMAT_BC7_SRGB_BLOCK
                                                                 : VK_FORMAT_BC7_UNORM_BLOCK;
    }
    return color_space == asset::GltfTextureColorSpace::Srgb ? VK_FORMAT_R8G8B8A8_SRGB
                                                             : VK_FORMAT_R8G8B8A8_UNORM;
}

GltfBasisuTextureUpload transcode_gltf_basisu_texture(
    const asset::GltfImage& image, asset::GltfTextureColorSpace color_space,
    bool use_bc_compression) {
    if (image.encoding != asset::GltfImageEncoding::Ktx2Basisu) {
        throw std::runtime_error("BasisU transcode requires a KTX2 BasisU image");
    }
    if (image.encoded_bytes.empty()) {
        throw std::runtime_error("BasisU transcode requires encoded KTX2 bytes");
    }

    ensure_basisu_transcoder_initialized();

    basist::ktx2_transcoder transcoder;
    const std::uint32_t data_size =
        checked_u32_size(image.encoded_bytes.size(), "KTX2 texture data");
    if (!transcoder.init(image.encoded_bytes.data(), data_size)) {
        throw std::runtime_error("failed to parse KTX2 BasisU texture");
    }
    if (transcoder.get_faces() != 1 || transcoder.get_layers() != 0 || transcoder.is_hdr()) {
        throw std::runtime_error("only 2D LDR KTX2 BasisU material textures are supported");
    }
    const bool bc7_available =
        use_bc_compression &&
        basist::basis_is_format_supported(basist::transcoder_texture_format::cTFBC7_RGBA,
                                          transcoder.get_basis_tex_format());
    if (!bc7_available &&
        !basist::basis_is_format_supported(basist::transcoder_texture_format::cTFRGBA32,
                                           transcoder.get_basis_tex_format())) {
        throw std::runtime_error("BasisU target texture format is not supported");
    }
    if (!transcoder.start_transcoding()) {
        throw std::runtime_error("failed to start KTX2 BasisU transcoding");
    }

    const basist::transcoder_texture_format target_format =
        basisu_target_format(bc7_available);
    const std::uint32_t mip_levels = std::max(1U, transcoder.get_levels());

    GltfBasisuTextureUpload result{
        .extent = {transcoder.get_width(), transcoder.get_height()},
        .mip_levels = mip_levels,
        .format = gltf_basisu_texture_upload_format(color_space, bc7_available),
    };
    result.mips.reserve(mip_levels);

    for (std::uint32_t mip = 0; mip < mip_levels; ++mip) {
        basist::ktx2_image_level_info info{};
        if (!transcoder.get_image_level_info(info, mip, 0, 0)) {
            throw std::runtime_error("failed to read KTX2 BasisU mip information");
        }
        const std::uint32_t byte_count = basist::basis_compute_transcoded_image_size_in_bytes(
            target_format, info.m_orig_width, info.m_orig_height);
        const std::size_t offset = result.bytes.size();
        result.bytes.resize(checked_size_add(offset, byte_count,
                                             "KTX2 BasisU transcode byte size overflows"));

        const std::uint32_t output_units =
            bc7_available
                ? info.m_total_blocks
                : checked_u32_mul(info.m_orig_width, info.m_orig_height,
                                  "KTX2 BasisU output pixel count overflows");
        const std::uint32_t decode_flags =
            bc7_available ? basist::cDecodeFlagsHighQuality : 0U;
        if (!transcoder.transcode_image_level(mip, 0, 0, result.bytes.data() + offset,
                                              output_units, target_format, decode_flags)) {
            throw std::runtime_error("failed to transcode KTX2 BasisU mip");
        }

        result.mips.push_back(render::UploadedTexture2DMip{
            .extent = {info.m_orig_width, info.m_orig_height},
            .byte_offset = static_cast<VkDeviceSize>(offset),
            .byte_count = byte_count,
        });
    }

    return result;
}

} // namespace cubey
