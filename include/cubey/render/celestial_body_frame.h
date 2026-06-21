#pragma once

#include <cubey/core/math.h>
#include <cubey/render/celestial_system.h>
#include <cubey/render/frame_data.h>
#include <cubey/render/material.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/target.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <span>

namespace cubey::render {

struct CelestialBodyFrameUniforms {
    cubey::math::Mat4 view_projection{1.0F};
    cubey::math::Vec4 center_radius{0.0F, 0.0F, 0.0F, 0.0F};
    cubey::math::Vec4 camera_position_options{0.0F, 0.0F, 0.0F, 0.35F};
    cubey::math::Vec4 light_direction_intensity{0.0F, 1.0F, 0.0F, 1.0F};
    cubey::math::Vec4 color_phase{0.58F, 0.62F, 0.74F, 0.5F};
    cubey::math::Vec4 visibility_atmosphere{1.0F, 0.0F, 0.0F, 0.0F};
};

static_assert(sizeof(CelestialBodyFrameUniforms) == sizeof(float) * 36U);

struct CelestialBodyAtmosphereInputs {
    cubey::math::Vec3 camera_position_m{0.0F, 0.0F, 0.0F};
    float planet_radius_m = 0.0F;
    float atmosphere_outer_radius_m = 0.0F;
};

struct CelestialBodyFrameInputs {
    cubey::math::Vec3 camera_render_position_m{0.0F, 0.0F, 0.0F};
    CelestialBodyAtmosphereInputs atmosphere{};
};

struct CelestialBodyFrameMaterialConfig {
    std::uint32_t frame_slot_count = 1;
};

struct CelestialBodyFramePipelineConfig {
    VkExtent2D extent{};
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    std::span<const ShaderStageFile> shader_stage_files{};
};

[[nodiscard]] CelestialBodyFrameUniforms celestial_body_frame_uniforms(
    const CelestialBody& body, const CelestialBodyRenderPlacement& placement,
    const CelestialLighting& lighting, const cubey::math::Mat4& view_projection,
    const CelestialBodyFrameInputs& inputs = {});
[[nodiscard]] MaterialPassInfo celestial_body_pass_info();

class CelestialBodyFrame {
  public:
    CelestialBodyFrame() = default;

    CelestialBodyFrame(const CelestialBodyFrame&) = delete;
    CelestialBodyFrame& operator=(const CelestialBodyFrame&) = delete;
    CelestialBodyFrame(CelestialBodyFrame&&) = delete;
    CelestialBodyFrame& operator=(CelestialBodyFrame&&) = delete;

    void create_materials(const cubey::vulkan::Device& device,
                          const CelestialBodyFrameMaterialConfig& config);
    void create_pipeline(const cubey::vulkan::Device& device,
                         const CelestialBodyFramePipelineConfig& config);
    void destroy_pipeline();
    void destroy();

    void upload(FrameSlot frame_slot, const CelestialBodyFrameUniforms& uniforms) const;
    void record_pass(const cubey::vulkan::CommandRecorder& recorder,
                     const RenderTargetView& target, FrameSlot frame_slot, const Mesh& mesh) const;

    [[nodiscard]] bool materials_created() const noexcept;
    [[nodiscard]] const FrameUniformMaterialInstance<CelestialBodyFrameUniforms>& material() const;
    [[nodiscard]] const GraphicsPipelineResource& pipeline() const;

  private:
    std::optional<FrameUniformMaterialInstance<CelestialBodyFrameUniforms>> material_;
    std::optional<GraphicsPipelineResource> pipeline_;
};

} // namespace cubey::render

