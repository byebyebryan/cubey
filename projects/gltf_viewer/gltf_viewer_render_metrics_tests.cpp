#include "gltf_viewer_render_metrics.h"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_render_metric_emission_contract() {
    namespace viewer = cubey::projects::gltf_viewer;

    cubey::ForwardPbrRenderer3DFrameMetrics metrics;
    metrics.draw_plan = {
        .scene_source_packet_count = 1U,
        .scene_classification_count = 2U,
        .shadow_source_packet_count = 3U,
        .shadow_classification_count = 4U,
        .route_packet_reference_count = 5U,
        .visible_scene_unique_material_count = 6U,
        .has_transmission = true,
    };
    metrics.material_table = {
        .initialized = true,
        .material_count = 7U,
        .allocated_descriptor_set_count = 8U,
        .block_count = 9U,
        .descriptor_pool_count = 10U,
        .uniform_buffer_count = 11U,
        .block_capacity = 12U,
        .uniform_stride = 13U,
        .uniform_block_byte_size = 14U,
        .allocated_uniform_byte_size = 15U,
    };
    metrics.render_graph = {
        .pass_count = 16U,
        .texture_count = 17U,
        .buffer_count = 18U,
        .before_texture_barrier_count = 19U,
        .before_buffer_barrier_count = 20U,
        .after_texture_barrier_count = 21U,
        .after_buffer_barrier_count = 22U,
        .before_barrier_count = 39U,
        .after_barrier_count = 43U,
    };
    metrics.render_graph_frame = {
        .slot_action = cubey::render::RenderGraphFrameSlotAction::Reused,
        .resource_prepare_milliseconds = 23.0,
        .graph_record_milliseconds = 24.0,
    };
    metrics.draw_plan_build_milliseconds = 25.0;
    metrics.render_graph_build_compile_milliseconds = 26.0;

    cubey::profiling::ProfileRecorder recorder({
        .output_prefix = "gltf-viewer-render-metrics-tests",
        .warmup_frames = 2U,
    });
    require(!viewer::emit_gltf_viewer_render_metrics(nullptr, 2U, metrics),
            "render metrics should not emit without a recorder");
    require(!viewer::emit_gltf_viewer_render_metrics(&recorder, 1U, metrics),
            "render metrics should retain warmup-frame policy");
    require(viewer::emit_gltf_viewer_render_metrics(&recorder, 2U, metrics),
            "render metrics should emit on the first recordable frame");

    constexpr std::array<std::pair<std::string_view, std::string_view>, 29> kMetricNames{
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerForwardPbrMetricCategory,
                                                      "scene_source_packet_count"},
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerForwardPbrMetricCategory,
                                                      "scene_classification_count"},
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerForwardPbrMetricCategory,
                                                      "shadow_source_packet_count"},
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerForwardPbrMetricCategory,
                                                      "shadow_classification_count"},
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerForwardPbrMetricCategory,
                                                      "route_packet_reference_count"},
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerForwardPbrMetricCategory,
                                                      "visible_scene_unique_material_count"},
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerForwardPbrMetricCategory,
                                                      "has_transmission"},
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerForwardPbrMetricCategory,
                                                      "material_table_initialized"},
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerForwardPbrMetricCategory,
                                                      "material_count"},
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerForwardPbrMetricCategory,
                                                      "allocated_descriptor_set_count"},
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerForwardPbrMetricCategory,
                                                      "block_count"},
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerForwardPbrMetricCategory,
                                                      "descriptor_pool_count"},
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerForwardPbrMetricCategory,
                                                      "uniform_buffer_count"},
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerForwardPbrMetricCategory,
                                                      "block_capacity"},
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerForwardPbrMetricCategory,
                                                      "uniform_stride_bytes"},
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerForwardPbrMetricCategory,
                                                      "uniform_block_byte_size"},
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerForwardPbrMetricCategory,
                                                      "allocated_uniform_byte_size"},
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerRenderGraphMetricCategory,
                                                      "pass_count"},
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerRenderGraphMetricCategory,
                                                      "texture_count"},
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerRenderGraphMetricCategory,
                                                      "buffer_count"},
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerRenderGraphMetricCategory,
                                                      "before_texture_barrier_count"},
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerRenderGraphMetricCategory,
                                                      "before_buffer_barrier_count"},
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerRenderGraphMetricCategory,
                                                      "after_texture_barrier_count"},
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerRenderGraphMetricCategory,
                                                      "after_buffer_barrier_count"},
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerRenderGraphMetricCategory,
                                                      "before_barrier_count"},
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerRenderGraphMetricCategory,
                                                      "after_barrier_count"},
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerRenderGraphMetricCategory,
                                                      "frame_slot_created"},
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerRenderGraphMetricCategory,
                                                      "frame_slot_replaced"},
        std::pair<std::string_view, std::string_view>{viewer::kGltfViewerRenderGraphMetricCategory,
                                                      "frame_slot_reused"},
    };
    const std::vector<cubey::profiling::ProfileMetricRecord> records = recorder.metric_records();
    require(records.size() == kMetricNames.size(),
            "render metric emission should export the complete inventory once");
    for (std::size_t index = 0; index < records.size(); ++index) {
        require(records[index].frame_index == 2U &&
                    records[index].category == kMetricNames[index].first &&
                    records[index].name == kMetricNames[index].second,
                "render metric emission should preserve stable frame/category/name policy");
    }
    require(records[6].value == 1.0 && records[24].value == 39.0 && records[25].value == 43.0 &&
                records[26].value == 0.0 && records[27].value == 0.0 && records[28].value == 1.0,
            "render metric emission should encode transmission and slot action values");

    constexpr std::array<std::string_view, 4> kSpanNames{
        "forward_pbr.draw_plan_build",
        "forward_pbr.render_graph_build_compile",
        "render_graph.resource_prepare",
        "render_graph.record",
    };
    const std::vector<cubey::profiling::ProfileSpanRecord> spans = recorder.span_records();
    require(spans.size() == kSpanNames.size(),
            "render metric emission should export the complete CPU span inventory once");
    for (std::size_t index = 0; index < spans.size(); ++index) {
        require(spans[index].frame_index == 2U && spans[index].label == kSpanNames[index] &&
                    spans[index].duration_milliseconds > 0.0,
                "render metric emission should translate durations to stable CPU spans");
    }
}

} // namespace

void test_gltf_viewer_render_metric_emission() {
    test_render_metric_emission_contract();
}
