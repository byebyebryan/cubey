#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cubey/engine/terrain_surface_environment.h>
#include <cubey/render/material_instance.h>
#include <cubey/vulkan/memory_barriers.h>
#include <limits>
#include <stdexcept>

namespace cubey {
namespace {
struct IrradiancePush {
    math::Vec4 camera_blend, radii_ground, rayleigh, mie, ozone;
    math::Vec4 sun_direction_radius, atmosphere_options, primary_source;
};
static_assert(sizeof(IrradiancePush) == 128U);
} // namespace
render::ViewRayBasis3D
terrain_surface_environment_view_rays(const render::TerrainSurfacePatchCamera& camera) {
    const auto inverse = glm::inverse(camera.view_projection);
    const auto ray = [&](float x, float y) {
        const auto p = inverse * math::Vec4{x, y, 0.5F, 1.0F};
        return math::Vec3(p) / p.w - camera.position;
    };
    const auto center = ray(0, 0);
    const auto length = glm::length(center);
    if (!std::isfinite(length) || length <= 0)
        throw std::runtime_error("invalid surface environment camera");
    return {{(ray(1, 0) - center) / length, 1.0F},
            {(center - ray(0, 1)) / length, 1.0F},
            {center / length, 0.0F}};
}
struct TerrainSurfaceEnvironment::State {
    AtmosphereEnvironmentRunState atmosphere;
    double elapsed = 0;
    std::uint64_t reported_sky = std::numeric_limits<std::uint64_t>::max();
    AtmosphereEnvironmentRuntime runtime;
    std::optional<render::AtmosphereBackgroundAtlasResources> atlases;
    render::AtmosphereBackgroundFrame background;
    VkExtent2D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;
    struct Slot {
        std::optional<vulkan::Buffer> coefficients;
        std::unique_ptr<vulkan::DescriptorSetBundle> descriptors;
        std::uint64_t generation = std::numeric_limits<std::uint64_t>::max();
        IrradiancePush push{};
    };
    std::vector<Slot> slots;
    std::optional<render::ComputePipelineResource> irradiance;
    explicit State(TerrainSurfaceEnvironmentSettings settings) {
        const auto reference = render::terrain_surface_patch_fixed_daylight();
        if (!settings.atmosphere.time_of_day_mode && !settings.atmosphere.time_hours)
            settings.atmosphere.time_of_day_mode = "manual";
        if (!settings.atmosphere.time_paused)
            settings.atmosphere.time_paused = true;
        atmosphere = atmosphere_environment_run_state_from_config(
            settings.atmosphere, {.sun_elevation_degrees = reference.sun_elevation_degrees,
                                  .sun_azimuth_degrees = reference.sun_azimuth_degrees,
                                  .reference_geometry_enabled = false});
    }
};
TerrainSurfaceEnvironment::TerrainSurfaceEnvironment(TerrainSurfaceEnvironmentSettings settings)
    : state_(std::make_unique<State>(std::move(settings))) {}
TerrainSurfaceEnvironment::~TerrainSurfaceEnvironment() = default;
void TerrainSurfaceEnvironment::destroy() {
    state_->background.destroy();
    state_->runtime.destroy();
    state_->irradiance.reset();
    state_->slots.clear();
    state_->atlases.reset();
    state_->format = VK_FORMAT_UNDEFINED;
}
void TerrainSurfaceEnvironment::advance(double delta) {
    if (!std::isfinite(delta) || delta < 0)
        throw std::runtime_error("surface environment delta must be finite and nonnegative");
    auto& s = *state_;
    s.elapsed += delta;
    (void)atmosphere_environment_advance_time(s.atmosphere, delta);
    s.runtime.advance(delta);
}
AtmosphereEnvironmentRunState& TerrainSurfaceEnvironment::atmosphere() noexcept {
    return state_->atmosphere;
}
double TerrainSurfaceEnvironment::elapsed_seconds() const noexcept {
    return state_->elapsed;
}
void TerrainSurfaceEnvironment::select_preset(std::string_view preset) {
    auto& state = state_->atmosphere;
    if (preset == "daylight") {
        const auto reference = render::terrain_surface_patch_fixed_daylight();
        state.solar_time_enabled = false;
        state.time_playing = false;
        state.environment.sun_elevation_degrees = reference.sun_elevation_degrees;
        state.environment.sun_azimuth_degrees = reference.sun_azimuth_degrees;
    } else {
        if (preset != "noon" && preset != "sunset" && preset != "night")
            throw std::runtime_error("unknown surface environment preset");
        state.solar_time_enabled = true;
        state.time_playing = false;
        state.environment.time_of_day.latitude_degrees = 45.0F;
        state.environment.time_of_day.day_of_year = 80.0F;
        state.environment.time_of_day.time_hours = preset == "noon"     ? 12.0F
                                                   : preset == "sunset" ? 18.0F
                                                                        : 0.0F;
    }
    atmosphere_environment_resolve_run_state(state);
}

bool TerrainSurfaceEnvironment::ensure_resources(const vulkan::Device& device,
                                                 vulkan::GpuRuntime& gpu, std::uint32_t slots,
                                                 render::ColorTargetView target,
                                                 const std::filesystem::path& shaders) {
    auto& s = *state_;
    const bool resources_created = s.runtime.resources_created();
    if (!s.runtime.resources_created()) {
        s.atlases.emplace(render::create_atmosphere_background_cached_textures(device, gpu));
        s.runtime.create_resources(
            device, {.frame_slot_count = slots, .atmosphere_textures = s.atlases->bindings()});
        s.runtime.create_pipelines(device, {shaders / "atmosphere.vert.spv",
                                            shaders / "atmosphere.frag.spv",
                                            shaders / "atmosphere.vert.spv",
                                            shaders / "atmosphere_reflection_prefilter.frag.spv"});
        s.slots.resize(slots);
        const std::array<vulkan::DescriptorSetBindingConfig, 3> bindings{
            {{.binding = 0U,
              .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
             {.binding = 1U,
              .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
              .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
             {.binding = 2U,
              .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT}}};
        for (auto& slot : s.slots) {
            slot.coefficients.emplace(
                device, vulkan::device_local_buffer_config(sizeof(math::Vec4) * 10U,
                                                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
            slot.descriptors = std::make_unique<vulkan::DescriptorSetBundle>(
                device, vulkan::DescriptorSetInfo(bindings));
        }
        const std::array<VkPushConstantRange, 1> push{
            {{VK_SHADER_STAGE_COMPUTE_BIT, 0U, sizeof(IrradiancePush)}}};
        render::emplace_single_set_compute_pipeline_resource(
            s.irradiance, device,
            render::compute_shader_file(shaders / "terrain_surface_environment.comp.spv"),
            s.slots.front().descriptors->layout(), push);
        s.background.create_materials(
            device, {.frame_slot_count = slots, .textures = s.atlases->bindings()});
    }
    if (s.format != target.format || s.extent.width != target.extent.width ||
        s.extent.height != target.extent.height) {
        const std::array<render::ShaderStageFile, 2> sky{
            render::vertex_shader_file(shaders / "atmosphere.vert.spv"),
            render::fragment_shader_file(shaders / "atmosphere.frag.spv")};
        s.background.destroy_pipeline();
        s.background.create_pipeline(device, {.extent = target.extent,
                                              .color_format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                              .depth_format = VK_FORMAT_D32_SFLOAT,
                                              .shader_stage_files = sky});
        s.format = target.format;
        s.extent = target.extent;
    }
    return !resources_created;
}
TerrainSurfaceWeatherFrame
TerrainSurfaceEnvironment::frame(const vulkan::Device& device,
                                 const vulkan::CommandRecorder& recorder, render::FrameSlot slot,
                                 const render::TerrainSurfacePatchCamera& camera,
                                 const render::GeneratedPbrEnvironment& fallback) {
    auto& s = *state_;
    atmosphere_environment_resolve_run_state(s.atmosphere);
    s.runtime.set_environment(s.atmosphere.environment);
    s.runtime.record_pending_update(recorder, slot);
    const auto rays = terrain_surface_environment_view_rays(camera);
    const auto atmosphere =
        render::atmosphere_environment_frame_uniforms(s.runtime.environment(), {.view_rays = rays});
    auto background_config = s.runtime.environment();
    background_config.ground_mode =
        render::AtmosphereEnvironmentGroundMode::SkyOnlyNoGroundOcclusion;
    s.background.upload(slot, render::atmosphere_environment_frame_uniforms(background_config,
                                                                            {.view_rays = rays}));
    const auto probe = s.runtime.reflection_probe().snapshot();
    auto& output = s.slots.at(slot.index);
    IrradiancePush push{
        atmosphere.camera_position_radius,
        atmosphere.radii_ground,
        atmosphere.rayleigh,
        atmosphere.mie,
        atmosphere.ozone,
        atmosphere.sun_direction_radius,
        atmosphere.atmosphere_options,
        {s.runtime.lighting().moon_color * s.runtime.lighting().moon_intensity,
         s.runtime.lighting().moon_intensity > s.runtime.lighting().sun_intensity ? 1.0F : 0.0F}};
    push.camera_blend.w = probe.blend;
    if (output.generation != probe.generation ||
        std::memcmp(&output.push, &push, sizeof(push)) != 0) {
        render::MaterialDescriptorWriter(output.descriptors->set())
            .combined_image_sampler(0U, probe.current->sampler().handle(), probe.current->view())
            .storage_buffer(1U, output.coefficients->handle(), output.coefficients->size())
            .combined_image_sampler(2U, probe.previous->sampler().handle(), probe.previous->view())
            .update(device);
        const VkMemoryBarrier ready{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                    .pNext = nullptr,
                                    .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
        recorder.pipeline_barrier(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0U, {&ready, 1U}, {}, {});
        recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, s.irradiance->pipeline());
        recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, s.irradiance->layout(), 0U,
                                     output.descriptors->set());
        recorder.push_constants(s.irradiance->layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0U, push);
        recorder.dispatch(1U, 1U, 1U);
        const VkBufferMemoryBarrier ready_coeff{.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                                .pNext = nullptr,
                                                .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                                                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                .buffer = output.coefficients->handle(),
                                                .offset = 0U,
                                                .size = output.coefficients->size()};
        recorder.pipeline_barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0U, {}, {&ready_coeff, 1U},
                                  {});
        output.push = push;
        output.generation = probe.generation;
    }
    TerrainSurfaceWeatherFrame result;
    result.background = &s.background;
    result.elapsed_s = s.elapsed;
    result.time_hours = s.atmosphere.environment.time_of_day.time_hours;
    result.sky_generation = probe.generation;
    if (s.reported_sky != probe.generation) {
        std::printf("terrain_surface_environment: wall_s=%.6f time_hours=%.6f sun_elevation=%.6f "
                    "sky_generation=%llu reflection_blend=%.6f clouds=0 "
                    "primary=%s; render_only=1\n",
                    s.elapsed, double(s.atmosphere.environment.time_of_day.time_hours),
                    double(s.atmosphere.environment.sun_elevation_degrees),
                    static_cast<unsigned long long>(probe.generation),
                    double(s.runtime.pbr_environment_bindings(fallback, false).prefiltered_blend),
                    push.primary_source.w > 0.5F ? "moon" : "sun");
        s.reported_sky = probe.generation;
    }
    result.environment = {s.runtime.pbr_environment_bindings(fallback, false),
                          s.runtime.lighting(),
                          s.atmosphere.resolved_exposure,
                          s.runtime.reflection_probe().sky_radiance_cube().sampler().handle(),
                          s.runtime.reflection_probe().sky_radiance_cube().view(),
                          atmosphere,
                          output.coefficients->handle(),
                          output.coefficients->size(),
                          true};
    return result;
}
} // namespace cubey
