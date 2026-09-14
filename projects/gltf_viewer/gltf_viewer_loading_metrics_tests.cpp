#include "gltf_viewer_loading_metrics.h"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <vector>

void test_gltf_viewer_render_metric_emission();

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
    resident.final_upload_step = cubey::vulkan::GpuUploadStepTicket{};
    resident.upload_session_metrics = {
        .owner_advance_count = 31U,
        .step_count = 37U,
        .submission_count = 41U,
        .uploaded_byte_count = 43U,
        .copy_count = 47U,
        .owner_total_milliseconds = 53.0,
        .owner_max_step_milliseconds = 59.0,
        .owner_target_milliseconds = 61.0,
        .owner_over_target_step_count = 67U,
        .backpressure_count = 61U,
        .pool_initial_capacity_byte_count = 71U,
        .pool_final_capacity_byte_count = 73U,
        .pool_peak_capacity_byte_count = 79U,
        .pool_reserved_at_final_submission_byte_count = 83U,
        .pool_growth_count = 89U,
        .first_step_to_final_completion_milliseconds = 97.0,
        .step_byte_cap = 63U,
        .copy_byte_target = 65U,
    };

    viewer::GltfViewerLoadingMetrics metrics;
    metrics.generation_id = generation_id;
    viewer::collect_gltf_viewer_asset_loading_metrics(metrics, asset);
    viewer::collect_gltf_viewer_asset_load_phase_metrics(metrics,
                                                         {
                                                             .document_parse_milliseconds = 1.0,
                                                             .buffer_load_milliseconds = 2.0,
                                                             .asset_validate_milliseconds = 3.0,
                                                             .image_payload_milliseconds = 4.0,
                                                             .image_decode_milliseconds = 5.0,
                                                             .asset_assembly_milliseconds = 6.0,
                                                         });
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
    require(metrics.document_parse_milliseconds == 1.0 && metrics.buffer_load_milliseconds == 2.0 &&
                metrics.asset_validate_milliseconds == 3.0 &&
                metrics.image_payload_milliseconds == 4.0 &&
                metrics.image_decode_milliseconds == 5.0 &&
                metrics.asset_assembly_milliseconds == 6.0,
            "loader phase timings should propagate without changing their boundaries");
    require(metrics.prepared_texture_upload_byte_count == 42U,
            "prepared texture bytes should aggregate CPU payloads for GPU residency");
    require(metrics.mesh_upload_byte_count == 29U &&
                metrics.mesh_upload_transfer_submission_count == 31U,
            "mesh residency values should preserve existing importer evidence");
    require(metrics.gpu_upload_owner_advance_count == 31U && metrics.gpu_upload_step_count == 37U &&
                metrics.gpu_upload_submission_count == 41U &&
                metrics.gpu_upload_byte_count == 43U && metrics.gpu_upload_copy_count == 47U &&
                metrics.gpu_upload_owner_submit_milliseconds == 53.0 &&
                metrics.gpu_upload_owner_max_step_milliseconds == 59.0 &&
                metrics.gpu_upload_owner_target_milliseconds == 61.0 &&
                metrics.gpu_upload_step_byte_cap == 63U &&
                metrics.gpu_upload_copy_byte_target == 65U &&
                metrics.gpu_upload_owner_over_target_step_count == 67U &&
                metrics.gpu_upload_backpressure_count == 61U &&
                metrics.gpu_upload_pool_initial_capacity_byte_count == 71U &&
                metrics.gpu_upload_pool_final_capacity_byte_count == 73U &&
                metrics.gpu_upload_pool_peak_capacity_byte_count == 79U &&
                metrics.gpu_upload_pool_reserved_at_final_submission_byte_count == 83U &&
                metrics.gpu_upload_pool_growth_count == 89U &&
                metrics.gpu_upload_first_step_to_final_completion_milliseconds == 97.0,
            "session residency should export its precise generation aggregate");
}

void test_loading_metric_emission_waits_for_a_recordable_frame() {
    namespace viewer = cubey::projects::gltf_viewer;
    constexpr std::array<std::string_view, 46> kMetricNames{
        "generation_id",
        "source_file_bytes",
        "metadata_probe_ms",
        "asset_load_ms",
        "document_parse_ms",
        "buffer_load_ms",
        "asset_validate_ms",
        "image_payload_ms",
        "image_decode_ms",
        "asset_assembly_ms",
        "scene_prepare_ms",
        "staged_worker_prepare_ms",
        "gltf_residency_ms",
        "staged_gpu_install_ms",
        "activation_ms",
        "triangle_count",
        "node_count",
        "material_count",
        "texture_count",
        "prepared_texture_count",
        "basisu_encoded_image_bytes",
        "decoded_rgba_bytes",
        "prepared_texture_upload_bytes",
        "mesh_upload_bytes",
        "mesh_upload_transfer_submission_count",
        "gpu_upload_bytes",
        "gpu_upload_copy_count",
        "gpu_upload_owner_advance_count",
        "gpu_upload_step_count",
        "gpu_upload_submission_count",
        "gpu_upload_owner_submit_ms",
        "gpu_upload_owner_max_step_ms",
        "gpu_upload_owner_target_ms",
        "gpu_upload_step_byte_cap",
        "gpu_upload_copy_byte_target",
        "gpu_upload_owner_over_target_step_count",
        "gpu_upload_completion_latency_ms",
        "gpu_upload_pool_initial_capacity_bytes",
        "gpu_upload_pool_final_capacity_bytes",
        "gpu_upload_pool_peak_capacity_bytes",
        "gpu_upload_pool_reserved_at_final_submission_bytes",
        "gpu_upload_pool_growth_count",
        "gpu_upload_backpressure_count",
        "gpu_upload_first_step_to_final_completion_ms",
        "gpu_upload_submission_frame",
        "gpu_upload_completion_frame",
    };
    constexpr std::size_t kMetricsPerGeneration = kMetricNames.size();
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
    for (std::size_t index = 0; index < kMetricNames.size(); ++index) {
        require(records[index].name == kMetricNames[index],
                "one generation should export the exact glTF loading metric inventory");
    }
}

} // namespace

int main() {
    test_loading_metric_aggregation();
    test_loading_metric_emission_waits_for_a_recordable_frame();
    test_gltf_viewer_render_metric_emission();
    return 0;
}
