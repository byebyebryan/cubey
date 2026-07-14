#include <cubey/engine/cloud_environment_runtime.h>

namespace cubey {

CloudEnvironmentRuntimeFrame
cloud_environment_runtime_frame(const CloudEnvironmentConfig& config, double elapsed_seconds,
                                const render::AtmosphereEnvironmentLighting& lighting,
                                const CloudEnvironmentSurfaceViewInfo& view,
                                std::uint32_t temporal_frame_index) {
    render::CloudLayerConfig layer = config.layer;
    layer.wind_offset_m = static_cast<float>(elapsed_seconds) * config.wind_speed_mps;
    const render::CloudLayerFrameInfo frame_view{
        .camera_position = view.camera_position,
        .camera_right = view.camera_right,
        .camera_up = view.camera_up,
        .camera_forward = view.camera_forward,
        .tan_half_fovy = view.tan_half_fovy,
        .sun_direction = lighting.sun_direction,
        .sun_color = lighting.sun_color,
        .sun_intensity = lighting.sun_intensity,
        .moon_direction = lighting.moon_direction,
        .moon_color = lighting.moon_color,
        .moon_intensity = lighting.moon_intensity,
        .ambient_color = lighting.ambient_color,
        .ambient_intensity = lighting.ambient_intensity,
        .target_extent = view.target_extent,
        .temporal_frame_index = temporal_frame_index,
        .camera_mode = 0.0F,
        .external_background = view.external_background,
        .near_plane_m = view.near_plane_m,
        .far_plane_m = view.far_plane_m,
        .scene_depth_occlusion_enabled = view.scene_depth_occlusion_enabled,
        .scene_depth_fade_m = view.scene_depth_fade_m,
    };
    return {
        .layer = layer,
        .view = frame_view,
        .uniforms = render::cloud_layer_frame_uniforms(layer, frame_view),
        .enabled = config.enabled,
    };
}

void CloudEnvironmentRuntime::create_surface_resources(
    const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
    const render::CloudLayerGeneratedShaderFiles& shaders, const CloudEnvironmentConfig& config) {
    config_ = config;
    elapsed_seconds_ = 0.0;
    surface_.create_generated_resources(device, gpu, shaders, config_.layer);
}

void CloudEnvironmentRuntime::create_surface_target_resources(
    const cubey::vulkan::Device& device, const render::CloudLayerRuntimeShaderFiles& shaders,
    render::CloudLayerCompositeMode composite_mode, VkFormat color_format, VkExtent2D extent,
    std::uint32_t frame_slot_count) {
    surface_.create_swapchain_resources(device, shaders, composite_mode, color_format, extent,
                                        frame_slot_count, config_.layer);
}

void CloudEnvironmentRuntime::destroy_surface_target_resources() {
    surface_.destroy_swapchain_resources();
}

void CloudEnvironmentRuntime::destroy_surface_resources() {
    surface_.destroy_generated_resources();
    elapsed_seconds_ = 0.0;
}

void CloudEnvironmentRuntime::set_config(const CloudEnvironmentConfig& config) {
    config_ = config;
}

void CloudEnvironmentRuntime::set_elapsed_seconds(double elapsed_seconds) {
    elapsed_seconds_ = elapsed_seconds;
}

const CloudEnvironmentConfig& CloudEnvironmentRuntime::config() const noexcept {
    return config_;
}

double CloudEnvironmentRuntime::elapsed_seconds() const noexcept {
    return elapsed_seconds_;
}

void CloudEnvironmentRuntime::update_weather_texture(const cubey::vulkan::Device& device,
                                                     cubey::vulkan::GpuRuntime& gpu,
                                                     const render::ShaderStageFile& shader,
                                                     bool force) {
    surface_.update_weather_texture(device, gpu, shader, config_.layer, force);
}

CloudEnvironmentRuntimeFrame
CloudEnvironmentRuntime::frame(const CloudEnvironmentSurfaceViewInfo& view,
                               const render::AtmosphereEnvironmentLighting& lighting) const {
    return cloud_environment_runtime_frame(config_, elapsed_seconds_, lighting, view,
                                           surface_.temporal_frame_index());
}

render::CloudLayerRuntimeFrame
CloudEnvironmentRuntime::declare_surface_product(render::RenderGraphBuilder& graph,
                                                 render::FrameSlot frame_slot,
                                                 const CloudEnvironmentRuntimeFrame& frame) const {
    return surface_.declare_product(graph, frame.view.target_extent, frame.layer, frame_slot,
                                    frame.uniforms);
}

void CloudEnvironmentRuntime::declare_surface_composite(
    render::RenderGraphBuilder& graph, render::RenderGraphTextureHandle target,
    const render::CloudLayerRuntimeFrame& product, render::FrameSlot frame_slot,
    std::optional<render::RenderGraphTextureHandle> background,
    std::optional<render::RenderGraphTextureHandle> scene_depth) const {
    surface_.declare_composite(graph, target, product, frame_slot, background, scene_depth);
}

render::CloudLayerShadowProduct CloudEnvironmentRuntime::declare_shadow_product(
    render::RenderGraphBuilder& graph, render::FrameSlot frame_slot,
    const render::CloudLayerShadowRequest& request) const {
    return surface_.declare_shadow_product(graph, frame_slot, request);
}

void CloudEnvironmentRuntime::update_surface_descriptors(
    const cubey::vulkan::Device& device, render::FrameSlot frame_slot,
    const render::CompiledRenderGraph& graph, const render::RenderGraphResourceSet& resources,
    const render::CloudLayerRuntimeFrame& frame,
    std::optional<render::RenderGraphTextureHandle> background,
    std::optional<render::RenderGraphTextureHandle> scene_depth) const {
    surface_.update_descriptors(device, frame_slot, graph, resources, frame, background,
                                scene_depth);
}

void CloudEnvironmentRuntime::update_shadow_descriptors(
    const cubey::vulkan::Device& device, render::FrameSlot frame_slot,
    const render::CompiledRenderGraph& graph, const render::RenderGraphResourceSet& resources,
    const render::CloudLayerShadowProduct& product) const {
    surface_.update_shadow_descriptors(device, frame_slot, graph, resources, product);
}

void CloudEnvironmentRuntime::complete_surface_frame(render::FrameSlot frame_slot,
                                                     const render::CloudLayerRuntimeFrame& frame) {
    surface_.complete_frame(frame_slot, frame);
}

bool CloudEnvironmentRuntime::surface_resources_created() const noexcept {
    return surface_.generated_resources_created();
}

const render::CloudLayerGeneratedResources& CloudEnvironmentRuntime::generated_resources() const {
    return surface_.generated_resources();
}

void CloudEnvironmentRuntime::create_resources(
    const cubey::vulkan::Device& device, const render::CloudEnvironmentProbeConfig& config,
    const render::CloudLayerGeneratedResources& generated, const render::TextureCube& clear_sky) {
    probe_.create_resources(device, config, generated, clear_sky);
}

void CloudEnvironmentRuntime::create_pipelines(
    const cubey::vulkan::Device& device,
    const render::CloudEnvironmentProbePipelineConfig& config) {
    probe_.create_pipelines(device, config);
}

void CloudEnvironmentRuntime::destroy() {
    probe_.destroy();
    destroy_surface_target_resources();
    destroy_surface_resources();
}

void CloudEnvironmentRuntime::advance(double delta_seconds) {
    elapsed_seconds_ += delta_seconds;
    probe_.advance(delta_seconds);
}

void CloudEnvironmentRuntime::invalidate() {
    probe_.invalidate();
}

bool CloudEnvironmentRuntime::record_pending_update(
    const cubey::vulkan::CommandRecorder& recorder,
    const render::CloudEnvironmentProbeUpdateInfo& info) {
    return probe_.record_pending_update(recorder, info);
}

bool CloudEnvironmentRuntime::record_pending_update(const cubey::vulkan::CommandRecorder& recorder,
                                                    render::FrameSlot frame_slot,
                                                    const CloudEnvironmentRuntimeFrame& frame) {
    if (!frame.enabled) {
        return false;
    }
    return record_pending_update(recorder, render::CloudEnvironmentProbeUpdateInfo{
                                               .frame_slot = frame_slot,
                                               .cloud = frame.layer,
                                               .frame = frame.view,
                                           });
}

bool CloudEnvironmentRuntime::resources_created() const noexcept {
    return probe_.resources_created();
}

bool CloudEnvironmentRuntime::pipelines_created() const noexcept {
    return probe_.pipelines_created();
}

float CloudEnvironmentRuntime::age_seconds() const noexcept {
    return probe_.age_seconds();
}

render::CloudEnvironmentProbeSnapshot CloudEnvironmentRuntime::snapshot() const {
    return probe_.snapshot();
}

render::PbrEnvironmentTextureBindings CloudEnvironmentRuntime::pbr_environment_bindings(
    const render::PbrEnvironmentTextureBindings& fallback) const {
    render::validate_pbr_environment_texture_bindings(fallback);
    render::PbrEnvironmentTextureBindings bindings = fallback;
    const render::CloudEnvironmentProbeSnapshot environment = snapshot();
    if (!environment.valid) {
        return bindings;
    }

    bindings.prefiltered_sampler = environment.current->sampler().handle();
    bindings.prefiltered_view = environment.current->view();
    bindings.prefiltered_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bindings.previous_prefiltered_sampler = environment.previous->sampler().handle();
    bindings.previous_prefiltered_view = environment.previous->view();
    bindings.previous_prefiltered_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bindings.prefiltered_mip_levels = environment.current->mip_levels();
    bindings.prefiltered_blend = environment.blend;
    return bindings;
}

} // namespace cubey
