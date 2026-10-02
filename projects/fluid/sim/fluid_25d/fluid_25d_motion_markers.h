#pragma once

#include "fluid_25d_config.h"

#include <cubey/core/math.h>
#include <cubey/engine/project_gpu_services.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/gpu_timestamps.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {

inline constexpr std::uint32_t kFluid25DMotionMarkerCount = 512U;
inline constexpr std::uint32_t kFluid25DMotionMarkerTrailPoints = 6U;
inline constexpr std::uint32_t kFluid25DMotionMarkerVertices = 42U;

enum class Fluid25DMotionMarkerMode : std::uint32_t {
    Source = 0U,
    Local = 1U
};

// Interpolate accepted positions independently of render FPS. Inspection
// advance shows the completed state until the next regular fixed update.
class Fluid25DMotionMarkerDisplayClock {
  public:
    void update(double accumulator_seconds, float fixed_delta_seconds, std::uint32_t fixed_steps,
                bool advancing, bool paused) {
        if (!std::isfinite(accumulator_seconds) || accumulator_seconds < 0.0 ||
            !std::isfinite(fixed_delta_seconds) || fixed_delta_seconds <= 0.0F)
            throw std::runtime_error("invalid motion marker display clock");
        if (advancing) {
            fraction_ = 1.0F;
            hold_until_step_ = true;
        } else if (!paused) {
            if (fixed_steps > 0U)
                hold_until_step_ = false;
            if (!hold_until_step_)
                fraction_ = std::clamp(
                    static_cast<float>(accumulator_seconds / fixed_delta_seconds), 0.0F, 1.0F);
        }
    }
    void reset() noexcept {
        fraction_ = 1.0F;
        hold_until_step_ = true;
    }
    [[nodiscard]] float fraction() const noexcept {
        return fraction_;
    }

  private:
    float fraction_ = 1.0F;
    bool hold_until_step_ = true;
};

// Presentation state only. No shader with a writable hydraulic binding sees
// this buffer. Positions and trail history are in cell coordinates; the clock
// advances once per accepted outer solver step, independently of render FPS.
struct Fluid25DMotionMarkerGpu {
    std::array<float, 4> position_age_active{};
    std::array<float, 4> previous_xy_reserved{};
    std::array<std::array<float, 4>, kFluid25DMotionMarkerTrailPoints> history{};
};
static_assert(sizeof(Fluid25DMotionMarkerGpu) == 128U);

struct Fluid25DMotionMarkerFields {
    const cubey::vulkan::Buffer* terrain = nullptr;
    const cubey::vulkan::Buffer* depth_a = nullptr;
    const cubey::vulkan::Buffer* depth_b = nullptr;
    const cubey::vulkan::Buffer* velocity = nullptr;
    const cubey::vulkan::Buffer* source_rate = nullptr;
    const cubey::vulkan::Buffer* status = nullptr;
};

class Fluid25DMotionMarkers {
  public:
    void create(cubey::vulkan::Device& device, cubey::ProjectGpuServices& gpu,
                const Fluid25DConfig& config, Fluid25DMotionMarkerFields fields,
                std::array<float, 2> source_xy, std::uint32_t frame_slots = 1U,
                bool profile_enabled = false,
                Fluid25DMotionMarkerMode mode = Fluid25DMotionMarkerMode::Source);
    void create_render_pipeline(cubey::vulkan::Device& device, VkFormat color_format,
                                VkFormat depth_format, VkExtent2D extent);
    void destroy_render_pipeline();
    void destroy();
    void record_reset(VkCommandBuffer command_buffer);
    void begin_frame(VkCommandBuffer command_buffer, std::uint32_t slot, std::uint64_t frame);
    [[nodiscard]] cubey::vulkan::GpuTimestampProfiler* profiler() noexcept {
        return profiler_ ? &*profiler_ : nullptr;
    }
    [[nodiscard]] std::uint64_t profile_frame_index(std::uint32_t slot) const {
        return profile_frames_.at(slot);
    }
    [[nodiscard]] std::uint32_t profile_slot_count() const noexcept {
        return static_cast<std::uint32_t>(profile_frames_.size());
    }
    void record_step(VkCommandBuffer command_buffer, bool depth_is_a);
    void record_draw(VkCommandBuffer command_buffer, bool depth_is_a,
                     const cubey::math::Mat4& view_projection, VkExtent2D extent,
                     float height_scale, float interpolation);
    [[nodiscard]] const cubey::vulkan::Buffer& buffer() const;
    [[nodiscard]] std::uint32_t completed_steps() const noexcept {
        return completed_steps_;
    }
    [[nodiscard]] bool created() const noexcept {
        return markers_.has_value();
    }

  private:
    void dispatch(VkCommandBuffer command_buffer, bool depth_is_a, bool reset);
    Fluid25DConfig config_{};
    std::array<float, 2> source_xy_{};
    Fluid25DMotionMarkerMode mode_ = Fluid25DMotionMarkerMode::Source;
    std::uint32_t completed_steps_ = 0U;
    std::optional<cubey::vulkan::Buffer> markers_;
    std::optional<cubey::vulkan::DescriptorSetBundle> update_a_;
    std::optional<cubey::vulkan::DescriptorSetBundle> update_b_;
    std::optional<cubey::vulkan::DescriptorSetBundle> render_a_;
    std::optional<cubey::vulkan::DescriptorSetBundle> render_b_;
    std::optional<cubey::render::ComputePipelineResource> update_pipeline_;
    std::optional<cubey::render::GraphicsPipelineResource> render_pipeline_;
    std::optional<cubey::vulkan::GpuTimestampProfiler> profiler_;
    std::vector<std::uint64_t> profile_frames_;
    std::uint32_t current_slot_ = 0U;
};

// Uses synthetic independent fields and the actual compute shader. It never
// substitutes these fields for the application's terrain or hydraulic state.
void validate_fluid_25d_motion_marker_gpu_controls(cubey::vulkan::Device& device,
                                                   cubey::ProjectGpuServices& gpu);

} // namespace cubey::projects::fluid::fluid_25d
