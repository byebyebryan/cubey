#pragma once

#include <cubey/render/cloud_environment_probe.h>
#include <cubey/render/generated_ibl.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>

namespace cubey {

class CloudEnvironmentRuntime {
  public:
    CloudEnvironmentRuntime() = default;

    CloudEnvironmentRuntime(const CloudEnvironmentRuntime&) = delete;
    CloudEnvironmentRuntime& operator=(const CloudEnvironmentRuntime&) = delete;
    CloudEnvironmentRuntime(CloudEnvironmentRuntime&&) = delete;
    CloudEnvironmentRuntime& operator=(CloudEnvironmentRuntime&&) = delete;

    void create_resources(const cubey::vulkan::Device& device,
                          const render::CloudEnvironmentProbeConfig& config,
                          const render::CloudLayerGeneratedResources& generated,
                          const render::TextureCube& clear_sky);
    void create_pipelines(const cubey::vulkan::Device& device,
                          const render::CloudEnvironmentProbePipelineConfig& config);
    void destroy();

    void advance(double delta_seconds);
    void invalidate();
    [[nodiscard]] bool record_pending_update(const cubey::vulkan::CommandRecorder& recorder,
                                             const render::CloudEnvironmentProbeUpdateInfo& info);

    [[nodiscard]] bool resources_created() const noexcept;
    [[nodiscard]] bool pipelines_created() const noexcept;
    [[nodiscard]] float age_seconds() const noexcept;
    [[nodiscard]] render::CloudEnvironmentProbeSnapshot snapshot() const;
    [[nodiscard]] render::PbrEnvironmentTextureBindings
    pbr_environment_bindings(const render::PbrEnvironmentTextureBindings& fallback) const;

  private:
    render::CloudEnvironmentProbe probe_{};
};

} // namespace cubey
