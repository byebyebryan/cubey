#include "ocean_app_internal.h"

namespace cubey::projects::ocean::detail {

void OceanApp::create_pipeline(cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
                               VkFormat color_format, VkExtent2D extent,
                               std::uint32_t frame_slot_count) {
    create_atmosphere_environment_runtime(device, gpu, frame_slot_count);
    ocean_gpu_.create(device, OceanGpuResourceConfig{
                                  .ocean = ocean_config_,
                                  .shader_dir = CUBEY_OCEAN_SHADER_DIR,
                                  .color_format = kOceanSceneColorFormat,
                                  .depth_format = kOceanDepthFormat,
                                  .target_extent = extent,
                                  .frame_slot_count = frame_slot_count,
                              });
    cloud_shadow_fallback_texture_.emplace(
        create_ocean_cloud_shadow_fallback_texture(device, gpu));
    cloud_reflection_fallback_texture_.emplace(
        create_ocean_cloud_reflection_fallback_texture(device, gpu));
    for (std::uint32_t index = 0; index < frame_slot_count; ++index) {
        const cubey::render::FrameSlot frame_slot{.index = index, .count = frame_slot_count};
        update_cloud_shadow_fallback_descriptor(device, frame_slot);
        update_cloud_planar_reflection_descriptor(device, frame_slot);
    }
    create_terrain_ocean_field_resources(device, gpu, frame_slot_count);
    ocean_gpu_.update_terrain_ocean_field_descriptor(device,
                                                     terrain_ocean_fields_texture_.value());
    update_terrain_ocean_field_uniform_descriptors(device);
    create_atmosphere_background_frame(device, extent, frame_slot_count);
    if (clouds_config_.enabled) {
        create_cloud_resources(device, gpu);
        create_cloud_pipelines(device, extent, frame_slot_count);
        create_cloud_environment_runtime(device, frame_slot_count);
        create_cloud_planar_reflection(device, extent, frame_slot_count);
    }
    create_reference_pillar_resources(device, gpu, extent);
    hdr_post_frame_.create_materials(device, {
                                                 .frame_slot_count = frame_slot_count,
                                             });
    const std::array<cubey::render::ShaderStageFile, 2> post_shader_stage_files{
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .path = std::filesystem::path(CUBEY_OCEAN_SHADER_DIR) / "forward_pbr_post.vert.spv",
        },
        cubey::render::ShaderStageFile{
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .path = std::filesystem::path(CUBEY_OCEAN_SHADER_DIR) / "forward_pbr_post.frag.spv",
        },
    };
    hdr_post_frame_.create_pipeline(device, {
                                                .extent = extent,
                                                .color_format = color_format,
                                                .shader_stage_files = post_shader_stage_files,
                                            });
    graph_executor_.clear();
    graph_executor_.resize(frame_slot_count);
    pipeline_color_format_ = color_format;
    textures_initialized_ = false;
    spectrum_initialized_ = false;
    foam_initialized_ = false;
    ocean_compute_frame_index_ = 0;
    gpu_config_ = ocean_config_;
}

void OceanApp::create_terrain_ocean_field_resources(cubey::vulkan::Device& device,
                                                    cubey::vulkan::GpuRuntime& gpu,
                                                    std::uint32_t frame_slot_count) {
    terrain_ocean_fields_ = make_ocean_diagnostic_terrain_fields(
        ocean_config_, ocean_surface_frame().local_frame.water_datum_m);
    terrain_ocean_fields_texture_.emplace(
        cubey::render::create_uploaded_terrain_ocean_field_texture(device, gpu,
                                                                   terrain_ocean_fields_));
    terrain_ocean_field_uniforms_.emplace(device, frame_slot_count);
}

void OceanApp::upload_terrain_ocean_field_uniform(cubey::render::FrameSlot frame_slot) const {
    if (!terrain_ocean_field_uniforms_.has_value()) {
        throw std::runtime_error("ocean terrain field uniforms are not initialized");
    }
    terrain_ocean_field_uniforms_->upload(
        frame_slot, ocean_terrain_field_uniforms(terrain_ocean_fields_,
                                                 ocean_config_.terrain_fields_enabled));
}

void OceanApp::update_terrain_ocean_field_uniform_descriptors(const cubey::vulkan::Device& device) {
    if (!terrain_ocean_field_uniforms_.has_value()) {
        throw std::runtime_error("ocean terrain field uniforms are not initialized");
    }
    const std::uint32_t slot_count = terrain_ocean_field_uniforms_->slot_count();
    for (std::uint32_t index = 0; index < slot_count; ++index) {
        const cubey::render::FrameSlot frame_slot{.index = index, .count = slot_count};
        upload_terrain_ocean_field_uniform(frame_slot);
        ocean_gpu_.update_terrain_ocean_field_uniform_descriptor(
            device, frame_slot, terrain_ocean_field_uniforms_->buffer(frame_slot).handle(),
            terrain_ocean_field_uniforms_->range());
    }
}

void OceanApp::create_atmosphere_environment_runtime(cubey::vulkan::Device& device,
                                                     cubey::vulkan::GpuRuntime& gpu,
                                                     std::uint32_t frame_slot_count) {
    if (!atmosphere_background_atlases_.has_value()) {
        atmosphere_background_atlases_.emplace(
            cubey::render::create_atmosphere_background_cached_textures(
                device, gpu,
                {
                    .night_sky_extent = 128,
                }));
    }
    if (!atmosphere_runtime_.resources_created()) {
        atmosphere_runtime_.create_resources(
            device, cubey::AtmosphereEnvironmentRuntimeResourceConfig{
                        .reflection_extent = 64,
                        .reflection_mip_levels = 5,
                        .reflection_update_hz = ocean_config_.cloud_environment_update_hz,
                        .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                        .frame_slot_count = frame_slot_count,
                        .atmosphere_textures = atmosphere_background_atlases_->bindings(),
                    });
        const std::filesystem::path shader_dir = CUBEY_OCEAN_SHADER_DIR;
        atmosphere_runtime_.create_pipelines(
            device,
            cubey::AtmosphereEnvironmentRuntimePipelineConfig{
                .atmosphere_vertex_shader = shader_dir / "atmosphere.vert.spv",
                .atmosphere_fragment_shader = shader_dir / "atmosphere.frag.spv",
                .reflection_prefilter_vertex_shader = shader_dir / "atmosphere.vert.spv",
                .reflection_prefilter_fragment_shader =
                    shader_dir / "atmosphere_reflection_prefilter.frag.spv",
            });
        atmosphere_runtime_.force_reflection_refresh();
    }
    atmosphere_runtime_.set_environment(atmosphere_state_.environment);
}

void OceanApp::create_atmosphere_background_frame(const cubey::vulkan::Device& device,
                                                  VkExtent2D extent,
                                                  std::uint32_t frame_slot_count) {
    if (!atmosphere_background_atlases_.has_value()) {
        throw std::runtime_error("ocean atmosphere background textures are not initialized");
    }
    atmosphere_background_.destroy();
    atmosphere_background_.create_materials(
        device, cubey::render::AtmosphereBackgroundFrameMaterialConfig{
                    .frame_slot_count = frame_slot_count,
                    .textures = atmosphere_background_atlases_->bindings(),
                });
    const std::filesystem::path shader_dir = CUBEY_OCEAN_SHADER_DIR;
    const std::array<cubey::render::ShaderStageFile, 2> shaders{
        cubey::render::vertex_shader_file(shader_dir / "atmosphere.vert.spv"),
        cubey::render::fragment_shader_file(shader_dir / "atmosphere.frag.spv"),
    };
    atmosphere_background_.create_pipeline(
        device, cubey::render::AtmosphereBackgroundFramePipelineConfig{
                    .extent = extent,
                    .color_format = kOceanSceneColorFormat,
                    .depth_format = kOceanDepthFormat,
                    .shader_stage_files = shaders,
                });
}

[[nodiscard]] cubey::CloudEnvironmentConfig
OceanApp::ocean_cloud_config(const OceanSurfaceFrame& surface_frame) const {
    cubey::CloudEnvironmentConfig config = clouds_config_;
    config.layer.planet_radius_m = surface_frame.local_frame.planet_radius_m;
    config.layer.background_mode = cubey::render::CloudLayerBackgroundMode::Atmosphere;
    config.layer.density_model = cubey::render::CloudLayerDensityModel::SurfaceVolume;
    config.layer.orbit_representation =
        cubey::render::CloudLayerOrbitRepresentation::SurfaceShell;
    return config;
}

[[nodiscard]] bool OceanApp::cloud_products_enabled() const noexcept {
    return clouds_config_.enabled && atmosphere_runtime_.clouds().surface_resources_created();
}

[[nodiscard]] bool OceanApp::cloud_composite_enabled() const noexcept {
    return render_view_ == OceanRenderView::Final && cloud_products_enabled();
}

void OceanApp::create_cloud_resources(const cubey::vulkan::Device& device,
                                      cubey::vulkan::GpuRuntime& gpu) {
    cubey::CloudEnvironmentRuntime& clouds = atmosphere_runtime_.clouds();
    if (clouds.surface_resources_created()) {
        return;
    }
    clouds.create_surface_resources(device, gpu, cloud_runtime_shader_files().generated,
                                    ocean_cloud_config(ocean_surface_frame()));
}

void OceanApp::create_cloud_pipelines(const cubey::vulkan::Device& device, VkExtent2D extent,
                                      std::uint32_t frame_slot_count) {
    if (!atmosphere_runtime_.clouds().surface_resources_created()) {
        return;
    }
    atmosphere_runtime_.clouds().create_surface_target_resources(
        device, cloud_runtime_shader_files(),
        cubey::render::CloudLayerCompositeMode::ExternalBackgroundSceneDepth,
        kOceanSceneColorFormat, extent, frame_slot_count);
}

void OceanApp::create_cloud_environment_runtime(const cubey::vulkan::Device& device,
                                                std::uint32_t frame_slot_count) {
    cubey::CloudEnvironmentRuntime& clouds = atmosphere_runtime_.clouds();
    clouds.destroy_probe_resources();
    if (!clouds.surface_resources_created()) {
        return;
    }
    const cubey::render::AtmosphereReflectionProbe& atmosphere_probe =
        atmosphere_runtime_.reflection_probe();
    clouds.create_resources(device,
                            cubey::render::CloudEnvironmentProbeConfig{
                                .extent = ocean_config_.cloud_environment_extent,
                                .mip_levels = 5U,
                                .view_steps = 32U,
                                .update_hz = ocean_config_.cloud_environment_update_hz,
                                .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                .frame_slot_count = frame_slot_count,
                            },
                            clouds.generated_resources(), atmosphere_probe.sky_radiance_cube());
    const std::filesystem::path shader_dir = CUBEY_OCEAN_SHADER_DIR;
    clouds.create_pipelines(
        device, cubey::render::CloudEnvironmentProbePipelineConfig{
                    .cloud_march = cubey::render::compute_shader_file(
                        shader_dir / "surface_cloud_march.comp.spv"),
                    .prefilter_vertex =
                        cubey::render::vertex_shader_file(shader_dir / "atmosphere.vert.spv"),
                    .prefilter_fragment = cubey::render::fragment_shader_file(
                        shader_dir / "cloud_environment_prefilter.frag.spv"),
                });
}

void OceanApp::create_cloud_planar_reflection(const cubey::vulkan::Device& device,
                                              VkExtent2D target_extent,
                                              std::uint32_t frame_slot_count) {
    cloud_planar_reflection_.destroy();
    if (!atmosphere_runtime_.clouds().surface_resources_created()) {
        return;
    }
    cloud_planar_reflection_.create_resources(
        device,
        cubey::render::CloudPlanarReflectionConfig{
            .target_extent = target_extent,
            .resolution_scale = ocean_config_.cloud_planar_resolution_scale,
            .mip_levels = 6U,
            .view_steps = ocean_config_.cloud_planar_view_steps,
            .guard_band = ocean_config_.cloud_planar_guard_band,
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .frame_slot_count = frame_slot_count,
        },
        atmosphere_runtime_.clouds().generated_resources());
    const std::filesystem::path shader_dir = CUBEY_OCEAN_SHADER_DIR;
    cloud_planar_reflection_.create_pipelines(
        device,
        cubey::render::CloudPlanarReflectionPipelineConfig{
            .cloud_march =
                cubey::render::compute_shader_file(shader_dir / "surface_cloud_march.comp.spv"),
            .filter_vertex = cubey::render::vertex_shader_file(shader_dir / "cloud.vert.spv"),
            .filter_fragment = cubey::render::fragment_shader_file(
                shader_dir / "cloud_planar_filter.frag.spv"),
        });
}

[[nodiscard]] bool OceanApp::cloud_environment_active() const noexcept {
    return cloud_products_enabled() && ocean_config_.cloud_reflection_strength > 0.0F;
}

[[nodiscard]] bool OceanApp::cloud_planar_reflection_active() const noexcept {
    return cloud_products_enabled() && ocean_config_.cloud_reflection_strength > 0.0F &&
           ocean_config_.cloud_reflection_source == OceanCloudReflectionSource::Planar;
}

void OceanApp::update_cloud_shadow_fallback_descriptor(const cubey::vulkan::Device& device,
                                                       cubey::render::FrameSlot frame_slot) {
    if (!cloud_shadow_fallback_texture_.has_value()) {
        throw std::runtime_error("ocean cloud shadow fallback is not initialized");
    }
    const cubey::render::Texture2D& fallback = cloud_shadow_fallback_texture_.value();
    ocean_gpu_.update_cloud_shadow_descriptor(device, frame_slot, fallback.sampler().handle(),
                                              fallback.view(),
                                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void OceanApp::update_cloud_planar_reflection_descriptor(const cubey::vulkan::Device& device,
                                                         cubey::render::FrameSlot frame_slot) {
    if (cloud_planar_reflection_.resources_created()) {
        const cubey::render::CloudPlanarReflectionSnapshot planar =
            cloud_planar_reflection_.snapshot(frame_slot);
        if (planar.valid && planar.texture != nullptr) {
            ocean_gpu_.update_cloud_planar_reflection_descriptor(device, frame_slot,
                                                                 *planar.texture);
            return;
        }
    }
    if (!cloud_reflection_fallback_texture_.has_value()) {
        throw std::runtime_error("ocean cloud planar reflection fallback is not initialized");
    }
    ocean_gpu_.update_cloud_planar_reflection_descriptor(
        device, frame_slot, cloud_reflection_fallback_texture_.value());
}

void OceanApp::refresh_cloud_weather_if_needed(const cubey::vulkan::Device& device,
                                               cubey::vulkan::GpuRuntime& gpu) {
    cubey::CloudEnvironmentRuntime& clouds = atmosphere_runtime_.clouds();
    if (!clouds.surface_resources_created()) {
        return;
    }
    clouds.set_config(ocean_cloud_config(ocean_surface_frame()));
    clouds.update_weather_texture(device, gpu, cloud_runtime_shader_files().generated.weather);
}

void OceanApp::sync_cloud_runtime_after_ui(cubey::host::WindowedAppContext& context) {
    if (!clouds_config_.enabled || atmosphere_runtime_.clouds().surface_resources_created()) {
        return;
    }
    cubey::vulkan::check(vkDeviceWaitIdle(context.device().handle()),
                         "vkDeviceWaitIdle ocean cloud enable");
    static_cast<void>(context.gpu().drain());
    create_cloud_resources(context.device(), context.gpu());
    create_cloud_pipelines(context.device(), context.swapchain().extent(),
                           context.frame_slot_count());
    create_cloud_environment_runtime(context.device(), context.frame_slot_count());
    create_cloud_planar_reflection(context.device(), context.swapchain().extent(),
                                   context.frame_slot_count());
    static_cast<void>(context.gpu().drain());
}

void OceanApp::create_reference_pillar_resources(cubey::vulkan::Device& device,
                                                 cubey::vulkan::GpuRuntime& gpu,
                                                 VkExtent2D extent) {
    reference_pillar_pipeline_.reset();
    reference_pillar_mesh_.reset();

    const cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormal> mesh =
        make_ocean_reference_pillar_mesh();
    reference_pillar_mesh_.emplace(gpu, mesh.mesh_config());

    const cubey::render::VertexInputLayout vertex_layout =
        cubey::render::vertex_position_color_normal_input_layout();
    const std::filesystem::path shader_dir = CUBEY_OCEAN_SHADER_DIR;
    const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
        cubey::render::vertex_shader_file(shader_dir / "ocean_reference_pillar.vert.spv"),
        cubey::render::fragment_shader_file(shader_dir / "ocean_reference_pillar.frag.spv"),
    };
    reference_pillar_pipeline_.emplace(
        device, cubey::render::GraphicsPipelineFileResourceConfig{
                    .extent = extent,
                    .color_format = kOceanSceneColorFormat,
                    .depth_format = kOceanDepthFormat,
                    .shader_stage_files = shader_stage_files,
                    .vertex_bindings = vertex_layout.bindings(),
                    .vertex_attributes = vertex_layout.attribute_descriptions(),
                    .material_pass = ocean_reference_pillar_pass_info(),
                });
}

void OceanApp::destroy_swapchain_resources() {
    graph_executor_.clear();
    cloud_planar_reflection_.destroy();
    hdr_post_frame_.destroy();
    reference_pillar_pipeline_.reset();
    reference_pillar_mesh_.reset();
    cloud_shadow_fallback_texture_.reset();
    cloud_reflection_fallback_texture_.reset();
    atmosphere_background_.destroy();
    ocean_gpu_.reset();
    atmosphere_runtime_.destroy();
    terrain_ocean_fields_texture_.reset();
    terrain_ocean_field_uniforms_.reset();
    terrain_ocean_fields_ = {};
    atmosphere_background_atlases_.reset();
    pipeline_color_format_ = VK_FORMAT_UNDEFINED;
    textures_initialized_ = false;
    spectrum_initialized_ = false;
    foam_initialized_ = false;
    gpu_config_.reset();
}

void OceanApp::sync_gpu_resources(cubey::host::WindowedAppContext& context) {
    validate_ocean_config(ocean_config_);
    if (!gpu_config_.has_value()) {
        create_pipeline(context.device(), context.gpu(), context.swapchain().format(),
                        context.swapchain().extent(), context.frame_slot_count());
        return;
    }
    if (ocean_resource_layout_changed(ocean_config_, gpu_config_.value())) {
        cubey::vulkan::check(vkDeviceWaitIdle(context.device().handle()),
                             "vkDeviceWaitIdle before ocean resource recreation");
        create_pipeline(context.device(), context.gpu(), context.swapchain().format(),
                        context.swapchain().extent(), context.frame_slot_count());
        return;
    }
    if (ocean_terrain_field_source_changed(ocean_config_, gpu_config_.value())) {
        cubey::vulkan::check(vkDeviceWaitIdle(context.device().handle()),
                             "vkDeviceWaitIdle before ocean terrain field recreation");
        terrain_ocean_fields_texture_.reset();
        terrain_ocean_field_uniforms_.reset();
        create_terrain_ocean_field_resources(context.device(), context.gpu(),
                                             context.frame_slot_count());
        ocean_gpu_.update_terrain_ocean_field_descriptor(context.device(),
                                                         terrain_ocean_fields_texture_.value());
        update_terrain_ocean_field_uniform_descriptors(context.device());
    }
    if (ocean_wave_source_changed(ocean_config_, gpu_config_.value())) {
        spectrum_initialized_ = false;
        foam_initialized_ = false;
        spectrum_diagnostics_ = ocean_spectrum_diagnostics(ocean_config_);
    }
    if (ocean_cloud_environment_resource_changed(ocean_config_, gpu_config_.value()) &&
        atmosphere_runtime_.clouds().surface_resources_created()) {
        cubey::vulkan::check(vkDeviceWaitIdle(context.device().handle()),
                             "vkDeviceWaitIdle before cloud environment recreation");
        create_cloud_environment_runtime(context.device(), context.frame_slot_count());
    }
    if (ocean_cloud_planar_resource_changed(ocean_config_, gpu_config_.value()) &&
        atmosphere_runtime_.clouds().surface_resources_created()) {
        cubey::vulkan::check(vkDeviceWaitIdle(context.device().handle()),
                             "vkDeviceWaitIdle before cloud planar reflection recreation");
        create_cloud_planar_reflection(context.device(), context.swapchain().extent(),
                                       context.frame_slot_count());
    }
    if (ocean_config_.cloud_reflection_source != gpu_config_->cloud_reflection_source) {
        atmosphere_runtime_.clouds().invalidate();
    }
    gpu_config_ = ocean_config_;
    sync_cloud_runtime_after_ui(context);
    refresh_cloud_weather_if_needed(context.device(), context.gpu());
}
} // namespace cubey::projects::ocean::detail
