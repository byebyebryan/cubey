#include <cubey/engine/atmosphere_environment_runtime.h>

#include <stdexcept>

namespace cubey {
namespace {

bool vec3_equal(math::Vec3 lhs, math::Vec3 rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

bool time_of_day_equal(const render::AtmosphereEnvironmentTimeOfDay& lhs,
                       const render::AtmosphereEnvironmentTimeOfDay& rhs) {
    return lhs.time_hours == rhs.time_hours && lhs.day_of_year == rhs.day_of_year &&
           lhs.latitude_degrees == rhs.latitude_degrees &&
           lhs.azimuth_offset_degrees == rhs.azimuth_offset_degrees;
}

bool night_sky_equal(const render::AtmosphereEnvironmentNightSky& lhs,
                     const render::AtmosphereEnvironmentNightSky& rhs) {
    return lhs.twilight_strength == rhs.twilight_strength &&
           lhs.twilight_horizon_warmth == rhs.twilight_horizon_warmth &&
           lhs.star_intensity == rhs.star_intensity && lhs.star_density == rhs.star_density &&
           lhs.milky_way_intensity == rhs.milky_way_intensity &&
           lhs.milky_way_contrast == rhs.milky_way_contrast &&
           lhs.light_pollution == rhs.light_pollution &&
           lhs.camera_visual_mode == rhs.camera_visual_mode;
}

bool moon_equal(const render::AtmosphereEnvironmentMoon& lhs,
                const render::AtmosphereEnvironmentMoon& rhs) {
    return lhs.enabled == rhs.enabled && lhs.disk_intensity == rhs.disk_intensity &&
           lhs.moonlight_intensity == rhs.moonlight_intensity &&
           lhs.phase_offset_days == rhs.phase_offset_days &&
           lhs.angular_radius_scale == rhs.angular_radius_scale;
}

bool environment_equal(const render::AtmosphereEnvironmentConfig& lhs,
                       const render::AtmosphereEnvironmentConfig& rhs) {
    return time_of_day_equal(lhs.time_of_day, rhs.time_of_day) &&
           night_sky_equal(lhs.night_sky, rhs.night_sky) && moon_equal(lhs.moon, rhs.moon) &&
           lhs.bottom_radius_km == rhs.bottom_radius_km && lhs.top_radius_km == rhs.top_radius_km &&
           vec3_equal(lhs.rayleigh_scattering, rhs.rayleigh_scattering) &&
           lhs.rayleigh_scale_height_km == rhs.rayleigh_scale_height_km &&
           lhs.rayleigh_density_scale == rhs.rayleigh_density_scale &&
           lhs.mie_scattering == rhs.mie_scattering && lhs.mie_extinction == rhs.mie_extinction &&
           lhs.mie_scale_height_km == rhs.mie_scale_height_km &&
           lhs.mie_anisotropy == rhs.mie_anisotropy &&
           lhs.mie_density_scale == rhs.mie_density_scale &&
           vec3_equal(lhs.ozone_absorption, rhs.ozone_absorption) &&
           lhs.ozone_center_altitude_km == rhs.ozone_center_altitude_km &&
           lhs.ozone_half_width_km == rhs.ozone_half_width_km &&
           lhs.ozone_density_scale == rhs.ozone_density_scale &&
           lhs.ground_albedo == rhs.ground_albedo &&
           lhs.sun_angular_radius == rhs.sun_angular_radius &&
           lhs.sun_elevation_degrees == rhs.sun_elevation_degrees &&
           lhs.sun_azimuth_degrees == rhs.sun_azimuth_degrees &&
           lhs.camera_altitude_km == rhs.camera_altitude_km && lhs.ground_mode == rhs.ground_mode &&
           lhs.render_celestial_content == rhs.render_celestial_content &&
           lhs.render_sun_disk == rhs.render_sun_disk &&
           lhs.render_night_sky == rhs.render_night_sky &&
           lhs.render_moon_disk == rhs.render_moon_disk &&
           lhs.reference_geometry_enabled == rhs.reference_geometry_enabled &&
           lhs.reference_grid_km == rhs.reference_grid_km &&
           lhs.reference_intensity == rhs.reference_intensity;
}

cubey::scene::Environment3D
scene_environment_from_lighting(const render::AtmosphereEnvironmentLighting& lighting) {
    return cubey::scene::Environment3D{
        .ambient_color = lighting.ambient_color,
        .ambient_intensity = 0.0F,
        .diffuse_irradiance_sh = lighting.diffuse_irradiance_sh,
        .diffuse_irradiance_sh_enabled = true,
    };
}

} // namespace

AtmosphereEnvironmentRuntime::AtmosphereEnvironmentRuntime() {
    refresh_lighting();
}

void AtmosphereEnvironmentRuntime::create_resources(
    const cubey::vulkan::Device& device, const AtmosphereEnvironmentRuntimeResourceConfig& config) {
    if (config.frame_slot_count == 0) {
        throw std::runtime_error("atmosphere environment runtime requires at least one frame slot");
    }
    if (resources_created()) {
        throw std::runtime_error(
            "atmosphere environment runtime resources are already initialized");
    }

    reflection_probe_.create_resources(device,
                                       render::AtmosphereReflectionProbeConfig{
                                           .extent = config.reflection_extent,
                                           .mip_levels = config.reflection_mip_levels,
                                           .format = config.format,
                                           .frame_slot_count = config.frame_slot_count,
                                           .atmosphere_textures = config.atmosphere_textures,
                                       });
    mark_full_update_pending();
}

void AtmosphereEnvironmentRuntime::create_pipelines(
    const cubey::vulkan::Device& device, const AtmosphereEnvironmentRuntimePipelineConfig& config) {
    reflection_probe_.create_pipelines(
        device, render::AtmosphereReflectionProbePipelineConfig{
                    .atmosphere_vertex_shader = config.atmosphere_vertex_shader,
                    .atmosphere_fragment_shader = config.atmosphere_fragment_shader,
                    .prefilter_vertex_shader = config.reflection_prefilter_vertex_shader,
                    .prefilter_fragment_shader = config.reflection_prefilter_fragment_shader,
                });
}

void AtmosphereEnvironmentRuntime::destroy() {
    reflection_probe_.destroy();
    mark_full_update_pending();
}

bool AtmosphereEnvironmentRuntime::set_environment(
    const render::AtmosphereEnvironmentConfig& environment,
    AtmosphereReflectionProbeUpdateMode update_mode) {
    if (environment_initialized_ && environment_equal(environment_, environment)) {
        return false;
    }

    environment_ = environment;
    environment_initialized_ = true;
    refresh_lighting();
    if (update_mode == AtmosphereReflectionProbeUpdateMode::CoherentFull) {
        mark_full_update_pending();
    } else if (!full_update_pending_) {
        pending_face_updates_ = 6U;
    }
    return true;
}

void AtmosphereEnvironmentRuntime::mark_full_update_pending() {
    full_update_pending_ = true;
    face_cursor_ = 0;
    pending_face_updates_ = 0;
}

void AtmosphereEnvironmentRuntime::record_pending_update(
    const cubey::vulkan::CommandRecorder& recorder, render::FrameSlot frame_slot) {
    if (!resources_created()) {
        throw std::runtime_error("atmosphere environment runtime resources are not initialized");
    }

    const render::AtmosphereReflectionProbeUpdateInfo update{
        .frame_slot = frame_slot,
        .environment = environment_,
    };
    if (full_update_pending_) {
        reflection_probe_.record_full_update(recorder, update);
        full_update_pending_ = false;
        face_cursor_ = 0;
        pending_face_updates_ = 0;
        return;
    }
    if (pending_face_updates_ > 0) {
        reflection_probe_.record_face_update(recorder, update, face_cursor_);
        face_cursor_ = (face_cursor_ + 1U) % 6U;
        --pending_face_updates_;
    }
}

void AtmosphereEnvironmentRuntime::update_atmosphere_texture_bindings(
    const cubey::vulkan::Device& device,
    const render::AtmosphereBackgroundTextureBindings& textures) const {
    if (!resources_created()) {
        throw std::runtime_error("atmosphere environment runtime resources are not initialized");
    }
    reflection_probe_.update_atmosphere_texture_bindings(device, textures);
}

bool AtmosphereEnvironmentRuntime::resources_created() const noexcept {
    return reflection_probe_.resources_created();
}

const render::AtmosphereEnvironmentConfig&
AtmosphereEnvironmentRuntime::environment() const noexcept {
    return environment_;
}

const render::AtmosphereEnvironmentLighting&
AtmosphereEnvironmentRuntime::lighting() const noexcept {
    return lighting_;
}

cubey::scene::Environment3D AtmosphereEnvironmentRuntime::scene_environment() const {
    return scene_environment_from_lighting(lighting_);
}

AtmosphereEnvironmentRuntimeFrame
AtmosphereEnvironmentRuntime::frame(const AtmosphereEnvironmentRuntimeFrameInfo& info) const {
    return {
        .background = render::atmosphere_environment_frame_uniforms(
            environment_,
            render::AtmosphereEnvironmentFrameUniformInputs{
                .view_rays = info.view_rays,
                .render_view = info.render_view,
            }),
        .scene_environment = scene_environment(),
        .lighting = lighting_,
    };
}

AtmosphereEnvironmentRuntimeFrame AtmosphereEnvironmentRuntime::frame_from_celestial(
    const AtmosphereEnvironmentRuntimeCelestialFrameInfo& info) const {
    const render::AtmosphereEnvironmentLighting lighting =
        render::atmosphere_environment_lighting_from_celestial(environment_, info.celestial);
    return {
        .background = render::atmosphere_environment_frame_uniforms_from_celestial(
            environment_, info.celestial,
            render::AtmosphereEnvironmentFrameUniformInputs{
                .view_rays = info.view_rays,
                .render_view = info.render_view,
            }),
        .scene_environment = scene_environment_from_lighting(lighting),
        .lighting = lighting,
    };
}

render::PbrEnvironmentTextureBindings AtmosphereEnvironmentRuntime::pbr_environment_bindings(
    const render::GeneratedPbrEnvironment& fallback) const {
    if (!resources_created()) {
        throw std::runtime_error("atmosphere environment runtime resources are not initialized");
    }

    render::PbrEnvironmentTextureBindings bindings =
        render::pbr_environment_texture_bindings(fallback);
    const render::TextureCube& prefiltered = reflection_probe_.prefiltered_cube();
    bindings.prefiltered_sampler = prefiltered.sampler().handle();
    bindings.prefiltered_view = prefiltered.view();
    bindings.previous_prefiltered_sampler = prefiltered.sampler().handle();
    bindings.previous_prefiltered_view = prefiltered.view();
    bindings.prefiltered_mip_levels = reflection_probe_.mip_levels();
    bindings.prefiltered_blend = 1.0F;
    return bindings;
}

const render::AtmosphereReflectionProbe& AtmosphereEnvironmentRuntime::reflection_probe() const {
    if (!resources_created()) {
        throw std::runtime_error("atmosphere environment runtime resources are not initialized");
    }
    return reflection_probe_;
}

void AtmosphereEnvironmentRuntime::refresh_lighting() {
    lighting_ = render::atmosphere_environment_lighting(environment_);
}

} // namespace cubey
