#include "gltf_viewer_render_metrics.h"

#include <cstddef>

namespace cubey::projects::gltf_viewer {
namespace {

void record_metric(cubey::profiling::ProfileRecorder& recorder, std::uint64_t frame_index,
                   std::string_view category, std::string_view name, double value) {
    recorder.record_metric(frame_index, category, name, value);
}

void record_forward_pbr_metrics(cubey::profiling::ProfileRecorder& recorder,
                                std::uint64_t frame_index,
                                const cubey::ForwardPbrRenderer3DFrameMetrics& metrics) {
    const cubey::ForwardPbrRenderer3DFrameDrawMetrics& draw_plan = metrics.draw_plan;
    record_metric(recorder, frame_index, kGltfViewerForwardPbrMetricCategory,
                  "scene_source_packet_count",
                  static_cast<double>(draw_plan.scene_source_packet_count));
    record_metric(recorder, frame_index, kGltfViewerForwardPbrMetricCategory,
                  "scene_classification_count",
                  static_cast<double>(draw_plan.scene_classification_count));
    record_metric(recorder, frame_index, kGltfViewerForwardPbrMetricCategory,
                  "shadow_source_packet_count",
                  static_cast<double>(draw_plan.shadow_source_packet_count));
    record_metric(recorder, frame_index, kGltfViewerForwardPbrMetricCategory,
                  "shadow_classification_count",
                  static_cast<double>(draw_plan.shadow_classification_count));
    record_metric(recorder, frame_index, kGltfViewerForwardPbrMetricCategory,
                  "route_packet_reference_count",
                  static_cast<double>(draw_plan.route_packet_reference_count));
    record_metric(recorder, frame_index, kGltfViewerForwardPbrMetricCategory,
                  "visible_scene_unique_material_count",
                  static_cast<double>(draw_plan.visible_scene_unique_material_count));
    record_metric(recorder, frame_index, kGltfViewerForwardPbrMetricCategory, "has_transmission",
                  draw_plan.has_transmission ? 1.0 : 0.0);

    const cubey::render::PbrMaterialTableMetrics& material_table = metrics.material_table;
    record_metric(recorder, frame_index, kGltfViewerForwardPbrMetricCategory,
                  "material_table_initialized", material_table.initialized ? 1.0 : 0.0);
    record_metric(recorder, frame_index, kGltfViewerForwardPbrMetricCategory, "material_count",
                  static_cast<double>(material_table.material_count));
    record_metric(recorder, frame_index, kGltfViewerForwardPbrMetricCategory,
                  "allocated_descriptor_set_count",
                  static_cast<double>(material_table.allocated_descriptor_set_count));
    record_metric(recorder, frame_index, kGltfViewerForwardPbrMetricCategory, "block_count",
                  static_cast<double>(material_table.block_count));
    record_metric(recorder, frame_index, kGltfViewerForwardPbrMetricCategory,
                  "descriptor_pool_count",
                  static_cast<double>(material_table.descriptor_pool_count));
    record_metric(recorder, frame_index, kGltfViewerForwardPbrMetricCategory,
                  "uniform_buffer_count", static_cast<double>(material_table.uniform_buffer_count));
    record_metric(recorder, frame_index, kGltfViewerForwardPbrMetricCategory, "block_capacity",
                  static_cast<double>(material_table.block_capacity));
    record_metric(recorder, frame_index, kGltfViewerForwardPbrMetricCategory,
                  "uniform_stride_bytes", static_cast<double>(material_table.uniform_stride));
    record_metric(recorder, frame_index, kGltfViewerForwardPbrMetricCategory,
                  "uniform_block_byte_size",
                  static_cast<double>(material_table.uniform_block_byte_size));
    record_metric(recorder, frame_index, kGltfViewerForwardPbrMetricCategory,
                  "allocated_uniform_byte_size",
                  static_cast<double>(material_table.allocated_uniform_byte_size));

    recorder.record_cpu_span(frame_index, "forward_pbr.draw_plan_build",
                             metrics.draw_plan_build_milliseconds);
    recorder.record_cpu_span(frame_index, "forward_pbr.render_graph_build_compile",
                             metrics.render_graph_build_compile_milliseconds);
}

void record_render_graph_metrics(cubey::profiling::ProfileRecorder& recorder,
                                 std::uint64_t frame_index,
                                 const cubey::ForwardPbrRenderer3DFrameMetrics& metrics) {
    const cubey::render::RenderGraphCompiledMetrics& graph = metrics.render_graph;
    record_metric(recorder, frame_index, kGltfViewerRenderGraphMetricCategory, "pass_count",
                  static_cast<double>(graph.pass_count));
    record_metric(recorder, frame_index, kGltfViewerRenderGraphMetricCategory, "texture_count",
                  static_cast<double>(graph.texture_count));
    record_metric(recorder, frame_index, kGltfViewerRenderGraphMetricCategory, "buffer_count",
                  static_cast<double>(graph.buffer_count));
    record_metric(recorder, frame_index, kGltfViewerRenderGraphMetricCategory,
                  "before_texture_barrier_count",
                  static_cast<double>(graph.before_texture_barrier_count));
    record_metric(recorder, frame_index, kGltfViewerRenderGraphMetricCategory,
                  "before_buffer_barrier_count",
                  static_cast<double>(graph.before_buffer_barrier_count));
    record_metric(recorder, frame_index, kGltfViewerRenderGraphMetricCategory,
                  "after_texture_barrier_count",
                  static_cast<double>(graph.after_texture_barrier_count));
    record_metric(recorder, frame_index, kGltfViewerRenderGraphMetricCategory,
                  "after_buffer_barrier_count",
                  static_cast<double>(graph.after_buffer_barrier_count));
    record_metric(recorder, frame_index, kGltfViewerRenderGraphMetricCategory,
                  "before_barrier_count", static_cast<double>(graph.before_barrier_count));
    record_metric(recorder, frame_index, kGltfViewerRenderGraphMetricCategory,
                  "after_barrier_count", static_cast<double>(graph.after_barrier_count));

    const cubey::render::RenderGraphFrameRecordMetrics& frame = metrics.render_graph_frame;
    record_metric(recorder, frame_index, kGltfViewerRenderGraphMetricCategory, "frame_slot_created",
                  frame.slot_action == cubey::render::RenderGraphFrameSlotAction::Created ? 1.0
                                                                                          : 0.0);
    record_metric(
        recorder, frame_index, kGltfViewerRenderGraphMetricCategory, "frame_slot_replaced",
        frame.slot_action == cubey::render::RenderGraphFrameSlotAction::Replaced ? 1.0 : 0.0);
    record_metric(recorder, frame_index, kGltfViewerRenderGraphMetricCategory, "frame_slot_reused",
                  frame.slot_action == cubey::render::RenderGraphFrameSlotAction::Reused ? 1.0
                                                                                         : 0.0);

    recorder.record_cpu_span(frame_index, "render_graph.resource_prepare",
                             frame.resource_prepare_milliseconds);
    recorder.record_cpu_span(frame_index, "render_graph.record", frame.graph_record_milliseconds);
}

} // namespace

bool emit_gltf_viewer_render_metrics(cubey::profiling::ProfileRecorder* recorder,
                                     std::uint64_t frame_index,
                                     const cubey::ForwardPbrRenderer3DFrameMetrics& metrics) {
    if (recorder == nullptr || !recorder->should_record_frame(frame_index)) {
        return false;
    }
    record_forward_pbr_metrics(*recorder, frame_index, metrics);
    record_render_graph_metrics(*recorder, frame_index, metrics);
    return true;
}

} // namespace cubey::projects::gltf_viewer
