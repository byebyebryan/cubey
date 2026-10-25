#include "sdf_renderer.h"
#include <cubey/vulkan/immediate_commands.h>
#include <iostream>

namespace cubey::projects::sdf {
void Renderer::create_environment(vulkan::Device& device, vulkan::GpuRuntime& gpu,
                                  const SdfConfig& config, std::uint32_t slots) {
    material_atmosphere_ = config.lighting == "material-atmosphere";
    render::AtmosphereEnvironmentConfig environment;
    environment.sun_elevation_degrees = config.sun_elevation;
    environment.sun_azimuth_degrees = config.sun_azimuth;
    environment.moon.enabled = false;
    environment.reference_geometry_enabled = false;
    if (material_atmosphere_) {
        environment.camera_altitude_km = .002F;
        environment.ground_mode = render::AtmosphereEnvironmentGroundMode::SkyOnlyNoGroundOcclusion;
        environment.render_night_sky = false;
        environment.render_moon_disk = false;
    }
    atmosphere_.set_environment(environment);
    if (!material_atmosphere_)
        return;
    const auto start = std::chrono::steady_clock::now();
    atmosphere_textures_.emplace(
        render::create_atmosphere_background_placeholder_textures(device, gpu));
    atmosphere_.create_resources(
        device, {.reflection_extent = config.environment_extent,
                 .reflection_mip_levels = 5,
                 .reflection_update_hz = config.lighting_cycle == "frozen" ? 4.0F : 30.0F,
                 .frame_slot_count = slots,
                 .atmosphere_textures = atmosphere_textures_->bindings(),
                 .reflection_ground_mode = render::AtmosphereEnvironmentGroundMode::Ground});
    const auto path = std::filesystem::path(CUBEY_SDF_SHADER_DIR);
    atmosphere_.create_pipelines(
        device, {.atmosphere_vertex_shader = path / "atmosphere.vert.spv",
                 .atmosphere_fragment_shader = path / "atmosphere.frag.spv",
                 .reflection_prefilter_vertex_shader = path / "atmosphere.vert.spv",
                 .reflection_prefilter_fragment_shader =
                     path / "atmosphere_reflection_prefilter.frag.spv"});
    sky_background_.create_materials(
        device, {.frame_slot_count = slots, .textures = atmosphere_textures_->bindings()});
    const std::array sky_stages{render::vertex_shader_file(path / "atmosphere.vert.spv"),
                                render::fragment_shader_file(path / "atmosphere.frag.spv")};
    sky_background_.create_pipeline(device, {.extent = scene_extent_,
                                             .color_format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                             .depth_format = VK_FORMAT_D32_SFLOAT,
                                             .shader_stage_files = sky_stages});
    static_cast<void>(gpu.submit_and_wait(
        {.label = "sdf settle atmosphere probe", .work = [&](vulkan::GpuOwnerContext& owner) {
             vulkan::ImmediateCommands commands(owner);
             atmosphere_.record_pending_update(vulkan::CommandRecorder(commands.command_buffer()),
                                               {0, slots});
             commands.submit_and_wait();
         }}));
    atmosphere_.advance(.25);
    if (config.clouds)
        create_clouds(device, gpu, config, slots);
    const auto snapshot = atmosphere_.reflection_probe().snapshot();
    if (!snapshot.valid || snapshot.blend != 1)
        throw std::runtime_error("SDF atmosphere probe failed to settle");
    environment_clock_ = std::chrono::steady_clock::now();
    std::cout << "sdf: environment ready source=" << (clouds_enabled_ ? "clouds" : "atmosphere")
              << " generation=" << snapshot.generation << " extent=" << config.environment_extent
              << " setup_ms="
              << std::chrono::duration<double, std::milli>(environment_clock_ - start).count()
              << '\n';
}
void Renderer::bind_environment(const vulkan::Device& device, render::FrameSlot slot) {
    const auto textures = material_atmosphere_
                              ? atmosphere_.pbr_environment_bindings(*study_environment_)
                              : render::pbr_environment_texture_bindings(*study_environment_);
    vulkan::DescriptorWriteBatch writes;
    writes.combined_image_sampler(descriptors_->set(slot.index), 9, textures.prefiltered_sampler,
                                  textures.prefiltered_view, textures.prefiltered_layout);
    writes.combined_image_sampler(
        descriptors_->set(slot.index), 11, textures.previous_prefiltered_sampler,
        textures.previous_prefiltered_view, textures.previous_prefiltered_layout);
    if (material_atmosphere_) {
        const auto& sky = atmosphere_.reflection_probe().sky_radiance_cube();
        writes.combined_image_sampler(descriptors_->set(slot.index), 12, sky.sampler().handle(),
                                      sky.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    } else
        writes.combined_image_sampler(descriptors_->set(slot.index), 12,
                                      textures.prefiltered_sampler, textures.prefiltered_view,
                                      textures.prefiltered_layout);
    writes.update(device);
}
void Renderer::prepare_environment(vulkan::Device& device, VkCommandBuffer command,
                                   render::FrameSlot slot, const SdfConfig& config, math::Vec3 eye,
                                   math::Vec3 target) {
    auto environment = atmosphere_.environment();
    environment.sun_elevation_degrees = config.sun_elevation;
    environment.sun_azimuth_degrees = config.sun_azimuth;
    const bool changed = atmosphere_.set_environment(environment);
    if (!material_atmosphere_)
        return;
    const auto now = std::chrono::steady_clock::now();
    const bool cycling = config.lighting_cycle != "frozen";
    const double seconds = environment_seconds_.value_or(0);
    const double delta = cycling ? (previous_environment_seconds_
                                        ? std::max(0.0, seconds - *previous_environment_seconds_)
                                        : 0)
                                 : std::chrono::duration<double>(now - environment_clock_).count();
    if (cycling &&
        (!previous_environment_seconds_ ||
         static_cast<int>(seconds / 2) != static_cast<int>(*previous_environment_seconds_ / 2)))
        std::cout << "material-cycle time=" << seconds << " sun=" << config.sun_elevation
                  << " azimuth=" << config.sun_azimuth << " cloud=" << config.cloud_time
                  << " exposure=" << config.exposure << '\n';
    atmosphere_.advance(delta);
    if (cycling)
        previous_environment_seconds_ = seconds;
    environment_clock_ = now;
    atmosphere_.record_pending_update(vulkan::CommandRecorder(command), slot);
    if (clouds_enabled_) {
        auto& clouds = atmosphere_.clouds();
        clouds.set_elapsed_seconds(config.cloud_time);
        const auto sky_generation = atmosphere_.reflection_probe().snapshot().generation;
        if ((cycling || sky_generation != cloud_sky_generation_) &&
            clouds.record_pending_update(vulkan::CommandRecorder(command), slot,
                                         cycling ? cloud_frame(eye, target)
                                                 : cloud_frame({0, 0, 0}, {0, 0, -1}))) {
            cloud_sky_generation_ = sky_generation;
            std::cout << "sdf: cloud probe capture generation=" << clouds.snapshot().generation
                      << '\n';
        }
    }
    bind_environment(device, slot);
    if (changed)
        std::cout << "sdf: environment refresh geometry_rebuilt=0 generation="
                  << atmosphere_.reflection_probe().snapshot().generation << '\n';
}
CloudEnvironmentRuntimeFrame Renderer::cloud_frame(math::Vec3 eye, math::Vec3 target) const {
    const auto forward = glm::normalize(target - eye),
               right = glm::normalize(glm::cross(forward, math::Vec3{0, 1, 0}));
    return atmosphere_.clouds().frame(
        {.camera_position = eye + math::Vec3{0, 2, 0},
         .camera_right = right,
         .camera_up = glm::cross(right, forward),
         .camera_forward = forward,
         .tan_half_fovy = std::tan(glm::radians(22.5F)),
         .target_extent = scene_extent_,
         .near_plane_m = .01F,
         .far_plane_m = 100,
         .sky_only_background = cloud_sky_only_,
         .scene_depth_mode = render::CloudLayerSceneDepthMode::OpaqueForeground},
        atmosphere_.lighting());
}
void Renderer::create_clouds(vulkan::Device& device, vulkan::GpuRuntime& gpu,
                             const SdfConfig& config, std::uint32_t slots) {
    clouds_enabled_ = true;
    cloud_sky_only_ = config.cloud_sky_only;
    CloudEnvironmentConfig cloud;
    apply_cloud_environment_weather_preset(cloud, CloudEnvironmentWeatherPreset::BrokenCumulus);
    cloud.layer.planet_radius_m = atmosphere_.environment().bottom_radius_km * 1000;
    cloud.layer.distance_mode = cloud_environment_distance_mode_from_name(config.cloud_distance);
    cloud.layer.background_mode = render::CloudLayerBackgroundMode::Atmosphere;
    cloud.layer.coverage = config.cloud_coverage;
    cloud.layer.quality = cloud_environment_quality_from_name(config.cloud_quality);
    cloud.layer.view_samples = static_cast<std::int32_t>(config.cloud_view_samples);
    cloud.layer.resolve_mode = cloud_environment_resolve_mode_from_name(config.cloud_resolve);
    cloud.layer.debug_view = render::cloud_layer_debug_view_from_name(config.cloud_debug);
    cloud.layer.view_steps_override = 64;
    cloud.layer.temporal_enabled = false;
    std::cout << "sdf: cloud view distance=" << config.cloud_distance
              << " horizon_layer=" << cloud.layer.horizon_layer_enabled
              << " sky_only_background=" << cloud_sky_only_ << " quality=" << config.cloud_quality
              << " samples=" << config.cloud_view_samples << " resolve=" << config.cloud_resolve
              << " debug=" << config.cloud_debug << '\n';
    const auto path = std::filesystem::path(CUBEY_SDF_SHADER_DIR);
    cloud_shaders_ = render::cloud_layer_runtime_shader_files(
        path, render::CloudLayerCompositeMode::ExternalBackgroundSceneDepth);
    auto& clouds = atmosphere_.clouds();
    clouds.create_surface_resources(device, gpu, cloud_shaders_.generated, cloud);
    clouds.create_surface_target_resources(
        device, cloud_shaders_, render::CloudLayerCompositeMode::ExternalBackgroundSceneDepth,
        VK_FORMAT_R16G16B16A16_SFLOAT, scene_extent_, slots);
    clouds.create_resources(device,
                            {.extent = config.environment_extent,
                             .mip_levels = 5,
                             .view_steps = 64,
                             .frame_slot_count = slots},
                            clouds.generated_resources(),
                            atmosphere_.reflection_probe().sky_radiance_cube());
    clouds.create_pipelines(
        device, {.cloud_march = cloud_shaders_.surface_march,
                 .prefilter_vertex = render::vertex_shader_file(path / "atmosphere.vert.spv"),
                 .prefilter_fragment =
                     render::fragment_shader_file(path / "cloud_environment_prefilter.frag.spv")});
    clouds.set_elapsed_seconds(config.cloud_time);
    const math::Vec3 target{0, config.target_y, 0};
    const auto eye =
        target + math::Vec3{std::cos(config.pitch) * std::sin(config.yaw), std::sin(config.pitch),
                            std::cos(config.pitch) * std::cos(config.yaw)} *
                     config.distance;
    static_cast<void>(gpu.submit_and_wait(
        {.label = "sdf settle frozen cloud probe", .work = [&](vulkan::GpuOwnerContext& owner) {
             vulkan::ImmediateCommands commands(owner);
             static_cast<void>(clouds.record_pending_update(
                 vulkan::CommandRecorder(commands.command_buffer()), {0, slots},
                 config.lighting_cycle == "frozen" ? cloud_frame({0, 0, 0}, {0, 0, -1})
                                                   : cloud_frame(eye, target)));
             commands.submit_and_wait();
         }}));
    clouds.advance(.25);
    clouds.set_elapsed_seconds(config.cloud_time);
    if (!clouds.snapshot().valid || clouds.snapshot().blend != 1)
        throw std::runtime_error("SDF cloud probe failed to settle");
    cloud_sky_generation_ = atmosphere_.reflection_probe().snapshot().generation;
    std::cout << "sdf: cloud probe capture generation=" << clouds.snapshot().generation << '\n';
}
} // namespace cubey::projects::sdf
