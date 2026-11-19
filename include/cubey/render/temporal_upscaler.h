#pragma once

#include <cubey/render/temporal_aa.h>

#include <cstdint>

namespace cubey::render {

// Spatial control at the nominal DLSS Quality scale, with nearest-integer dimensions.
[[nodiscard]] constexpr VkExtent2D quality_spatial_render_extent(VkExtent2D display) noexcept {
    const auto scaled = [](std::uint32_t pixels) {
        return static_cast<std::uint32_t>((std::uint64_t{pixels} * 2U + 1U) / 3U);
    };
    return {scaled(display.width), scaled(display.height)};
}

struct TemporalUpscaleInputs {
    RenderGraphTextureHandle color;
    RenderGraphTextureHandle depth;
    RenderGraphTextureHandle motion;
    TemporalFrame view;
    VkExtent2D output_extent;
    float frame_time_ms = 0.0F;
};

// Adds GPU work before display post. The producer supplies scene-linear color,
// forward Z and motion in unjittered render pixels. The backend returns a
// scene-linear display-size texture and consumes the candidate's reset reason.
// The target owner keeps the backend alive until its last submission retires.
class TemporalUpscaler {
  public:
    virtual ~TemporalUpscaler() = default;
    [[nodiscard]] virtual RenderGraphTextureHandle
    declare(RenderGraphBuilder& graph, const TemporalUpscaleInputs& inputs) = 0;
};

} // namespace cubey::render
