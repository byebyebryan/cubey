#include "planet_surface_runtime.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <stdexcept>

namespace cubey::projects::planet {

void PlanetSurfaceRuntime::rebuild(const PlanetConfig& config, const PlanetFrame& frame,
                                   PlanetSurfaceView view) {
    const PlanetSurfacePatchPlan plan = plan_planet_surface_patches(
        config, view,
        PlanetSurfacePatchSelectionHints{
            .previous_selected_patches = previous_selected_patch_ids_,
        });
    patch_grid_ = make_planet_patch_grid_mesh(config);
    patch_instances_ = make_planet_surface_gpu_patch_instances(config, plan);
    refresh_previous_selected_patch_ids(plan);
    mark_instance_buffers_stale();
    refresh_render_diagnostics(config, plan);
    build_render_origin_world_m_ = frame.render_origin_world_m;
    build_view_ = view;
    has_build_ = true;
}

bool PlanetSurfaceRuntime::plan_changed(const PlanetConfig& config, const PlanetFrame& frame,
                                        PlanetSurfaceView view) const {
    if (!has_build_) {
        return true;
    }

    const double origin_threshold_m =
        std::max(static_cast<double>(config.radius_m) * 0.002, 256.0);
    if (glm::length(frame.render_origin_world_m - build_render_origin_world_m_) >
        origin_threshold_m) {
        return true;
    }
    if (std::abs(view.aspect_ratio - build_view_.aspect_ratio) > 0.005F ||
        std::abs(view.viewport_height_px - build_view_.viewport_height_px) > 1.0F) {
        return true;
    }
    const float forward_dot =
        glm::dot(glm::normalize(view.camera_forward_world),
                 glm::normalize(build_view_.camera_forward_world));
    return forward_dot < 0.9985F;
}

void PlanetSurfaceRuntime::clear_gpu_buffers() {
    instance_buffers_.clear();
}

void PlanetSurfaceRuntime::clear_selection_history() {
    previous_selected_patch_ids_.clear();
    has_build_ = false;
}

void PlanetSurfaceRuntime::resize_frame_slots(std::uint32_t frame_slot_count) {
    if (frame_slot_count == 0U) {
        throw std::runtime_error("planet patch instance buffers require at least one frame slot");
    }
    if (instance_buffers_.size() == frame_slot_count) {
        return;
    }
    instance_buffers_.clear();
    instance_buffers_.resize(frame_slot_count);
}

const cubey::render::InstanceBuffer<PlanetSurfaceGpuPatchInstance>&
PlanetSurfaceRuntime::ensure_instance_buffer(cubey::vulkan::GpuRuntime& gpu,
                                             cubey::render::FrameSlot frame_slot) {
    cubey::render::validate_frame_slot(frame_slot);
    if (frame_slot.count != instance_buffers_.size()) {
        throw std::runtime_error("planet patch instance buffer frame slot count mismatch");
    }
    if (patch_instances_.empty()) {
        throw std::runtime_error("planet patch instance upload requires non-empty patches");
    }
    InstanceBufferSlot& slot = instance_buffers_.at(frame_slot.index);
    if (!slot.buffer.has_value() || slot.generation != instance_generation_) {
        slot.buffer.emplace(gpu, std::span<const PlanetSurfaceGpuPatchInstance>{patch_instances_});
        slot.generation = instance_generation_;
    }
    return slot.buffer.value();
}

const PlanetPatchGridMeshData& PlanetSurfaceRuntime::patch_grid() const {
    if (patch_grid_.vertices.empty() || patch_grid_.indices.empty()) {
        throw std::runtime_error("planet patch grid mesh data is not initialized");
    }
    return patch_grid_;
}

const PlanetSurfaceDiagnostics& PlanetSurfaceRuntime::diagnostics() const {
    return build_.diagnostics;
}

cubey::math::DVec3 PlanetSurfaceRuntime::render_origin_world_m() const {
    return build_render_origin_world_m_;
}

std::uint32_t PlanetSurfaceRuntime::instance_count() const {
    return static_cast<std::uint32_t>(patch_instances_.size());
}

void PlanetSurfaceRuntime::refresh_previous_selected_patch_ids(
    const PlanetSurfacePatchPlan& plan) {
    previous_selected_patch_ids_.clear();
    previous_selected_patch_ids_.reserve(plan.selected_patches.size());
    for (const PlanetSurfacePatchInstance& patch : plan.selected_patches) {
        previous_selected_patch_ids_.push_back(patch.id);
    }
}

void PlanetSurfaceRuntime::refresh_render_diagnostics(const PlanetConfig& config,
                                                      const PlanetSurfacePatchPlan& plan) {
    build_ = {};
    build_.diagnostics = plan.diagnostics;
    build_.diagnostics.vertex_count =
        static_cast<std::uint32_t>(patch_grid_.vertices.size() * patch_instances_.size());
    build_.diagnostics.triangle_count =
        static_cast<std::uint32_t>((patch_grid_.indices.size() / 3U) * patch_instances_.size());

    for (std::size_t index = 0; index < build_.diagnostics.min_cell_edge_m_by_lod.size();
         ++index) {
        const float min_edge = build_.diagnostics.min_cell_edge_m_by_lod[index];
        const float max_edge = build_.diagnostics.max_cell_edge_m_by_lod[index];
        if (min_edge > 0.0F && (build_.diagnostics.min_edge_length_m == 0.0F ||
                                min_edge < build_.diagnostics.min_edge_length_m)) {
            build_.diagnostics.min_edge_length_m = min_edge;
        }
        build_.diagnostics.max_edge_length_m =
            std::max(build_.diagnostics.max_edge_length_m, max_edge);
    }

    if (config.skirts_enabled && build_.diagnostics.patch_count > 0U) {
        build_.diagnostics.seam_edge_count = build_.diagnostics.patch_count * 4U;
        build_.diagnostics.skirt_triangle_count =
            build_.diagnostics.patch_count * config.patch_resolution * 16U;
        const float min_cell = build_.diagnostics.min_edge_length_m > 0.0F
                                   ? build_.diagnostics.min_edge_length_m
                                   : config.radius_m * 0.00001F;
        const float max_cell = std::max(build_.diagnostics.max_edge_length_m, min_cell);
        build_.diagnostics.min_skirt_depth_m =
            std::max(min_cell * config.skirt_depth_scale, config.radius_m * 0.00001F);
        build_.diagnostics.max_skirt_depth_m =
            std::max(max_cell * config.skirt_depth_scale, config.radius_m * 0.00001F);
    }
}

void PlanetSurfaceRuntime::mark_instance_buffers_stale() {
    ++instance_generation_;
    if (instance_generation_ != 0U) {
        return;
    }
    instance_generation_ = 1U;
    for (InstanceBufferSlot& slot : instance_buffers_) {
        slot.generation = 0U;
    }
}

} // namespace cubey::projects::planet
