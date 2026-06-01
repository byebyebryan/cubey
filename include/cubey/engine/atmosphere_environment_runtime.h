#pragma once

#include <cubey/render/atmosphere_background_frame.h>
#include <cubey/render/atmosphere_environment.h>
#include <cubey/render/atmosphere_reflection_probe.h>
#include <cubey/render/generated_ibl.h>
#include <cubey/scene/view_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <filesystem>

namespace cubey {

struct AtmosphereEnvironmentRuntimeResourceConfig {
    std::uint32_t reflection_extent = 64;
    std::uint32_t reflection_mip_levels = 5;
    VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
    std::uint32_t frame_slot_count = 1;
    render::AtmosphereBackgroundTextureBindings atmosphere_textures{};
};

struct AtmosphereEnvironmentRuntimePipelineConfig {
    std::filesystem::path atmosphere_vertex_shader{};
    std::filesystem::path atmosphere_fragment_shader{};
    std::filesystem::path reflection_prefilter_vertex_shader{};
    std::filesystem::path reflection_prefilter_fragment_shader{};
};

struct AtmosphereEnvironmentRuntimeFrameInfo {
    render::ViewRayBasis3D view_rays{};
    render::AtmosphereEnvironmentRenderView render_view =
        render::AtmosphereEnvironmentRenderView::Final;
};

struct AtmosphereEnvironmentRuntimeFrame {
    render::AtmosphereEnvironmentFrameUniforms background{};
    cubey::scene::Environment3D scene_environment{};
    render::AtmosphereEnvironmentLighting lighting{};
};

class AtmosphereEnvironmentRuntime {
  public:
    AtmosphereEnvironmentRuntime();

    AtmosphereEnvironmentRuntime(const AtmosphereEnvironmentRuntime&) = delete;
    AtmosphereEnvironmentRuntime& operator=(const AtmosphereEnvironmentRuntime&) = delete;
    AtmosphereEnvironmentRuntime(AtmosphereEnvironmentRuntime&&) = delete;
    AtmosphereEnvironmentRuntime& operator=(AtmosphereEnvironmentRuntime&&) = delete;

    void create_resources(const cubey::vulkan::Device& device,
                          const AtmosphereEnvironmentRuntimeResourceConfig& config);
    void create_pipelines(const cubey::vulkan::Device& device,
                          const AtmosphereEnvironmentRuntimePipelineConfig& config);
    void destroy();

    void set_environment(const render::AtmosphereEnvironmentConfig& environment);
    void mark_full_update_pending();
    void record_pending_update(const cubey::vulkan::CommandRecorder& recorder,
                               render::FrameSlot frame_slot);
    void update_atmosphere_texture_bindings(
        const cubey::vulkan::Device& device,
        const render::AtmosphereBackgroundTextureBindings& textures) const;

    [[nodiscard]] bool resources_created() const noexcept;
    [[nodiscard]] const render::AtmosphereEnvironmentConfig& environment() const noexcept;
    [[nodiscard]] const render::AtmosphereEnvironmentLighting& lighting() const noexcept;
    [[nodiscard]] cubey::scene::Environment3D scene_environment() const;
    [[nodiscard]] AtmosphereEnvironmentRuntimeFrame
    frame(const AtmosphereEnvironmentRuntimeFrameInfo& info) const;
    [[nodiscard]] render::PbrEnvironmentTextureBindings
    pbr_environment_bindings(const render::GeneratedPbrEnvironment& fallback) const;
    [[nodiscard]] const render::AtmosphereReflectionProbe& reflection_probe() const;

  private:
    void refresh_lighting();

    render::AtmosphereEnvironmentConfig environment_{};
    render::AtmosphereEnvironmentLighting lighting_{};
    render::AtmosphereReflectionProbe reflection_probe_{};
    bool full_update_pending_ = true;
    std::uint32_t face_cursor_ = 0;
    std::uint32_t pending_face_updates_ = 0;
};

} // namespace cubey
