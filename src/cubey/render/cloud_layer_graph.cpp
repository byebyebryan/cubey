#include <cubey/render/cloud_layer.h>

#include <cubey/render/generated_texture.h>
#include <cubey/render/pass.h>
#include <cubey/render/render_graph.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/immediate_commands.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "cloud_layer_internal.h"

namespace cubey::render {

using detail::CloudLayerShadowPushConstants;
using detail::cloud_layer_composite_mode_has_background;
using detail::cloud_layer_composite_mode_has_scene_depth;

namespace {

[[nodiscard]] bool cloud_layer_surface_march_debug_view_supported(CloudLayerDebugView view) {
    switch (view) {
    case CloudLayerDebugView::Final:
    case CloudLayerDebugView::RawFinal:
    case CloudLayerDebugView::AuthoredWeather:
    case CloudLayerDebugView::Density:
    case CloudLayerDebugView::Transmittance:
    case CloudLayerDebugView::Lighting:
    case CloudLayerDebugView::Shadow:
    case CloudLayerDebugView::Steps:
    case CloudLayerDebugView::Background:
    case CloudLayerDebugView::CloudAlpha:
    case CloudLayerDebugView::Distance:
    case CloudLayerDebugView::MetadataDistance:
    case CloudLayerDebugView::MetadataAlpha:
    case CloudLayerDebugView::MetadataConfidence:
    case CloudLayerDebugView::MetadataDensity:
    case CloudLayerDebugView::BaseDensity:
    case CloudLayerDebugView::DetailDensity:
    case CloudLayerDebugView::AmbientLight:
    case CloudLayerDebugView::DirectLight:
    case CloudLayerDebugView::PhaseLight:
    case CloudLayerDebugView::CloudType:
    case CloudLayerDebugView::WeatherEdge:
    case CloudLayerDebugView::CoverageBias:
    case CloudLayerDebugView::LocalAlpha:
    case CloudLayerDebugView::JitterPattern:
    case CloudLayerDebugView::EdgeMask:
        return true;
    case CloudLayerDebugView::VisibleDensity:
    case CloudLayerDebugView::VisibleCloudType:
    case CloudLayerDebugView::DistanceRegime:
    case CloudLayerDebugView::OrbitAlpha:
    case CloudLayerDebugView::OrbitCoverage:
    case CloudLayerDebugView::OrbitDetail:
    case CloudLayerDebugView::OrbitHull:
    case CloudLayerDebugView::OrbitEnvelope:
    case CloudLayerDebugView::OrbitShellAlpha:
    case CloudLayerDebugView::OrbitShellHeight:
    case CloudLayerDebugView::OrbitShellNormal:
    case CloudLayerDebugView::OrbitShellShadow:
    case CloudLayerDebugView::LocalScatter:
    case CloudLayerDebugView::LocalClear:
    case CloudLayerDebugView::LocalStructure:
    case CloudLayerDebugView::LocalEdgeDetail:
    case CloudLayerDebugView::FarShellAlpha:
    case CloudLayerDebugView::LocalWithShellAlpha:
    case CloudLayerDebugView::TransitionWeights:
    case CloudLayerDebugView::SceneDepthOcclusion:
    case CloudLayerDebugView::OrbitShellFootprint:
    case CloudLayerDebugView::OrbitShellFilter:
    case CloudLayerDebugView::OrbitShellMass:
    case CloudLayerDebugView::HorizonStepLength:
    case CloudLayerDebugView::HorizonFilterLod:
    case CloudLayerDebugView::HorizonHandoff:
    case CloudLayerDebugView::LocalTruncation:
    case CloudLayerDebugView::IntegratedHorizonAlpha:
    case CloudLayerDebugView::IntegratedHorizonRadiance:
        return false;
    }
    return false;
}

[[nodiscard]] bool cloud_layer_use_surface_march(const CloudLayerConfig& config) {
    return config.density_model == CloudLayerDensityModel::SurfaceVolume &&
           config.distance_mode == CloudLayerDistanceMode::Local &&
           config.view_sample_mode == CloudLayerViewSampleMode::SingleFrame &&
           config.view_samples <= 1 && !config.temporal_enabled && config.local_volume_enabled &&
           !config.horizon_layer_enabled &&
           cloud_layer_surface_march_debug_view_supported(config.debug_view);
}


} // namespace

RenderGraphTextureState cloud_layer_sampled_texture_state() {
    return {
        .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .access_mask = VK_ACCESS_SHADER_READ_BIT,
        .stage_mask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    };
}

RenderGraphTextureDesc cloud_layer_color_texture_desc(std::string label, VkExtent2D extent) {
    return {
        .label = std::move(label),
        .extent = {extent.width, extent.height, 1U},
        .format = kCloudLayerColorFormat,
        .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
    };
}

RenderGraphTextureDesc cloud_layer_shadow_texture_desc() {
    return {
        .label = "cloud shadow transmittance",
        .extent = {kCloudLayerShadowTextureSize, kCloudLayerShadowTextureSize, 1U},
        .format = kCloudLayerShadowFormat,
        .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
    };
}

void CloudLayerRuntime::upload_frame_uniforms(FrameSlot frame_slot,
                                             const CloudLayerFrameUniforms& uniforms) const {
    frame_uniforms().upload(frame_slot, uniforms);
}

CloudLayerRuntimeFrame CloudLayerRuntime::declare_product(
    RenderGraphBuilder& graph, VkExtent2D target_extent, const CloudLayerConfig& config,
    FrameSlot frame_slot, CloudLayerFrameUniforms uniforms) const {
    const VkExtent2D cloud_extent = cloud_layer_product_extent(target_extent, config.quality);
    const bool temporal_pass_enabled =
        config.temporal_enabled && config.debug_view == CloudLayerDebugView::Final &&
        history_extent_matches(cloud_extent);
    const std::uint32_t history_read_index =
        frame_slot.index < history_read_indices_.size() ? history_read_indices_[frame_slot.index]
                                                        : 0U;
    const std::uint32_t history_write_index = 1U - history_read_index;
    const bool history_read_valid = history_texture_valid(frame_slot, history_read_index);
    const bool history_write_valid = history_texture_valid(frame_slot, history_write_index);
    const RenderGraphTextureState sampled_state = cloud_layer_sampled_texture_state();

    CloudLayerRuntimeFrame frame{
        .product =
            {
                .cloud = graph.create_texture(
                    cloud_layer_color_texture_desc("cloud product", cloud_extent)),
                .metadata = graph.create_texture(
                    cloud_layer_color_texture_desc("cloud metadata", cloud_extent)),
                .extent = cloud_extent,
            },
        .temporal_pass_enabled = temporal_pass_enabled,
        .surface_march_enabled = cloud_layer_use_surface_march(config),
        .history_write_index = history_write_index,
        .frame_uniforms = uniforms,
    };
    frame.product.resolved_cloud = frame.product.cloud;
    frame.product.resolved_metadata = frame.product.metadata;

    graph.add_pass("cloud march", RenderGraphQueueDomain::Compute)
        .write_storage_texture(frame.product.cloud, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .write_storage_texture(frame.product.metadata, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .execute([this, frame_slot, cloud_extent, surface_march_enabled = frame.surface_march_enabled](
                     const RenderGraphExecutionContext& context) {
            record_march_dispatch(context.recorder(), march_material().set(frame_slot),
                                  cloud_extent, surface_march_enabled);
        });

    if (temporal_pass_enabled) {
        const Texture2D& read_texture = history_cloud_texture(frame_slot, history_read_index);
        frame.history_cloud_read = graph.import_texture(
            cloud_layer_color_texture_desc("cloud history read", cloud_extent),
            read_texture.handle(), read_texture.view(),
            history_read_valid
                ? std::optional<RenderGraphTextureState>{sampled_state}
                : std::optional<RenderGraphTextureState>{render_graph_undefined_texture_state()},
            sampled_state);
        const Texture2D& metadata_read_texture =
            history_metadata_texture(frame_slot, history_read_index);
        frame.history_metadata_read = graph.import_texture(
            cloud_layer_color_texture_desc("cloud metadata history read", cloud_extent),
            metadata_read_texture.handle(), metadata_read_texture.view(),
            history_read_valid
                ? std::optional<RenderGraphTextureState>{sampled_state}
                : std::optional<RenderGraphTextureState>{render_graph_undefined_texture_state()},
            sampled_state);
        const Texture2D& write_texture = history_cloud_texture(frame_slot, history_write_index);
        frame.history_cloud_write = graph.import_texture(
            cloud_layer_color_texture_desc("cloud history write", cloud_extent),
            write_texture.handle(), write_texture.view(),
            history_write_valid
                ? std::optional<RenderGraphTextureState>{sampled_state}
                : std::optional<RenderGraphTextureState>{render_graph_undefined_texture_state()},
            sampled_state);
        const Texture2D& metadata_write_texture =
            history_metadata_texture(frame_slot, history_write_index);
        frame.history_metadata_write = graph.import_texture(
            cloud_layer_color_texture_desc("cloud metadata history write", cloud_extent),
            metadata_write_texture.handle(), metadata_write_texture.view(),
            history_write_valid
                ? std::optional<RenderGraphTextureState>{sampled_state}
                : std::optional<RenderGraphTextureState>{render_graph_undefined_texture_state()},
            sampled_state);
        graph.add_pass("cloud temporal", RenderGraphQueueDomain::Compute)
            .read_texture(frame.product.cloud, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
            .read_texture(frame.product.metadata, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
            .read_texture(frame.history_cloud_read, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
            .read_texture(frame.history_metadata_read, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
            .write_storage_texture(frame.history_cloud_write, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
            .write_storage_texture(frame.history_metadata_write,
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
            .execute([this, frame_slot, cloud_extent, uniforms, history_read_index](
                         const RenderGraphExecutionContext& context) {
                temporal_uniforms().upload(frame_slot, temporal_frame_uniforms(
                                                           frame_slot, uniforms,
                                                           history_read_index));
                record_temporal_dispatch(context.recorder(), temporal_material().set(frame_slot),
                                         cloud_extent);
            });
        frame.product.resolved_cloud = frame.history_cloud_write;
        frame.product.resolved_metadata = frame.history_metadata_write;
    }

    return frame;
}

CloudLayerShadowProduct CloudLayerRuntime::declare_shadow_product(
    RenderGraphBuilder& graph, FrameSlot frame_slot, const CloudLayerShadowRequest& request) const {
    if (!std::isfinite(request.direct_light_intensity) || request.direct_light_intensity < 0.0F) {
        throw std::runtime_error("cloud shadow direct light intensity must be finite and non-negative");
    }
    const float light_direction_length = glm::length(request.direct_light_direction);
    if (request.direct_light_intensity > 0.0F &&
        (!std::isfinite(light_direction_length) || light_direction_length <= 0.0001F)) {
        throw std::runtime_error("lit cloud shadows require a finite direct light direction");
    }

    const CloudLayerShadowProjection projection = cloud_layer_shadow_projection(request);
    CloudLayerShadowProduct product{
        .transmittance = graph.create_texture(cloud_layer_shadow_texture_desc()),
        .world_to_uv_x = projection.world_to_uv_x,
        .world_to_uv_y = projection.world_to_uv_y,
        .extent = projection.extent,
        .texel_world_size_m = projection.texel_world_size_m,
    };
    graph.add_pass("cloud shadow", RenderGraphQueueDomain::Compute)
        .write_storage_texture(product.transmittance, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .execute([this, frame_slot, request, projection](
                     const RenderGraphExecutionContext& context) {
            record_shadow_dispatch(context.recorder(), shadow_material().set(frame_slot), request,
                                   projection);
        });
    return product;
}

void CloudLayerRuntime::declare_composite(
    RenderGraphBuilder& graph, RenderGraphTextureHandle target, const CloudLayerRuntimeFrame& frame,
    FrameSlot frame_slot, std::optional<RenderGraphTextureHandle> background,
    std::optional<RenderGraphTextureHandle> scene_depth) const {
    const bool expects_background = cloud_layer_composite_mode_has_background(composite_mode_);
    const bool expects_scene_depth = cloud_layer_composite_mode_has_scene_depth(composite_mode_);
    if (expects_background != background.has_value()) {
        throw std::runtime_error("cloud layer composite background mode mismatch");
    }
    if (expects_scene_depth != scene_depth.has_value()) {
        throw std::runtime_error("cloud layer composite scene depth mode mismatch");
    }
    auto pass = graph.add_pass("cloud composite", RenderGraphQueueDomain::Graphics)
                    .read_texture(frame.product.resolved_cloud)
                    .read_texture(frame.product.resolved_metadata);
    if (background.has_value()) {
        pass.read_texture(background.value());
    }
    if (scene_depth.has_value()) {
        pass.read_texture(scene_depth.value(), VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }
    pass.write_color(target)
        .material_pass(cloud_layer_composite_pass_info(background.has_value(),
                                                       scene_depth.has_value()))
        .execute([this, target, frame_slot](const RenderGraphExecutionContext& context) {
            const ColorTargetView color_target = resolved_color_target_view(context, target);
            record_composite_draw(context.recorder(), color_target, frame_slot);
        });
}

void CloudLayerRuntime::update_product_descriptors(
    const cubey::vulkan::Device& device, FrameSlot frame_slot, const CompiledRenderGraph& graph,
    const RenderGraphResourceSet& resources, const CloudLayerRuntimeFrame& frame) const {
    const RenderGraphSampledTextureView cloud_product =
        resolved_sampled_texture_view(graph, resources, frame.product.cloud);
    const RenderGraphSampledTextureView cloud_metadata =
        resolved_sampled_texture_view(graph, resources, frame.product.metadata);
    const CloudLayerGeneratedResources& generated = generated_resources();

    MaterialDescriptorWriter(march_material().set(frame_slot))
        .uniform_buffer(kCloudLayerUniformBinding, frame_uniforms().buffer(frame_slot).handle(),
                        frame_uniforms().range())
        .storage_image(kCloudLayerOutputBinding, cloud_product.view, VK_IMAGE_LAYOUT_GENERAL)
        .combined_image_sampler(kCloudLayerBaseNoiseBinding, generated.base_noise.sampler().handle(),
                                generated.base_noise.view())
        .combined_image_sampler(kCloudLayerDetailNoiseBinding,
                                generated.detail_noise.sampler().handle(),
                                generated.detail_noise.view())
        .combined_image_sampler(kCloudLayerWeatherBinding, generated.weather.sampler().handle(),
                                generated.weather.view())
        .storage_image(kCloudLayerMetadataBinding, cloud_metadata.view, VK_IMAGE_LAYOUT_GENERAL)
        .combined_image_sampler(kCloudLayerBlueNoiseBinding,
                                generated.blue_noise.sampler().handle(),
                                generated.blue_noise.view())
        .update(device);

    if (frame.temporal_pass_enabled) {
        const RenderGraphSampledTextureView history_cloud_read =
            resolved_sampled_texture_view(graph, resources, frame.history_cloud_read);
        const RenderGraphSampledTextureView history_metadata_read =
            resolved_sampled_texture_view(graph, resources, frame.history_metadata_read);
        const RenderGraphSampledTextureView history_cloud_write =
            resolved_sampled_texture_view(graph, resources, frame.history_cloud_write);
        const RenderGraphSampledTextureView history_metadata_write =
            resolved_sampled_texture_view(graph, resources, frame.history_metadata_write);
        MaterialDescriptorWriter(temporal_material().set(frame_slot))
            .combined_image_sampler(kCloudLayerTemporalCurrentCloudBinding,
                                    composite_sampler().handle(), cloud_product.view,
                                    cloud_product.layout)
            .combined_image_sampler(kCloudLayerTemporalCurrentMetadataBinding,
                                    composite_sampler().handle(), cloud_metadata.view,
                                    cloud_metadata.layout)
            .combined_image_sampler(kCloudLayerTemporalHistoryCloudBinding,
                                    composite_sampler().handle(), history_cloud_read.view,
                                    history_cloud_read.layout)
            .combined_image_sampler(kCloudLayerTemporalHistoryMetadataBinding,
                                    composite_sampler().handle(), history_metadata_read.view,
                                    history_metadata_read.layout)
            .uniform_buffer(kCloudLayerTemporalUniformBinding,
                            temporal_uniforms().buffer(frame_slot).handle(),
                            temporal_uniforms().range())
            .storage_image(kCloudLayerTemporalOutputBinding, history_cloud_write.view,
                           VK_IMAGE_LAYOUT_GENERAL)
            .storage_image(kCloudLayerTemporalOutputMetadataBinding, history_metadata_write.view,
                           VK_IMAGE_LAYOUT_GENERAL)
            .update(device);
    }
}

void CloudLayerRuntime::update_composite_descriptors(
    const cubey::vulkan::Device& device, FrameSlot frame_slot, const CompiledRenderGraph& graph,
    const RenderGraphResourceSet& resources, const CloudLayerRuntimeFrame& frame,
    std::optional<RenderGraphTextureHandle> background,
    std::optional<RenderGraphTextureHandle> scene_depth) const {
    const bool expects_background = cloud_layer_composite_mode_has_background(composite_mode_);
    const bool expects_scene_depth = cloud_layer_composite_mode_has_scene_depth(composite_mode_);
    if (expects_background != background.has_value()) {
        throw std::runtime_error("cloud layer descriptor background mode mismatch");
    }
    if (expects_scene_depth != scene_depth.has_value()) {
        throw std::runtime_error("cloud layer descriptor scene depth mode mismatch");
    }
    const RenderGraphSampledTextureView resolved_cloud_product =
        resolved_sampled_texture_view(graph, resources, frame.product.resolved_cloud);
    const RenderGraphSampledTextureView resolved_cloud_metadata =
        resolved_sampled_texture_view(graph, resources, frame.product.resolved_metadata);
    MaterialDescriptorWriter writer(composite_material().set(frame_slot));
    writer.uniform_buffer(kCloudLayerUniformBinding, frame_uniforms().buffer(frame_slot).handle(),
                          frame_uniforms().range())
        .combined_image_sampler(kCloudLayerCompositeCloudBinding, composite_sampler().handle(),
                                resolved_cloud_product.view, resolved_cloud_product.layout)
        .combined_image_sampler(kCloudLayerCompositeMetadataBinding, composite_sampler().handle(),
                                resolved_cloud_metadata.view, resolved_cloud_metadata.layout);
    if (background.has_value()) {
        const RenderGraphSampledTextureView background_view =
            resolved_sampled_texture_view(graph, resources, background.value());
        writer.combined_image_sampler(kCloudLayerCompositeBackgroundBinding,
                                      composite_sampler().handle(), background_view.view,
                                      background_view.layout);
    }
    if (scene_depth.has_value()) {
        const RenderGraphSampledTextureView scene_depth_view =
            resolved_sampled_texture_view(graph, resources, scene_depth.value());
        writer.combined_image_sampler(kCloudLayerCompositeSceneDepthBinding,
                                      composite_sampler().handle(), scene_depth_view.view,
                                      scene_depth_view.layout);
    }
    writer.update(device);
}

void CloudLayerRuntime::update_descriptors(
    const cubey::vulkan::Device& device, FrameSlot frame_slot, const CompiledRenderGraph& graph,
    const RenderGraphResourceSet& resources, const CloudLayerRuntimeFrame& frame,
    std::optional<RenderGraphTextureHandle> background,
    std::optional<RenderGraphTextureHandle> scene_depth) const {
    update_product_descriptors(device, frame_slot, graph, resources, frame);
    update_composite_descriptors(device, frame_slot, graph, resources, frame, background,
                                 scene_depth);
}

void CloudLayerRuntime::update_shadow_descriptors(
    const cubey::vulkan::Device& device, FrameSlot frame_slot, const CompiledRenderGraph& graph,
    const RenderGraphResourceSet& resources, const CloudLayerShadowProduct& product) const {
    const RenderGraphSampledTextureView transmittance =
        resolved_sampled_texture_view(graph, resources, product.transmittance);
    const CloudLayerGeneratedResources& generated = generated_resources();
    MaterialDescriptorWriter(shadow_material().set(frame_slot))
        .uniform_buffer(kCloudLayerShadowUniformBinding, frame_uniforms().buffer(frame_slot).handle(),
                        frame_uniforms().range())
        .storage_image(kCloudLayerShadowOutputBinding, transmittance.view, VK_IMAGE_LAYOUT_GENERAL)
        .combined_image_sampler(kCloudLayerShadowBaseNoiseBinding,
                                generated.base_noise.sampler().handle(),
                                generated.base_noise.view())
        .combined_image_sampler(kCloudLayerShadowDetailNoiseBinding,
                                generated.detail_noise.sampler().handle(),
                                generated.detail_noise.view())
        .combined_image_sampler(kCloudLayerShadowWeatherBinding,
                                generated.weather.sampler().handle(), generated.weather.view())
        .update(device);
}

void CloudLayerRuntime::record_march_dispatch(const cubey::vulkan::CommandRecorder& recorder,
                                              VkDescriptorSet descriptor_set,
                                             VkExtent2D extent,
                                             bool surface_march_enabled) const {
    const ComputePipelineResource& pipeline =
        surface_march_enabled ? surface_march_pipeline() : general_march_pipeline();
    record_compute_pipeline_dispatch(
        recorder,
        compute_pipeline_dispatch_info(
            pipeline, descriptor_set,
            ceil_dispatch_groups(extent.width, extent.height, kCloudLayerComputeGroupSize)));
}

void CloudLayerRuntime::record_shadow_dispatch(
    const cubey::vulkan::CommandRecorder& recorder, VkDescriptorSet descriptor_set,
    const CloudLayerShadowRequest& request, const CloudLayerShadowProjection& projection) const {
    const CloudLayerShadowPushConstants push_constants{
        .receiver_center_half_extent =
            {
                projection.receiver_center.x,
                projection.receiver_center.y,
                projection.receiver_center.z,
                request.half_extent_m,
            },
        .receiver_axis_u_texel_size =
            {
                projection.receiver_axis_u.x,
                projection.receiver_axis_u.y,
                projection.receiver_axis_u.z,
                projection.texel_world_size_m,
            },
        .receiver_axis_v_steps =
            {
                projection.receiver_axis_v.x,
                projection.receiver_axis_v.y,
                projection.receiver_axis_v.z,
                static_cast<float>(kCloudLayerShadowStepCount),
            },
        .light_direction_intensity =
            {
                request.direct_light_direction.x,
                request.direct_light_direction.y,
                request.direct_light_direction.z,
                request.direct_light_intensity,
            },
    };
    record_compute_pipeline_dispatch(
        recorder,
        compute_pipeline_dispatch_info(
            shadow_pipeline(), descriptor_set,
            ceil_dispatch_groups(projection.extent.width, projection.extent.height,
                                 kCloudLayerComputeGroupSize)),
        VK_SHADER_STAGE_COMPUTE_BIT, push_constants);
}

void CloudLayerRuntime::record_temporal_dispatch(const cubey::vulkan::CommandRecorder& recorder,
                                                VkDescriptorSet descriptor_set,
                                                VkExtent2D extent) const {
    record_compute_pipeline_dispatch(
        recorder,
        compute_pipeline_dispatch_info(
            temporal_pipeline(), descriptor_set,
            ceil_dispatch_groups(extent.width, extent.height, kCloudLayerComputeGroupSize)));
}

void CloudLayerRuntime::record_composite_draw(const cubey::vulkan::CommandRecorder& recorder,
                                             const ColorTargetView& target,
                                             FrameSlot frame_slot) const {
    record_render_target_pass(
        recorder, render_target_view(target),
        RenderClearValues{
            .color = color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
        },
        [this, frame_slot](const cubey::vulkan::CommandRecorder& pass_recorder) {
            record_fullscreen_pipeline_draw(
                pass_recorder,
                {.pipeline = &composite_pipeline(),
                 .descriptor_set = composite_material().set(frame_slot)});
        });
}


} // namespace cubey::render
