#pragma once

#include <cubey/core/profiling.h>
#include <cubey/engine/forward_pbr_renderer_3d.h>

#include <cstdint>
#include <string_view>

namespace cubey::projects::gltf_viewer {

inline constexpr std::string_view kGltfViewerForwardPbrMetricCategory = "forward_pbr";
inline constexpr std::string_view kGltfViewerRenderGraphMetricCategory = "render_graph";

// Emits one renderer snapshot into the Viewer-owned profile recorder. The
// helper is intentionally policy-only: render/engine types provide values,
// while the Viewer chooses stable categories and names.
[[nodiscard]] bool
emit_gltf_viewer_render_metrics(cubey::profiling::ProfileRecorder* recorder,
                                std::uint64_t frame_index,
                                const cubey::ForwardPbrRenderer3DFrameMetrics& metrics);

} // namespace cubey::projects::gltf_viewer
