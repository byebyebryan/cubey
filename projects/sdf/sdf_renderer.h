#pragma once
#include "sdf_config.h"
#include "sdf_field.h"

#include <chrono>
#include <cubey/core/profiling.h>
#include <cubey/engine/atmosphere_environment_runtime.h>
#include <cubey/render/hdr_post_frame.h>
#include <cubey/render/render_graph_frame.h>
#include <cubey/render/uniform_buffer.h>
#include <cubey/vulkan/gpu_timestamps.h>
#include <span>

namespace cubey::projects::sdf {
struct LightingUniforms {
    render::AtmosphereEnvironmentFrameUniforms atmosphere;
    math::Vec4 direct_color;
    std::array<math::Vec4, 9> irradiance;
    math::Vec4 material;
    math::Vec4 quality; // roughness, softness, AO, ground height
    GpuEdit cutter;
    math::Vec4 cutter_style;      // opacity, world line width, unused, unused
    math::Vec4 preview_style;     // opacity, primitive count, unused, unused
    math::Vec4 surface_options;   // procedural, bump, single-material shortcut, unused
    math::Vec4 transport_options; // glass crossing budget, reserved
};
static_assert(offsetof(LightingUniforms, cutter) % 16 == 0);
static_assert(offsetof(LightingUniforms, cutter_style) == offsetof(LightingUniforms, cutter) + 48);
class Renderer {
  public:
    void create(vulkan::Device& device, vulkan::GpuRuntime& gpu, VkExtent2D extent, VkFormat format,
                std::uint32_t slots, const Field& field, const SdfConfig& config,
                const AuthoredScene* scene = nullptr);
    void destroy();
    void update_field(vulkan::Device& device, const Field& field);
    void update_authored(const AuthoredScene* scene);
    void set_environment_elapsed_seconds(double seconds) {
        environment_seconds_ = seconds;
    }
    void record(vulkan::Device& device, VkCommandBuffer command, render::ColorTargetView target,
                render::FrameSlot slot, bool present, const SdfConfig& config, float yaw,
                float pitch, float distance, profiling::ProfileRecorder* profile,
                std::uint64_t frame_index, const BoxEdit* cutter = nullptr,
                float cutter_opacity = 0, std::span<const SceneInstruction> previews = {});

  private:
    void create_environment(vulkan::Device& device, vulkan::GpuRuntime& gpu,
                            const SdfConfig& config, std::uint32_t slots);
    void prepare_environment(vulkan::Device& device, VkCommandBuffer command,
                             render::FrameSlot slot, const SdfConfig& config, math::Vec3 eye,
                             math::Vec3 target);
    void bind_environment(const vulkan::Device& device, render::FrameSlot slot);
    void create_clouds(vulkan::Device& device, vulkan::GpuRuntime& gpu, const SdfConfig& config,
                       std::uint32_t slots);
    [[nodiscard]] CloudEnvironmentRuntimeFrame cloud_frame(math::Vec3 eye, math::Vec3 target) const;
    AtmosphereEnvironmentRuntime atmosphere_;
    std::optional<render::AtmosphereBackgroundPlaceholderTextures> atmosphere_textures_;
    render::AtmosphereBackgroundFrame sky_background_;
    bool material_atmosphere_ = false;
    bool clouds_enabled_ = false;
    bool cloud_sky_only_ = false;
    std::uint64_t cloud_sky_generation_ = 0;
    render::CloudLayerRuntimeShaderFiles cloud_shaders_;
    std::chrono::steady_clock::time_point environment_clock_{};
    std::optional<double> environment_seconds_, previous_environment_seconds_;
    std::optional<render::GeneratedPbrEnvironment> study_environment_;
    std::unique_ptr<vulkan::Buffer> scene_optics_;
    std::optional<render::FrameUniformBuffer<LightingUniforms>> lighting_;
    std::unique_ptr<vulkan::DescriptorSetArray> descriptors_;
    std::unique_ptr<vulkan::Buffer> scene_program_, scene_materials_, scene_patterns_,
        scene_domains_, scene_libraries_;
    std::vector<std::unique_ptr<vulkan::Buffer>> preview_buffers_;
    std::optional<render::GraphicsPipelineResource> pipeline_, occluder_pipeline_;
    render::HdrPostFrame post_;
    render::RenderGraphFrameExecutor executor_;
    std::optional<vulkan::GpuTimestampProfiler> profiler_;
    std::optional<vulkan::GpuTimestampProfiler> whole_profiler_;
    std::vector<std::optional<std::uint64_t>> recorded_frames_;
    const Field* field_ = nullptr;
    VkExtent2D scene_extent_{};
};
} // namespace cubey::projects::sdf
