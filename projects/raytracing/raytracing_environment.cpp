#include "raytracing_environment.h"
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/gpu_timestamps.h>
#include <cubey/vulkan/immediate_commands.h>
#include <glm/gtc/constants.hpp>
#include <set>

namespace cubey::projects::raytracing {
namespace {
auto shader(const char* p) {
    return std::filesystem::path(CUBEY_RAYTRACING_SHADER_DIR) / p;
}
render::AtmosphereEnvironmentConfig state(const Config& c, float eye_y) {
    render::AtmosphereEnvironmentConfig e;
    e.sun_elevation_degrees = glm::degrees(c.sun_elevation);
    e.sun_azimuth_degrees = glm::degrees(c.sun_azimuth);
    e.camera_altitude_km = std::max(eye_y, .05F) * .001F;
    e.moon.enabled = false;
    e.render_night_sky = false;
    e.render_moon_disk = false;
    e.reference_geometry_enabled = false;
    e.ground_mode = render::AtmosphereEnvironmentGroundMode::SkyOnlyNoGroundOcclusion;
    return e;
}
} // namespace
Environment::Environment(vulkan::Device& device, vulkan::GpuRuntime& gpu, const Config& c,
                         std::uint32_t slots) {
    const auto start = std::chrono::steady_clock::now();
    active_ = c.environment == "atmosphere";
    try {
        fallback_.emplace(
            render::create_generated_pbr_environment(device, gpu,
                                                     {.irradiance_extent = active_ ? 4U : 1U,
                                                      .prefiltered_extent = active_ ? 4U : 1U,
                                                      .prefiltered_mip_levels = 1,
                                                      .brdf_lut_extent = active_ ? 128U : 1U}));
        fallback_ms_ =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        if (active_) {
            atmosphere_.set_environment(state(c, c.target_y + std::sin(c.pitch) * c.distance));
            placeholders_.emplace(
                render::create_atmosphere_background_placeholder_textures(device, gpu));
            atmosphere_.create_resources(
                device, {.reflection_extent = 128,
                         .reflection_mip_levels = 5,
                         .reflection_update_hz = 4,
                         .frame_slot_count = slots,
                         .atmosphere_textures = placeholders_->bindings(),
                         .reflection_ground_mode =
                             render::AtmosphereEnvironmentGroundMode::SkyOnlyNoGroundOcclusion,
                         .consumer_stage_mask = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR});
            const auto pipeline_start = std::chrono::steady_clock::now();
            atmosphere_.create_pipelines(
                device, {.atmosphere_vertex_shader = shader("atmosphere.vert.spv"),
                         .atmosphere_fragment_shader = shader("atmosphere.frag.spv"),
                         .reflection_prefilter_vertex_shader = shader("atmosphere.vert.spv"),
                         .reflection_prefilter_fragment_shader =
                             shader("atmosphere_reflection_prefilter.frag.spv")});
            pipeline_ms_ = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - pipeline_start)
                               .count();
            (void)gpu.submit_and_wait(
                {.label = "raytracing settle shared sky",
                 .work = [&](vulkan::GpuOwnerContext& owner) {
                     vulkan::ImmediateCommands commands(owner);
                     vulkan::GpuTimestampProfiler timer(device, 1, 1);
                     timer.begin_frame(commands.command_buffer(), 0);
                     timer.begin_pass(commands.command_buffer(), 0, "environment.probe-build");
                     atmosphere_.record_pending_update(
                         vulkan::CommandRecorder(commands.command_buffer()), {0, slots});
                     timer.end_pass(commands.command_buffer(), 0);
                     commands.submit_and_wait();
                     timer.collect(0);
                     for (const auto& value : timer.latest_timings())
                         probe_gpu_ms_ += value.milliseconds;
                 }});
            atmosphere_.advance(.25);
            if (!atmosphere_.reflection_probe().snapshot().valid)
                throw std::runtime_error("RT sky did not settle");
        }
    } catch (...) {
        atmosphere_.destroy();
        throw;
    }
    setup_ms_ =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
Environment::~Environment() {
    atmosphere_.destroy();
}
EnvironmentUniform Environment::prepare(const Config& c, VkCommandBuffer command,
                                        render::FrameSlot slot, math::Vec3 eye, math::Vec3& sun) {
    EnvironmentUniform result;
    result.transport = {
        c.reflection_source == "scene" ? 1.F : 0.F, c.transmission_source == "scene" ? 1.F : 0.F,
        c.scene.find("refraction-") == 0 || c.scene == "slab" ? 1.F : 0.F,
        c.scene.find("reflection-") == 0 || c.scene.find("refraction-") == 0 || c.scene == "slab"
            ? 1.F
            : 0.F};
    result.limits = {c.max_events, c.max_path_rays, c.max_total_rays, 8};
    result.glass = {c.ior, c.attenuation_distance, c.throughput_cutoff, 0};
    result.attenuation = {c.attenuation_red, c.attenuation_green, c.attenuation_blue, 0};
    if (active_) {
        atmosphere_.set_environment(state(c, eye.y));
        const auto now = std::chrono::steady_clock::now();
        atmosphere_.advance(c.common.headless
                                ? (c.sun_speed != 0 ? 1.0 / std::max(c.common.fps, 1U) : 0)
                                : std::chrono::duration<double>(now - previous_).count());
        previous_ = now;
        atmosphere_.record_pending_update(vulkan::CommandRecorder(command), slot);
        const auto& light = atmosphere_.lighting();
        sun = light.primary_light_direction;
        result.direct_color = {
            light.primary_light_color * light.primary_light_intensity * (c.intensity / 3.F), 0};
        for (std::size_t i = 0; i < 9; ++i)
            result.irradiance[i] = {light.diffuse_irradiance_sh[i] * (c.intensity / 3.F), 0};
        const auto bindings = atmosphere_.pbr_environment_bindings(*fallback_, false);
        result.options = {bindings.prefiltered_blend, float(bindings.prefiltered_mip_levels - 1), 1,
                          c.intensity / 3.F};
    }
    return result;
}
void Environment::bind(const vulkan::Device& device, VkDescriptorSet set) const {
    const auto bindings = active_ ? atmosphere_.pbr_environment_bindings(*fallback_, false)
                                  : render::pbr_environment_texture_bindings(*fallback_);
    const auto& sky =
        active_ ? atmosphere_.reflection_probe().sky_radiance_cube() : fallback_->prefiltered_cube;
    vulkan::DescriptorWriteBatch{}
        .combined_image_sampler(set, 7, sky.sampler().handle(), sky.view(),
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .combined_image_sampler(set, 8, bindings.prefiltered_sampler, bindings.prefiltered_view,
                                bindings.prefiltered_layout)
        .combined_image_sampler(set, 9, bindings.previous_prefiltered_sampler,
                                bindings.previous_prefiltered_view,
                                bindings.previous_prefiltered_layout)
        .combined_image_sampler(set, 10, bindings.brdf_lut_sampler, bindings.brdf_lut_view,
                                bindings.brdf_lut_layout)
        .update(device);
}
std::uint64_t Environment::generation() const {
    return active_ ? atmosphere_.reflection_probe().snapshot().generation : 0;
}
std::uint64_t Environment::texture_allocation_bytes(const vulkan::Device& device) const {
    std::set<VkImage> images{fallback_->irradiance_cube.handle(),
                             fallback_->prefiltered_cube.handle(), fallback_->brdf_lut.handle()};
    if (active_) {
        const auto snapshot = atmosphere_.reflection_probe().snapshot();
        images.insert(atmosphere_.reflection_probe().sky_radiance_cube().handle());
        if (snapshot.current)
            images.insert(snapshot.current->handle());
        if (snapshot.previous)
            images.insert(snapshot.previous->handle());
        images.insert(placeholders_->lunar_surface.handle());
        images.insert(placeholders_->night_sky.handle());
    }
    std::uint64_t bytes = 0;
    if (active_) {
        const auto snapshot = atmosphere_.reflection_probe().snapshot();
        if (snapshot.current == snapshot.previous) {
            VkMemoryRequirements spare{};
            vkGetImageMemoryRequirements(device.handle(), snapshot.current->handle(), &spare);
            bytes += spare.size;
        }
    }
    for (auto image : images) {
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device.handle(), image, &requirements);
        bytes += requirements.size;
    }
    return bytes;
}
} // namespace cubey::projects::raytracing
