#include <cubey/engine/atmosphere_environment_runtime.h>

#include <stdexcept>

namespace cubey::render {

AtmosphereEnvironmentRuntime::AtmosphereEnvironmentRuntime() {
    refresh_lighting();
}

void AtmosphereEnvironmentRuntime::create_resources(
    const cubey::vulkan::Device& device,
    const AtmosphereEnvironmentRuntimeResourceConfig& config) {
    if (config.frame_slot_count == 0) {
        throw std::runtime_error("atmosphere environment runtime requires at least one frame slot");
    }
    if (resources_created()) {
        throw std::runtime_error("atmosphere environment runtime resources are already initialized");
    }

    reflection_probe_.create_resources(device, AtmosphereReflectionProbeConfig{
                                                   .extent = config.reflection_extent,
                                                   .mip_levels = config.reflection_mip_levels,
                                                   .irradiance_extent = config.irradiance_extent,
                                                   .format = config.format,
                                                   .frame_slot_count = config.frame_slot_count,
                                                   .atmosphere_textures =
                                                       config.atmosphere_textures,
                                               });
    mark_full_update_pending();
}

void AtmosphereEnvironmentRuntime::create_pipelines(
    const cubey::vulkan::Device& device,
    const AtmosphereEnvironmentRuntimePipelineConfig& config) {
    reflection_probe_.create_pipelines(
        device, AtmosphereReflectionProbePipelineConfig{
                    .atmosphere_vertex_shader = config.atmosphere_vertex_shader,
                    .atmosphere_fragment_shader = config.atmosphere_fragment_shader,
                    .prefilter_vertex_shader = config.reflection_prefilter_vertex_shader,
                    .prefilter_fragment_shader = config.reflection_prefilter_fragment_shader,
                    .irradiance_vertex_shader = config.irradiance_vertex_shader,
                    .irradiance_fragment_shader = config.irradiance_fragment_shader,
                });
}

void AtmosphereEnvironmentRuntime::destroy() {
    reflection_probe_.destroy();
    mark_full_update_pending();
}

void AtmosphereEnvironmentRuntime::set_environment(
    const AtmosphereEnvironmentConfig& environment) {
    environment_ = environment;
    refresh_lighting();
    time_dirty_ = true;
}

void AtmosphereEnvironmentRuntime::mark_full_update_pending() {
    full_update_pending_ = true;
    time_dirty_ = false;
    face_cursor_ = 0;
}

void AtmosphereEnvironmentRuntime::record_pending_update(
    const cubey::vulkan::CommandRecorder& recorder, FrameSlot frame_slot) {
    if (!resources_created()) {
        throw std::runtime_error("atmosphere environment runtime resources are not initialized");
    }

    const AtmosphereReflectionProbeUpdateInfo update{
        .frame_slot = frame_slot,
        .environment = environment_,
    };
    if (full_update_pending_) {
        reflection_probe_.record_full_update(recorder, update);
        full_update_pending_ = false;
        time_dirty_ = false;
        face_cursor_ = 0;
        return;
    }
    if (time_dirty_) {
        reflection_probe_.record_face_update(recorder, update, face_cursor_);
        face_cursor_ = (face_cursor_ + 1U) % 6U;
        time_dirty_ = false;
    }
}

bool AtmosphereEnvironmentRuntime::resources_created() const noexcept {
    return reflection_probe_.resources_created();
}

const AtmosphereEnvironmentConfig& AtmosphereEnvironmentRuntime::environment() const noexcept {
    return environment_;
}

const AtmosphereEnvironmentLighting& AtmosphereEnvironmentRuntime::lighting() const noexcept {
    return lighting_;
}

cubey::scene::Environment3D AtmosphereEnvironmentRuntime::scene_environment() const {
    return cubey::scene::Environment3D{
        .ambient_color = lighting_.ambient_color,
        .ambient_intensity = 0.0F,
        .diffuse_irradiance_sh = lighting_.diffuse_irradiance_sh,
        .diffuse_irradiance_sh_enabled = true,
    };
}

PbrEnvironmentTextureBindings AtmosphereEnvironmentRuntime::pbr_environment_bindings(
    const GeneratedPbrEnvironment& fallback, AtmosphereDiffuseSource diffuse_source) const {
    if (!resources_created()) {
        throw std::runtime_error("atmosphere environment runtime resources are not initialized");
    }

    PbrEnvironmentTextureBindings bindings = pbr_environment_texture_bindings(fallback);
    if (diffuse_source == AtmosphereDiffuseSource::IrradianceCube) {
        const TextureCube& irradiance = reflection_probe_.irradiance_cube();
        bindings.irradiance_sampler = irradiance.sampler().handle();
        bindings.irradiance_view = irradiance.view();
    }
    const TextureCube& prefiltered = reflection_probe_.prefiltered_cube();
    bindings.prefiltered_sampler = prefiltered.sampler().handle();
    bindings.prefiltered_view = prefiltered.view();
    bindings.prefiltered_mip_levels = reflection_probe_.mip_levels();
    return bindings;
}

const AtmosphereReflectionProbe& AtmosphereEnvironmentRuntime::reflection_probe() const {
    if (!resources_created()) {
        throw std::runtime_error("atmosphere environment runtime resources are not initialized");
    }
    return reflection_probe_;
}

void AtmosphereEnvironmentRuntime::refresh_lighting() {
    lighting_ = atmosphere_environment_lighting(environment_);
}

} // namespace cubey::render
