#pragma once

#include <cubey/render/render_graph_compiled.h>

namespace cubey::vulkan {
class CommandRecorder;
} // namespace cubey::vulkan

namespace cubey::render {

void record_render_graph_barriers(const cubey::vulkan::CommandRecorder& recorder,
                                  const RenderGraphExecutionContext& context,
                                  RenderGraphBarrierPhase phase);

} // namespace cubey::render
