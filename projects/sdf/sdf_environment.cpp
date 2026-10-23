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
    atmosphere_.create_resources(device, {.reflection_extent = config.environment_extent,
                                          .reflection_mip_levels = 5,
                                          .frame_slot_count = slots,
                                          .atmosphere_textures = atmosphere_textures_->bindings(),
                                          .reflection_ground_mode = environment.ground_mode});
    const auto path = std::filesystem::path(CUBEY_SDF_SHADER_DIR);
    atmosphere_.create_pipelines(
        device, {.atmosphere_vertex_shader = path / "atmosphere.vert.spv",
                 .atmosphere_fragment_shader = path / "atmosphere.frag.spv",
                 .reflection_prefilter_vertex_shader = path / "atmosphere.vert.spv",
                 .reflection_prefilter_fragment_shader =
                     path / "atmosphere_reflection_prefilter.frag.spv"});
    static_cast<void>(gpu.submit_and_wait(
        {.label = "sdf settle atmosphere probe", .work = [&](vulkan::GpuOwnerContext& owner) {
             vulkan::ImmediateCommands commands(owner);
             atmosphere_.record_pending_update(vulkan::CommandRecorder(commands.command_buffer()),
                                               {0, slots});
             commands.submit_and_wait();
         }}));
    atmosphere_.advance(.25);
    const auto snapshot = atmosphere_.reflection_probe().snapshot();
    if (!snapshot.valid || snapshot.blend != 1)
        throw std::runtime_error("SDF atmosphere probe failed to settle");
    environment_clock_ = std::chrono::steady_clock::now();
    std::cout << "sdf: environment ready source=atmosphere generation=" << snapshot.generation
              << " extent=" << config.environment_extent << " setup_ms="
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
                                   render::FrameSlot slot, const SdfConfig& config) {
    auto environment = atmosphere_.environment();
    environment.sun_elevation_degrees = config.sun_elevation;
    environment.sun_azimuth_degrees = config.sun_azimuth;
    const bool changed = atmosphere_.set_environment(environment);
    if (!material_atmosphere_)
        return;
    const auto now = std::chrono::steady_clock::now();
    atmosphere_.advance(std::chrono::duration<double>(now - environment_clock_).count());
    environment_clock_ = now;
    atmosphere_.record_pending_update(vulkan::CommandRecorder(command), slot);
    bind_environment(device, slot);
    if (changed)
        std::cout << "sdf: environment refresh geometry_rebuilt=0 generation="
                  << atmosphere_.reflection_probe().snapshot().generation << '\n';
}
} // namespace cubey::projects::sdf
