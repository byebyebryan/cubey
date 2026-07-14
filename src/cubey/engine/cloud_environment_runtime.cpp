#include <cubey/engine/cloud_environment_runtime.h>

namespace cubey {

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
}

void CloudEnvironmentRuntime::advance(double delta_seconds) {
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
