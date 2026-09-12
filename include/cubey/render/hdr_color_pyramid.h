#pragma once

#include <cubey/render/frame_data.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/target.h>
#include <cubey/render/texture.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace cubey::render {

// A full-resolution, linear HDR radiance copy followed by a trilinear-filtered
// mip chain. It intentionally owns per-frame-slot images: a later forward
// transmission pass can sample the same-frame opaque scene without racing an
// in-flight frame.
struct HdrColorPyramidConfig {
    VkExtent2D extent{1U, 1U};
    VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
    std::uint32_t frame_slot_count = 1U;
    std::uint32_t minimum_mip_extent = 16U;
};

struct HdrColorPyramidPipelineConfig {
    ShaderStageFile vertex{};
    ShaderStageFile fragment{};
};

struct HdrColorPyramidSnapshot {
    const Texture2D* texture = nullptr;
    float max_lod = 0.0F;
    // Valid means record() has emitted radiance commands for this frame slot;
    // it does not imply that the GPU has completed them. A non-null texture is
    // still bindable while the current command buffer produces it before a
    // later consumer pass.
    bool valid = false;

    [[nodiscard]] bool bindable() const noexcept {
        return texture != nullptr;
    }
};

[[nodiscard]] MaterialPassInfo hdr_color_pyramid_filter_pass_info();
void validate_hdr_color_pyramid_config(const HdrColorPyramidConfig& config);
[[nodiscard]] std::uint32_t hdr_color_pyramid_mip_levels(const HdrColorPyramidConfig& config);

class HdrColorPyramid {
  public:
    HdrColorPyramid() = default;

    HdrColorPyramid(const HdrColorPyramid&) = delete;
    HdrColorPyramid& operator=(const HdrColorPyramid&) = delete;
    HdrColorPyramid(HdrColorPyramid&&) = delete;
    HdrColorPyramid& operator=(HdrColorPyramid&&) = delete;

    void create_resources(const cubey::vulkan::Device& device, const HdrColorPyramidConfig& config);
    void create_pipeline(const cubey::vulkan::Device& device,
                         const HdrColorPyramidPipelineConfig& config);
    void destroy();

    // The source must already be readable by the pass which calls record().
    // Descriptor mutation is deliberately separate from recording because a
    // render-graph transient supplies the source view at frame preparation.
    void update_source_descriptor(const cubey::vulkan::Device& device, FrameSlot frame_slot,
                                  VkSampler sampler, VkImageView source_view,
                                  VkImageLayout source_layout);

    // Copy the externally supplied scene radiance into mip zero and retain the
    // mip as a color attachment. Call source_target() to composite additional
    // radiance into that isolated source, then call record_remaining_mips()
    // before sampling the pyramid.
    void record_source_copy(const cubey::vulkan::CommandRecorder& recorder, FrameSlot frame_slot);
    [[nodiscard]] ColorTargetView source_target(FrameSlot frame_slot) const;
    void record_remaining_mips(const cubey::vulkan::CommandRecorder& recorder,
                               FrameSlot frame_slot);

    // Convenience path for callers that do not need to inject content into
    // mip zero between the copy and downsample stages.
    void record(const cubey::vulkan::CommandRecorder& recorder, FrameSlot frame_slot);

    [[nodiscard]] HdrColorPyramidSnapshot snapshot(FrameSlot frame_slot) const;
    [[nodiscard]] bool resources_created() const noexcept;
    [[nodiscard]] bool pipeline_created() const noexcept;
    [[nodiscard]] const HdrColorPyramidConfig& config() const noexcept {
        return config_;
    }

  private:
    struct Buffer {
        std::optional<Texture2D> texture{};
        std::vector<cubey::vulkan::ImageView> mip_views{};
        bool initialized = false;
        bool valid = false;
        bool source_copy_pending = false;
    };

    void transition_mip(const cubey::vulkan::CommandRecorder& recorder, VkImage image,
                        std::uint32_t mip_level, VkImageLayout old_layout, VkImageLayout new_layout,
                        VkAccessFlags src_access, VkAccessFlags dst_access,
                        VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage) const;
    [[nodiscard]] Buffer& buffer(FrameSlot frame_slot);
    [[nodiscard]] const Buffer& buffer(FrameSlot frame_slot) const;
    [[nodiscard]] MaterialInstance& filter_material(std::uint32_t mip_level) const;

    HdrColorPyramidConfig config_{};
    std::vector<Buffer> buffers_{};
    std::vector<std::unique_ptr<MaterialInstance>> filter_materials_{};
    std::optional<GraphicsPipelineResource> filter_pipeline_{};
};

} // namespace cubey::render
