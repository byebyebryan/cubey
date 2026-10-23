#include "pbr_materials_app_internal.h"
#include <cubey/vulkan/immediate_commands.h>
#include <iostream>

namespace cubey::projects::pbr_materials::detail {
render::ViewRayBasis3D PbrMaterialsApp::view_rays(VkExtent2D extent) const {
    const auto transform = orbit_camera_transform({.distance = orbit_controller_.distance(),
                                                   .yaw = config_.yaw + orbit_controller_.yaw(),
                                                   .pitch = orbit_controller_.pitch()});
    return render::view_ray_basis_3d(
        transform.rotation, static_cast<float>(extent.width) / static_cast<float>(extent.height),
        glm::radians(45.0F));
}
CloudEnvironmentRuntimeFrame PbrMaterialsApp::cloud_frame(VkExtent2D extent) const {
    const auto rays = view_rays(extent);
    const auto camera = orbit_camera_transform({.distance = orbit_controller_.distance(),
                                                .yaw = config_.yaw + orbit_controller_.yaw(),
                                                .pitch = orbit_controller_.pitch()});
    return atmosphere_.clouds().frame(
        {.camera_position = camera.translation + math::Vec3{0, 2, 0},
         .camera_right = math::Vec3{rays.right_aspect},
         .camera_up = math::Vec3{rays.up_tan_half_fovy},
         .camera_forward = math::Vec3{rays.forward},
         .tan_half_fovy = rays.up_tan_half_fovy.w,
         .target_extent = extent,
         .near_plane_m = .01F,
         .far_plane_m = 100,
         .sky_only_background = true,
         .scene_depth_mode = render::CloudLayerSceneDepthMode::OpaqueForeground},
        atmosphere_.lighting());
}
void PbrMaterialsApp::create_environment(const vulkan::Device& device, vulkan::GpuRuntime& gpu,
                                         std::uint32_t slots) {
    if (config_.environment == "studio") {
        std::cout << "pbr_materials: studio atmosphere_resources=0 cloud_resources=0\n";
        return;
    }
    if (config_.environment != "clear" && config_.environment != "clouds")
        throw std::invalid_argument("lighting must be studio, clear or clouds");
    const auto start = std::chrono::steady_clock::now();
    render::AtmosphereEnvironmentConfig e;
    e.sun_elevation_degrees = 8;
    e.sun_azimuth_degrees = -155;
    e.moon.enabled = false;
    e.reference_geometry_enabled = false;
    e.camera_altitude_km = .002F;
    e.ground_mode = render::AtmosphereEnvironmentGroundMode::SkyOnlyNoGroundOcclusion;
    e.render_night_sky = false;
    e.render_moon_disk = false;
    atmosphere_.set_environment(e);
    atmosphere_textures_.emplace(
        render::create_atmosphere_background_placeholder_textures(device, gpu));
    atmosphere_.create_resources(
        device, {.reflection_extent = 128,
                 .reflection_mip_levels = 5,
                 .frame_slot_count = slots,
                 .atmosphere_textures = atmosphere_textures_->bindings(),
                 .reflection_ground_mode = render::AtmosphereEnvironmentGroundMode::Ground});
    const auto path = std::filesystem::path(CUBEY_PBR_MATERIALS_SHADER_DIR);
    atmosphere_.create_pipelines(
        device, {.atmosphere_vertex_shader = path / "atmosphere.vert.spv",
                 .atmosphere_fragment_shader = path / "atmosphere.frag.spv",
                 .reflection_prefilter_vertex_shader = path / "atmosphere.vert.spv",
                 .reflection_prefilter_fragment_shader =
                     path / "atmosphere_reflection_prefilter.frag.spv"});
    (void)gpu.submit_and_wait(
        {.label = "material workshop settle sky", .work = [&](vulkan::GpuOwnerContext& owner) {
             vulkan::ImmediateCommands commands(owner);
             atmosphere_.record_pending_update(vulkan::CommandRecorder(commands.command_buffer()),
                                               {0, slots});
             commands.submit_and_wait();
         }});
    atmosphere_.advance(.25);
    if (!atmosphere_.reflection_probe().snapshot().valid)
        throw std::runtime_error("sky probe not ready after completion");
    if (config_.environment == "clouds") {
        CloudEnvironmentConfig c;
        apply_cloud_environment_weather_preset(c, CloudEnvironmentWeatherPreset::BrokenCumulus);
        c.layer.planet_radius_m = e.bottom_radius_km * 1000;
        c.layer.distance_mode = cloud_environment_distance_mode_from_name("auto");
        c.layer.background_mode = render::CloudLayerBackgroundMode::Atmosphere;
        c.layer.coverage = .28F;
        c.layer.quality = cloud_environment_quality_from_name("half");
        c.layer.view_samples = 2;
        c.layer.resolve_mode = cloud_environment_resolve_mode_from_name("terrain-post");
        c.layer.view_steps_override = 64;
        c.layer.temporal_enabled = false;
        cloud_shaders_ = render::cloud_layer_runtime_shader_files(
            path, render::CloudLayerCompositeMode::ExternalBackgroundSceneDepth);
        auto& clouds = atmosphere_.clouds();
        clouds.create_surface_resources(device, gpu, cloud_shaders_.generated, c);
        clouds.create_resources(
            device, {.extent = 128, .mip_levels = 5, .view_steps = 64, .frame_slot_count = slots},
            clouds.generated_resources(), atmosphere_.reflection_probe().sky_radiance_cube());
        clouds.create_pipelines(
            device, {.cloud_march = cloud_shaders_.surface_march,
                     .prefilter_vertex = render::vertex_shader_file(path / "atmosphere.vert.spv"),
                     .prefilter_fragment = render::fragment_shader_file(
                         path / "cloud_environment_prefilter.frag.spv")});
        clouds.set_elapsed_seconds(300);
        (void)gpu.submit_and_wait({.label = "material workshop settle frozen clouds",
                                   .work = [&](vulkan::GpuOwnerContext& owner) {
                                       vulkan::ImmediateCommands commands(owner);
                                       (void)clouds.record_pending_update(
                                           vulkan::CommandRecorder(commands.command_buffer()),
                                           {0, slots}, cloud_frame({1280, 720}));
                                       commands.submit_and_wait();
                                   }});
        clouds.advance(.25);
        clouds.set_elapsed_seconds(300);
        if (!clouds.snapshot().valid)
            throw std::runtime_error("cloud probe not ready after completion");
    }
    std::cout << "pbr_materials: environment ready source=" << config_.environment << " setup_ms="
              << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                     .count()
              << "\n";
}
void PbrMaterialsApp::create_environment_targets(const vulkan::Device& device, VkExtent2D extent,
                                                 std::uint32_t slots) {
    if (config_.environment == "clouds")
        atmosphere_.clouds().create_surface_target_resources(
            device, cloud_shaders_, render::CloudLayerCompositeMode::ExternalBackgroundSceneDepth,
            VK_FORMAT_R16G16B16A16_SFLOAT, extent, slots);
}
void PbrMaterialsApp::switch_environment(const vulkan::Device& device, vulkan::GpuRuntime& gpu,
                                         VkExtent2D extent, VkFormat format) {
    gpu.wait_queue_idle("material workshop lighting switch");
    engine_.renderers().destroy_all_resources();
    atmosphere_.destroy();
    atmosphere_textures_.reset();
    config_.environment = std::move(requested_environment_);
    requested_environment_.clear();
    const auto slots = static_cast<std::uint32_t>(materials_.size());
    create_environment(device, gpu, slots);
    forward_pbr_renderer().create_global_resources(
        device,
        {.environment_textures = config_.environment == "studio"
                                     ? render::pbr_environment_texture_bindings(ibl_environment())
                                     : atmosphere_.pbr_environment_bindings(ibl_environment()),
         .frame_slot_count = slots,
         .atmosphere_background_textures = atmosphere_textures_
                                               ? std::optional{atmosphere_textures_->bindings()}
                                               : std::nullopt});
    create_swapchain_resources(device, extent, format);
    auto edits = scene().create_edit_queue();
    const auto& sky = atmosphere_.lighting();
    auto light =
        config_.environment == "studio"
            ? cubey::directional_light_3d(glm::normalize(math::Vec3{-.35F, .42F, .84F}),
                                          {1.8F, 1.6F, 1.3F}, 1)
            : cubey::directional_light_3d(sky.primary_light_direction, sky.primary_light_color,
                                          sky.primary_light_intensity);
    light.casts_shadows = false;
    edits.lights3d().set_light(light_entity_, light);
    scene().commit(edits);
}

} // namespace cubey::projects::pbr_materials::detail
