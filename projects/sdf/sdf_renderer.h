#pragma once
#include "sdf_config.h"
#include "sdf_field.h"

#include <cubey/core/profiling.h>
#include <cubey/engine/atmosphere_environment_runtime.h>
#include <cubey/render/hdr_post_frame.h>
#include <cubey/render/render_graph_frame.h>
#include <cubey/render/uniform_buffer.h>
#include <cubey/vulkan/gpu_timestamps.h>

namespace cubey::projects::sdf {
struct LightingUniforms {
    render::AtmosphereEnvironmentFrameUniforms atmosphere;
    math::Vec4 direct_color;
    std::array<math::Vec4, 9> irradiance;
    math::Vec4 material;
    math::Vec4 quality; // roughness, softness, AO, ground height
};
class Renderer {
  public:
    void create(vulkan::Device& device, VkExtent2D extent, VkFormat format, std::uint32_t slots,
                const Field& field, const SdfConfig& config);
    void destroy();
    void update_field(vulkan::Device& device, const Field& field);
    void record(vulkan::Device& device, VkCommandBuffer command, render::ColorTargetView target,
                render::FrameSlot slot, bool present, const SdfConfig& config, float yaw,
                float pitch, float distance, profiling::ProfileRecorder* profile,
                std::uint64_t frame_index);

  private:
    AtmosphereEnvironmentRuntime atmosphere_;
    std::optional<render::FrameUniformBuffer<LightingUniforms>> lighting_;
    std::unique_ptr<vulkan::DescriptorSetArray> descriptors_;
    std::optional<render::GraphicsPipelineResource> pipeline_, occluder_pipeline_;
    render::HdrPostFrame post_;
    render::RenderGraphFrameExecutor executor_;
    std::optional<vulkan::GpuTimestampProfiler> profiler_;
    std::vector<std::optional<std::uint64_t>> recorded_frames_;
    const Field* field_ = nullptr;
    VkExtent2D scene_extent_{};
};
} // namespace cubey::projects::sdf
