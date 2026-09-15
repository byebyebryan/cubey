#pragma once

#include "fluid_25d_config.h"
#include "fluid_25d_scenarios.h"

#include <cubey/engine/project_gpu_services.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_timestamps.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {

inline constexpr std::uint32_t kFluid25DGpuProfilerPassCapacity = 1U;

// std430-compatible project-local cells. Flux faces are left, right, down,
// up. The third velocity component is the wet-state bit (0 or 1).
struct Fluid25DFluxGpu {
    std::array<float, 4> faces_m3_per_s{};
};
struct Fluid25DVelocityGpu {
    std::array<float, 4> velocity_wet{};
};
struct Fluid25DLedgerGpu {
    std::array<float, 2> source_sink_m3{};
};

static_assert(sizeof(Fluid25DFluxGpu) == sizeof(float) * 4U);
static_assert(sizeof(Fluid25DVelocityGpu) == sizeof(float) * 4U);
static_assert(sizeof(Fluid25DLedgerGpu) == sizeof(float) * 2U);

class Fluid25DGpuResources {
  public:
    void create_global_resources_if_needed(cubey::vulkan::Device& device,
                                           cubey::ProjectGpuServices& gpu,
                                           const Fluid25DConfig& config,
                                           const Fluid25DScenarioData& scenario,
                                           std::uint32_t frame_slot_count);
    void create_render_pipelines(cubey::vulkan::Device& device, VkFormat color_format,
                                 VkFormat depth_format, VkExtent2D extent);
    void destroy_swapchain_resources();
    void destroy_all_resources();

    [[nodiscard]] const cubey::vulkan::Buffer& terrain() const;
    [[nodiscard]] const cubey::vulkan::Buffer& source_rate() const;
    [[nodiscard]] const cubey::vulkan::Buffer& sink_rate() const;
    [[nodiscard]] const cubey::vulkan::Buffer& initial_depth() const;
    [[nodiscard]] const cubey::vulkan::Buffer& depth_a() const;
    [[nodiscard]] const cubey::vulkan::Buffer& depth_b() const;
    [[nodiscard]] const cubey::vulkan::Buffer& flux() const;
    [[nodiscard]] const cubey::vulkan::Buffer& velocity() const;
    [[nodiscard]] const cubey::vulkan::Buffer& ledger() const;
    [[nodiscard]] cubey::vulkan::GpuTimestampProfiler* profiler() noexcept {
        return profiler_.has_value() ? &profiler_.value() : nullptr;
    }
    [[nodiscard]] const std::vector<cubey::vulkan::GpuPassTiming>& latest_timings() const;

    [[nodiscard]] const cubey::render::ComputePipelineResource& reset_pipeline() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& flux_pipeline() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& depth_pipeline() const;
    [[nodiscard]] const cubey::render::GraphicsPipelineResource& diagnostic_pipeline() const;
    [[nodiscard]] const cubey::render::GraphicsPipelineResource& terrain_pipeline() const;
    [[nodiscard]] const cubey::render::GraphicsPipelineResource& water_pipeline() const;

    [[nodiscard]] VkDescriptorSet reset_descriptor_set() const noexcept {
        return reset_descriptors_.has_value() ? reset_descriptors_->set() : VK_NULL_HANDLE;
    }
    [[nodiscard]] VkDescriptorSet flux_descriptor_set(bool source_is_a) const noexcept {
        return source_is_a ? flux_a_descriptors_.value().set() : flux_b_descriptors_.value().set();
    }
    [[nodiscard]] VkDescriptorSet depth_descriptor_set(bool source_is_a) const noexcept {
        return source_is_a ? depth_a_to_b_descriptors_.value().set()
                           : depth_b_to_a_descriptors_.value().set();
    }
    [[nodiscard]] VkDescriptorSet render_descriptor_set() const noexcept {
        return current_depth_is_a_ ? render_a_descriptors_.value().set()
                                   : render_b_descriptors_.value().set();
    }

    [[nodiscard]] bool current_depth_is_a() const noexcept {
        return current_depth_is_a_;
    }
    void reset_depth_parity() noexcept {
        current_depth_is_a_ = true;
    }
    void advance_depth_parity() noexcept {
        current_depth_is_a_ = !current_depth_is_a_;
    }

  private:
    void create_buffers(cubey::ProjectGpuServices& gpu, const Fluid25DConfig& config,
                        const Fluid25DScenarioData& scenario);
    void create_descriptors(cubey::vulkan::Device& device);
    void create_compute_pipelines(cubey::vulkan::Device& device);

    std::optional<cubey::vulkan::Buffer> terrain_;
    std::optional<cubey::vulkan::Buffer> source_rate_;
    std::optional<cubey::vulkan::Buffer> sink_rate_;
    std::optional<cubey::vulkan::Buffer> initial_depth_;
    std::optional<cubey::vulkan::Buffer> depth_a_;
    std::optional<cubey::vulkan::Buffer> depth_b_;
    std::optional<cubey::vulkan::Buffer> flux_;
    std::optional<cubey::vulkan::Buffer> velocity_;
    std::optional<cubey::vulkan::Buffer> ledger_;
    std::optional<cubey::vulkan::GpuTimestampProfiler> profiler_;

    std::optional<cubey::vulkan::DescriptorSetBundle> reset_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> flux_a_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> flux_b_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> depth_a_to_b_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> depth_b_to_a_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> render_a_descriptors_;
    std::optional<cubey::vulkan::DescriptorSetBundle> render_b_descriptors_;

    std::optional<cubey::render::ComputePipelineResource> reset_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> flux_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> depth_pipeline_;
    std::optional<cubey::render::GraphicsPipelineResource> diagnostic_pipeline_;
    std::optional<cubey::render::GraphicsPipelineResource> terrain_pipeline_;
    std::optional<cubey::render::GraphicsPipelineResource> water_pipeline_;
    bool current_depth_is_a_ = true;
};

} // namespace cubey::projects::fluid::fluid_25d
