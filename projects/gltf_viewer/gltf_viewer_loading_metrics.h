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
    double document_parse_milliseconds = 0.0;
    double buffer_load_milliseconds = 0.0;
    double asset_validate_milliseconds = 0.0;
    double image_payload_milliseconds = 0.0;
    double image_decode_milliseconds = 0.0;
    double asset_assembly_milliseconds = 0.0;
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
    // One generation-scoped graphics-queue upload session covers textures,
    // static mesh buffers, and deformation buffers. Owner advances can be
    // allocation-only; steps/submissions contain physical copy work.
    std::uint64_t gpu_upload_byte_count = 0;
    std::uint32_t gpu_upload_copy_count = 0;
    std::uint32_t gpu_upload_owner_advance_count = 0;
    std::uint32_t gpu_upload_step_count = 0;
    std::uint32_t gpu_upload_submission_count = 0;
    // Historical metric name retained for profile compatibility: this is the
    // total owner-advance CPU cost, not only vkQueueSubmit call time.
    double gpu_upload_owner_submit_milliseconds = 0.0;
    double gpu_upload_owner_max_step_milliseconds = 0.0;
    double gpu_upload_owner_target_milliseconds = 0.0;
    std::uint32_t gpu_upload_owner_over_target_step_count = 0;
    double gpu_upload_completion_latency_milliseconds = 0.0;
    std::uint64_t gpu_upload_pool_initial_capacity_byte_count = 0;
    std::uint64_t gpu_upload_pool_final_capacity_byte_count = 0;
    std::uint64_t gpu_upload_pool_peak_capacity_byte_count = 0;
    // Reserved staging capacity sampled at the final submission, before
    // final-ticket completion makes the leases reclaimable.
    std::uint64_t gpu_upload_pool_reserved_at_final_submission_byte_count = 0;
    std::uint32_t gpu_upload_pool_growth_count = 0;
    std::uint32_t gpu_upload_backpressure_count = 0;
    double gpu_upload_first_step_to_final_completion_milliseconds = 0.0;
    // Application-frame correlation markers, not GPU timestamp samples. The
    // submission marker is the first frame that observed the owner enqueue or
    // AwaitingGpu state; completion is the activation frame. Zero is used by
    // deterministic headless startup.
    std::uint64_t gpu_upload_submission_frame = 0;
    std::uint64_t gpu_upload_completion_frame = 0;
    // Appended to keep aggregate initialization source-compatible.
    std::uint64_t gpu_upload_step_byte_cap = 0;
    std::uint64_t gpu_upload_copy_byte_target = 0;
};

[[nodiscard]] GltfViewerLoadingMetrics
gltf_viewer_loading_metrics_for_probe(const std::filesystem::path& source_path,
                                      double metadata_probe_milliseconds);
void collect_gltf_viewer_asset_loading_metrics(GltfViewerLoadingMetrics& metrics,
                                               const cubey::asset::GltfAsset& asset);
void collect_gltf_viewer_asset_load_phase_metrics(
    GltfViewerLoadingMetrics& metrics, const cubey::asset::GltfAssetLoadProfile& profile);
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
