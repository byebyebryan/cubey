#pragma once

#include <cubey/render/temporal_aa.h>

namespace cubey::render {

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
