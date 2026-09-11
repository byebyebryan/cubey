#include "gltf_viewer_loading_metrics.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>

namespace cubey::projects::gltf_viewer {
namespace {

void saturating_add(std::uint64_t& total, std::size_t value) noexcept {
    const std::uint64_t addend = static_cast<std::uint64_t>(value);
    if (total > std::numeric_limits<std::uint64_t>::max() - addend) {
        total = std::numeric_limits<std::uint64_t>::max();
        return;
    }
    total += addend;
}

void record_metric(cubey::profiling::ProfileRecorder& recorder, std::uint64_t frame_index,
                   std::string_view name, double value) {
    recorder.record_metric(frame_index, kGltfViewerLoadingMetricCategory, name, value);
}

void record_metrics(cubey::profiling::ProfileRecorder& recorder, std::uint64_t frame_index,
                    const GltfViewerLoadingMetrics& metrics) {
    record_metric(recorder, frame_index, "generation_id",
                  static_cast<double>(metrics.generation_id));
    record_metric(recorder, frame_index, "source_file_bytes",
                  static_cast<double>(metrics.source_file_byte_count));
    record_metric(recorder, frame_index, "metadata_probe_ms", metrics.metadata_probe_milliseconds);
    record_metric(recorder, frame_index, "asset_load_ms", metrics.gltf_asset_load_milliseconds);
    record_metric(recorder, frame_index, "document_parse_ms", metrics.document_parse_milliseconds);
    record_metric(recorder, frame_index, "buffer_load_ms", metrics.buffer_load_milliseconds);
    record_metric(recorder, frame_index, "asset_validate_ms", metrics.asset_validate_milliseconds);
    record_metric(recorder, frame_index, "image_payload_ms", metrics.image_payload_milliseconds);
    record_metric(recorder, frame_index, "image_decode_ms", metrics.image_decode_milliseconds);
    record_metric(recorder, frame_index, "asset_assembly_ms", metrics.asset_assembly_milliseconds);
    record_metric(recorder, frame_index, "scene_prepare_ms",
                  metrics.gltf_scene_prepare_milliseconds);
    record_metric(recorder, frame_index, "staged_worker_prepare_ms",
                  metrics.staged_worker_prepare_milliseconds);
    record_metric(recorder, frame_index, "gltf_residency_ms",
                  metrics.gltf_scene_residency_milliseconds);
    record_metric(recorder, frame_index, "staged_gpu_install_ms",
                  metrics.staged_gpu_install_milliseconds);
    record_metric(recorder, frame_index, "activation_ms", metrics.activation_milliseconds);
    record_metric(recorder, frame_index, "triangle_count",
                  static_cast<double>(metrics.triangle_count));
    record_metric(recorder, frame_index, "node_count", static_cast<double>(metrics.node_count));
    record_metric(recorder, frame_index, "material_count",
                  static_cast<double>(metrics.material_count));
    record_metric(recorder, frame_index, "texture_count",
                  static_cast<double>(metrics.texture_count));
    record_metric(recorder, frame_index, "prepared_texture_count",
                  static_cast<double>(metrics.prepared_texture_count));
    record_metric(recorder, frame_index, "basisu_encoded_image_bytes",
                  static_cast<double>(metrics.basisu_encoded_image_byte_count));
    record_metric(recorder, frame_index, "decoded_rgba_bytes",
                  static_cast<double>(metrics.decoded_rgba_byte_count));
    record_metric(recorder, frame_index, "prepared_texture_upload_bytes",
                  static_cast<double>(metrics.prepared_texture_upload_byte_count));
    record_metric(recorder, frame_index, "mesh_upload_bytes",
                  static_cast<double>(metrics.mesh_upload_byte_count));
    record_metric(recorder, frame_index, "mesh_upload_transfer_submission_count",
                  static_cast<double>(metrics.mesh_upload_transfer_submission_count));
    record_metric(recorder, frame_index, "gpu_upload_bytes",
                  static_cast<double>(metrics.gpu_upload_byte_count));
    record_metric(recorder, frame_index, "gpu_upload_copy_count",
                  static_cast<double>(metrics.gpu_upload_copy_count));
    record_metric(recorder, frame_index, "gpu_upload_owner_advance_count",
                  static_cast<double>(metrics.gpu_upload_owner_advance_count));
    record_metric(recorder, frame_index, "gpu_upload_step_count",
                  static_cast<double>(metrics.gpu_upload_step_count));
    record_metric(recorder, frame_index, "gpu_upload_submission_count",
                  static_cast<double>(metrics.gpu_upload_submission_count));
    record_metric(recorder, frame_index, "gpu_upload_owner_submit_ms",
                  metrics.gpu_upload_owner_submit_milliseconds);
    record_metric(recorder, frame_index, "gpu_upload_owner_max_step_ms",
                  metrics.gpu_upload_owner_max_step_milliseconds);
    record_metric(recorder, frame_index, "gpu_upload_owner_target_ms",
                  metrics.gpu_upload_owner_target_milliseconds);
    record_metric(recorder, frame_index, "gpu_upload_step_byte_cap",
                  static_cast<double>(metrics.gpu_upload_step_byte_cap));
    record_metric(recorder, frame_index, "gpu_upload_copy_byte_target",
                  static_cast<double>(metrics.gpu_upload_copy_byte_target));
    record_metric(recorder, frame_index, "gpu_upload_owner_over_target_step_count",
                  static_cast<double>(metrics.gpu_upload_owner_over_target_step_count));
    record_metric(recorder, frame_index, "gpu_upload_completion_latency_ms",
                  metrics.gpu_upload_completion_latency_milliseconds);
    record_metric(recorder, frame_index, "gpu_upload_pool_initial_capacity_bytes",
                  static_cast<double>(metrics.gpu_upload_pool_initial_capacity_byte_count));
    record_metric(recorder, frame_index, "gpu_upload_pool_final_capacity_bytes",
                  static_cast<double>(metrics.gpu_upload_pool_final_capacity_byte_count));
    record_metric(recorder, frame_index, "gpu_upload_pool_peak_capacity_bytes",
                  static_cast<double>(metrics.gpu_upload_pool_peak_capacity_byte_count));
    record_metric(
        recorder, frame_index, "gpu_upload_pool_reserved_at_final_submission_bytes",
        static_cast<double>(metrics.gpu_upload_pool_reserved_at_final_submission_byte_count));
    record_metric(recorder, frame_index, "gpu_upload_pool_growth_count",
                  static_cast<double>(metrics.gpu_upload_pool_growth_count));
    record_metric(recorder, frame_index, "gpu_upload_backpressure_count",
                  static_cast<double>(metrics.gpu_upload_backpressure_count));
    record_metric(recorder, frame_index, "gpu_upload_first_step_to_final_completion_ms",
                  metrics.gpu_upload_first_step_to_final_completion_milliseconds);
    record_metric(recorder, frame_index, "gpu_upload_submission_frame",
                  static_cast<double>(metrics.gpu_upload_submission_frame));
    record_metric(recorder, frame_index, "gpu_upload_completion_frame",
                  static_cast<double>(metrics.gpu_upload_completion_frame));
}

} // namespace

GltfViewerLoadingMetrics
gltf_viewer_loading_metrics_for_probe(const std::filesystem::path& source_path,
                                      double metadata_probe_milliseconds) {
    std::error_code error;
    const std::uintmax_t source_file_size = std::filesystem::file_size(source_path, error);
    return {
        .source_file_byte_count =
            error ? 0U
                  : std::min<std::uintmax_t>(source_file_size,
                                             std::numeric_limits<std::uint64_t>::max()),
        .metadata_probe_milliseconds = metadata_probe_milliseconds,
    };
}

void collect_gltf_viewer_asset_loading_metrics(GltfViewerLoadingMetrics& metrics,
                                               const cubey::asset::GltfAsset& asset) {
    metrics.node_count = asset.nodes.size();
    metrics.material_count = asset.materials.size();
    metrics.texture_count = asset.textures.size();
    metrics.basisu_encoded_image_byte_count = 0U;
    metrics.decoded_rgba_byte_count = 0U;
    for (const cubey::asset::GltfImage& image : asset.images) {
        saturating_add(metrics.basisu_encoded_image_byte_count, image.encoded_bytes.size());
        saturating_add(metrics.decoded_rgba_byte_count, image.rgba8.size());
    }
}

void collect_gltf_viewer_asset_load_phase_metrics(
    GltfViewerLoadingMetrics& metrics, const cubey::asset::GltfAssetLoadProfile& profile) {
    metrics.document_parse_milliseconds = profile.document_parse_milliseconds;
    metrics.buffer_load_milliseconds = profile.buffer_load_milliseconds;
    metrics.asset_validate_milliseconds = profile.asset_validate_milliseconds;
    metrics.image_payload_milliseconds = profile.image_payload_milliseconds;
    metrics.image_decode_milliseconds = profile.image_decode_milliseconds;
    metrics.asset_assembly_milliseconds = profile.asset_assembly_milliseconds;
}

void collect_gltf_viewer_prepared_loading_metrics(GltfViewerLoadingMetrics& metrics,
                                                  const cubey::GltfPreparedScene& prepared) {
    metrics.triangle_count = prepared.triangle_count;
    metrics.prepared_texture_count = prepared.textures.size();
    metrics.prepared_texture_upload_byte_count = 0U;
    for (const cubey::GltfPreparedTexture& texture : prepared.textures) {
        saturating_add(metrics.prepared_texture_upload_byte_count, texture.bytes.size());
    }
}

void collect_gltf_viewer_resident_loading_metrics(GltfViewerLoadingMetrics& metrics,
                                                  const cubey::GltfSceneResident& resident) {
    metrics.mesh_upload_byte_count = resident.mesh_upload_byte_count;
    metrics.mesh_upload_transfer_submission_count = resident.mesh_upload_transfer_submission_count;
    const cubey::GltfSceneUploadSessionMetrics& upload = resident.upload_session_metrics;
    metrics.gpu_upload_byte_count = upload.uploaded_byte_count;
    metrics.gpu_upload_copy_count = upload.copy_count;
    metrics.gpu_upload_owner_advance_count = upload.owner_advance_count;
    metrics.gpu_upload_step_count = upload.step_count;
    metrics.gpu_upload_submission_count = upload.submission_count;
    metrics.gpu_upload_owner_submit_milliseconds = upload.owner_total_milliseconds;
    metrics.gpu_upload_owner_max_step_milliseconds = upload.owner_max_step_milliseconds;
    metrics.gpu_upload_owner_target_milliseconds = upload.owner_target_milliseconds;
    metrics.gpu_upload_step_byte_cap = upload.step_byte_cap;
    metrics.gpu_upload_copy_byte_target = upload.copy_byte_target;
    metrics.gpu_upload_owner_over_target_step_count = upload.owner_over_target_step_count;
    metrics.gpu_upload_completion_latency_milliseconds =
        resident.final_upload_step.metrics().completion_latency_milliseconds;
    metrics.gpu_upload_pool_initial_capacity_byte_count = upload.pool_initial_capacity_byte_count;
    metrics.gpu_upload_pool_final_capacity_byte_count = upload.pool_final_capacity_byte_count;
    metrics.gpu_upload_pool_peak_capacity_byte_count = upload.pool_peak_capacity_byte_count;
    metrics.gpu_upload_pool_reserved_at_final_submission_byte_count =
        upload.pool_reserved_at_final_submission_byte_count;
    metrics.gpu_upload_pool_growth_count = upload.pool_growth_count;
    metrics.gpu_upload_backpressure_count = upload.backpressure_count;
    metrics.gpu_upload_first_step_to_final_completion_milliseconds =
        upload.first_step_to_final_completion_milliseconds;
}

bool emit_gltf_viewer_pending_loading_metrics(cubey::profiling::ProfileRecorder* recorder,
                                              std::uint64_t frame_index,
                                              std::vector<GltfViewerLoadingMetrics>& pending) {
    if (recorder == nullptr || !recorder->should_record_frame(frame_index)) {
        return false;
    }
    for (const GltfViewerLoadingMetrics& metrics : pending) {
        record_metrics(*recorder, frame_index, metrics);
    }
    const bool emitted = !pending.empty();
    pending.clear();
    return emitted;
}

} // namespace cubey::projects::gltf_viewer
