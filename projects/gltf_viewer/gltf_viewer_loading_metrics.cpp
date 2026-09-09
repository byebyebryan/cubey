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
