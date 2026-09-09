#pragma once

#include <cubey/asset/gltf_asset.h>
#include <cubey/core/profiling.h>
#include <cubey/engine/gltf_scene_importer.h>

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace cubey::projects::gltf_viewer {

inline constexpr std::string_view kGltfViewerLoadingMetricCategory = "gltf_loading";

// One imported generation's glTF-specific load evidence. Timings are CPU wall
// clock durations; the two StagedResource envelope values include queueing
// inside their respective worker/GPU-owner stages.
struct GltfViewerLoadingMetrics {
    std::uint64_t generation_id = 0;
    std::uint64_t source_file_byte_count = 0;
    double metadata_probe_milliseconds = 0.0;
    double gltf_asset_load_milliseconds = 0.0;
    double gltf_scene_prepare_milliseconds = 0.0;
    double staged_worker_prepare_milliseconds = 0.0;
    double gltf_scene_residency_milliseconds = 0.0;
    double staged_gpu_install_milliseconds = 0.0;
    double activation_milliseconds = 0.0;
    std::uint32_t triangle_count = 0;
    std::uint64_t node_count = 0;
    std::uint64_t material_count = 0;
    std::uint64_t texture_count = 0;
    std::uint64_t prepared_texture_count = 0;
    // GltfAsset retains encoded bytes for BasisU KTX2 images. Ordinary source
    // image encodings are decoded and represented by decoded_rgba_byte_count.
    std::uint64_t basisu_encoded_image_byte_count = 0;
    std::uint64_t decoded_rgba_byte_count = 0;
    // CPU-prepared bytes intended for texture upload; this is not a completed
    // GPU transfer-byte measurement.
    std::uint64_t prepared_texture_upload_byte_count = 0;
    std::uint64_t mesh_upload_byte_count = 0;
    std::uint32_t mesh_upload_transfer_submission_count = 0;
};

[[nodiscard]] GltfViewerLoadingMetrics
gltf_viewer_loading_metrics_for_probe(const std::filesystem::path& source_path,
                                      double metadata_probe_milliseconds);
void collect_gltf_viewer_asset_loading_metrics(GltfViewerLoadingMetrics& metrics,
                                               const cubey::asset::GltfAsset& asset);
void collect_gltf_viewer_prepared_loading_metrics(GltfViewerLoadingMetrics& metrics,
                                                  const cubey::GltfPreparedScene& prepared);
void collect_gltf_viewer_resident_loading_metrics(GltfViewerLoadingMetrics& metrics,
                                                  const cubey::GltfSceneResident& resident);

// Emits every successfully activated generation awaiting the first recordable
// frame. A missing recorder or a warmup frame intentionally retains the queue.
[[nodiscard]] bool
emit_gltf_viewer_pending_loading_metrics(cubey::profiling::ProfileRecorder* recorder,
                                         std::uint64_t frame_index,
                                         std::vector<GltfViewerLoadingMetrics>& pending);

} // namespace cubey::projects::gltf_viewer
