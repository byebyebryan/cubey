#include <cubey/render/hdr_post_frame.h>

#include <cubey/render/pass.h>

#include <array>
#include <stdexcept>
#include <utility>

namespace cubey::render {

RenderGraphTextureDesc hdr_scene_color_texture_desc(std::string label, VkExtent2D extent,
                                                    VkFormat format) {
    return {
        .label = std::move(label),
        .extent = {extent.width, extent.height, 1U},
        .format = format,
        .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
    };
}

PbrPostUniforms hdr_post_uniforms(VkFormat color_format, float exposure, PbrTonemap tonemap) {
    const PbrDisplayTransform display_transform =
        pbr_display_transform_for_target(color_format, exposure, tonemap);
    return {
        .display_transform = pbr_display_transform_uniform(display_transform),
    };
}

void HdrPostFrame::create_materials(const cubey::vulkan::Device& device,
                                    const HdrPostFrameMaterialConfig& config) {
    material_.emplace(device, FrameUniformMaterialInstanceConfig{
                                  .material_pass = pbr_post_pass_info(),
                                  .descriptor_set = 0,
                                  .frame_slot_count = config.frame_slot_count,
                                  .uniform_binding =
                                      static_cast<std::uint32_t>(PbrPostBinding::PostUniforms),
                              });
    sampler_.emplace(device, cubey::vulkan::SamplerConfig{
                                 .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                             });
}

void HdrPostFrame::create_pipeline(const cubey::vulkan::Device& device,
                                   const HdrPostFramePipelineConfig& config) {
    const std::array descriptor_set_layouts{material().layout()};
    pipeline_.emplace(device, GraphicsPipelineFileResourceConfig{
                                  .extent = config.extent,
                                  .color_format = config.color_format,
                                  .shader_stage_files = config.shader_stage_files,
                                  .descriptor_set_layouts = descriptor_set_layouts,
                                  .material_pass = pbr_post_pass_info(),
                              });
}

void HdrPostFrame::destroy_pipeline() {
    pipeline_.reset();
}

void HdrPostFrame::destroy() {
    destroy_pipeline();
    sampler_.reset();
    material_.reset();
}

void HdrPostFrame::upload(FrameSlot frame_slot, const PbrPostUniforms& uniforms) const {
    material().upload(frame_slot, uniforms);
}

void HdrPostFrame::update_scene_color_descriptor(
    const cubey::vulkan::Device& device, FrameSlot frame_slot, const CompiledRenderGraph& graph,
    const RenderGraphResourceSet& resources, RenderGraphTextureHandle scene_color) const {
    const RenderGraphSampledTextureView sampled =
        resolved_sampled_texture_view(graph, resources, scene_color);
    MaterialDescriptorWriter(material().set(frame_slot))
        .combined_image_sampler(static_cast<std::uint32_t>(PbrPostBinding::SceneColor),
                                sampler().handle(), sampled.view, sampled.layout)
        .update(device);
}

void HdrPostFrame::record_pass(const cubey::vulkan::CommandRecorder& recorder,
                               ColorTargetView target, FrameSlot frame_slot) const {
    record_render_target_pass(
        recorder, render_target_view(target),
        RenderClearValues{
            .color = color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
        },
        [this, frame_slot](const cubey::vulkan::CommandRecorder& pass_recorder) {
            record_fullscreen_pipeline_draw(pass_recorder, {
                                                               .pipeline = &pipeline(),
                                                               .descriptor_set =
                                                                   material().set(frame_slot),
                                                           });
        });
}

const FrameUniformMaterialInstance<PbrPostUniforms>& HdrPostFrame::material() const {
    if (!material_.has_value()) {
        throw std::runtime_error("HDR post material is not initialized");
    }
    return material_.value();
}

const GraphicsPipelineResource& HdrPostFrame::pipeline() const {
    if (!pipeline_.has_value()) {
        throw std::runtime_error("HDR post pipeline is not initialized");
    }
    return pipeline_.value();
}

const cubey::vulkan::Sampler& HdrPostFrame::sampler() const {
    if (!sampler_.has_value()) {
        throw std::runtime_error("HDR post sampler is not initialized");
    }
    return sampler_.value();
}

} // namespace cubey::render
