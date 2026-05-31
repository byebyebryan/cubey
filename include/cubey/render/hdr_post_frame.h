#pragma once

#include <cubey/render/frame_data.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/pbr.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/render_graph.h>
#include <cubey/render/target.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/sampler.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace cubey::render {

struct HdrPostFrameMaterialConfig {
    std::uint32_t frame_slot_count = 1;
};

struct HdrPostFramePipelineConfig {
    VkExtent2D extent{};
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    std::span<const ShaderStageFile> shader_stage_files{};
};

[[nodiscard]] RenderGraphTextureDesc hdr_scene_color_texture_desc(std::string label,
                                                                  VkExtent2D extent,
                                                                  VkFormat format);
[[nodiscard]] PbrPostUniforms hdr_post_uniforms(VkFormat color_format, float exposure = 0.0F,
                                                PbrTonemap tonemap = PbrTonemap::Aces);

class HdrPostFrame {
  public:
    HdrPostFrame() = default;

    HdrPostFrame(const HdrPostFrame&) = delete;
    HdrPostFrame& operator=(const HdrPostFrame&) = delete;
    HdrPostFrame(HdrPostFrame&&) = delete;
    HdrPostFrame& operator=(HdrPostFrame&&) = delete;

    void create_materials(const cubey::vulkan::Device& device,
                          const HdrPostFrameMaterialConfig& config);
    void create_pipeline(const cubey::vulkan::Device& device,
                         const HdrPostFramePipelineConfig& config);
    void destroy_pipeline();
    void destroy();

    void upload(FrameSlot frame_slot, const PbrPostUniforms& uniforms) const;
    void update_scene_color_descriptor(const cubey::vulkan::Device& device, FrameSlot frame_slot,
                                       const CompiledRenderGraph& graph,
                                       const RenderGraphResourceSet& resources,
                                       RenderGraphTextureHandle scene_color) const;
    void record_pass(const cubey::vulkan::CommandRecorder& recorder, ColorTargetView target,
                     FrameSlot frame_slot) const;

    [[nodiscard]] const FrameUniformMaterialInstance<PbrPostUniforms>& material() const;
    [[nodiscard]] const GraphicsPipelineResource& pipeline() const;
    [[nodiscard]] const cubey::vulkan::Sampler& sampler() const;

  private:
    std::optional<FrameUniformMaterialInstance<PbrPostUniforms>> material_;
    std::optional<GraphicsPipelineResource> pipeline_;
    std::optional<cubey::vulkan::Sampler> sampler_;
};

} // namespace cubey::render
