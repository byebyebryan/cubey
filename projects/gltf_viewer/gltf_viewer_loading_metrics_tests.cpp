#include "gltf_viewer_loading_metrics.h"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

cubey::projects::gltf_viewer::GltfViewerLoadingMetrics sample_metrics(std::uint64_t generation_id) {
    namespace viewer = cubey::projects::gltf_viewer;
    cubey::asset::GltfAsset asset;
    asset.nodes.resize(2U);
    asset.materials.resize(3U);
    asset.textures.resize(4U);
    asset.images = {
        {.encoded_bytes = std::vector<std::uint8_t>(7U)},
        {.rgba8 = std::vector<std::uint8_t>(11U)},
        {.rgba8 = std::vector<std::uint8_t>(13U)},
    };
    cubey::GltfPreparedScene prepared;
    prepared.triangle_count = 17U;
    prepared.textures = {
        {.bytes = std::vector<std::uint8_t>(19U)},
        {.bytes = std::vector<std::uint8_t>(23U)},
    };
    cubey::GltfSceneResident resident;
    resident.mesh_upload_byte_count = 29U;
    resident.mesh_upload_transfer_submission_count = 31U;

    viewer::GltfViewerLoadingMetrics metrics;
    metrics.generation_id = generation_id;
    viewer::collect_gltf_viewer_asset_loading_metrics(metrics, asset);
    viewer::collect_gltf_viewer_prepared_loading_metrics(metrics, prepared);
    viewer::collect_gltf_viewer_resident_loading_metrics(metrics, resident);
    return metrics;
}

void test_loading_metric_aggregation() {
    const cubey::projects::gltf_viewer::GltfViewerLoadingMetrics metrics = sample_metrics(7U);
    require(metrics.node_count == 2U && metrics.material_count == 3U && metrics.texture_count == 4U,
            "asset counts should describe the decoded glTF asset");
    require(metrics.triangle_count == 17U && metrics.prepared_texture_count == 2U,
            "prepared counts should describe the selected import product");
    require(metrics.basisu_encoded_image_byte_count == 7U && metrics.decoded_rgba_byte_count == 24U,
            "image byte totals should keep encoded BasisU and decoded RGBA distinct");
    require(metrics.prepared_texture_upload_byte_count == 42U,
            "prepared texture bytes should aggregate CPU payloads for GPU residency");
    require(metrics.mesh_upload_byte_count == 29U &&
                metrics.mesh_upload_transfer_submission_count == 31U,
            "mesh residency values should preserve existing importer evidence");
}

void test_loading_metric_emission_waits_for_a_recordable_frame() {
    namespace viewer = cubey::projects::gltf_viewer;
    constexpr std::size_t kMetricsPerGeneration = 19U;
    std::vector<viewer::GltfViewerLoadingMetrics> pending{sample_metrics(7U)};

    require(!viewer::emit_gltf_viewer_pending_loading_metrics(nullptr, 3U, pending) &&
                pending.size() == 1U,
            "a missing recorder must retain activated generation metrics");

    cubey::profiling::ProfileRecorder recorder({
        .output_prefix = "gltf-viewer-loading-metrics-tests",
        .warmup_frames = 2U,
    });
    require(!viewer::emit_gltf_viewer_pending_loading_metrics(&recorder, 1U, pending) &&
                pending.size() == 1U && recorder.metric_records().empty(),
            "warmup frames must retain metrics without exporting a partial generation");

    require(viewer::emit_gltf_viewer_pending_loading_metrics(&recorder, 2U, pending) &&
                pending.empty() && recorder.metric_records().size() == kMetricsPerGeneration,
            "the first recordable frame should export exactly one complete generation set");
    require(!viewer::emit_gltf_viewer_pending_loading_metrics(&recorder, 3U, pending) &&
                recorder.metric_records().size() == kMetricsPerGeneration,
            "an emitted generation must not be exported twice");

    pending.push_back(sample_metrics(8U));
    require(viewer::emit_gltf_viewer_pending_loading_metrics(&recorder, 3U, pending) &&
                pending.empty() && recorder.metric_records().size() == 2U * kMetricsPerGeneration,
            "a later activated generation should export its own complete metric set");
    const std::vector<cubey::profiling::ProfileMetricRecord> records = recorder.metric_records();
    require(records.back().category == viewer::kGltfViewerLoadingMetricCategory &&
                records.back().frame_index == 3U,
            "loading metrics should use the stable gltf_loading category on the emission frame");
}

} // namespace

int main() {
    test_loading_metric_aggregation();
    test_loading_metric_emission_waits_for_a_recordable_frame();
    return 0;
}
