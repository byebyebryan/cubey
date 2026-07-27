#include "planet_local_detail_runtime.h"

#include <stdexcept>

namespace cubey::projects::planet {

void PlanetLocalDetailRuntime::rebuild(const PlanetConfig& config, const PlanetFrame& frame) {
    rebuild(config, frame, default_planet_local_detail_view(frame));
}

void PlanetLocalDetailRuntime::rebuild(const PlanetConfig& config, const PlanetFrame& frame,
                                       PlanetLocalDetailView view) {
    build_ = make_planet_local_detail_mesh(config, frame, view);
    build_config_ = config;
    build_view_ = view;
    has_build_ = true;
}

void PlanetLocalDetailRuntime::clear() {
    build_ = {};
    build_config_ = {};
    build_view_ = {};
    has_build_ = false;
}

bool PlanetLocalDetailRuntime::topology_changed(const PlanetConfig& config) const {
    return topology_changed(config, build_view_);
}

bool PlanetLocalDetailRuntime::topology_changed(const PlanetConfig& config,
                                                PlanetLocalDetailView view) const {
    if (!has_build_) {
        return true;
    }
    if (build_config_.local_detail_lod_levels != config.local_detail_lod_levels ||
        build_config_.local_detail_cells_per_axis != config.local_detail_cells_per_axis ||
        build_config_.local_detail_outer_half_extent_m != config.local_detail_outer_half_extent_m ||
        build_config_.local_detail_enabled != config.local_detail_enabled) {
        return true;
    }
    const cubey::render::ClipmapGrid2DConfig grid =
        planet_local_detail_clipmap_config(config, view);
    const PlanetLocalDetailActiveRange active_range =
        planet_local_detail_active_range(config, grid, view);
    const PlanetLocalDetailDiagnostics& built = build_.diagnostics;
    return built.active != active_range.active ||
           built.active_first_level != active_range.first_level ||
           built.active_level_count != active_range.level_count ||
           built.outer_half_extent != grid.outer_half_extent ||
           built.finest_active_cell_size != active_range.finest_active_cell_size ||
           built.coarsest_active_cell_size != active_range.coarsest_active_cell_size;
}

bool PlanetLocalDetailRuntime::has_drawable_mesh() const {
    return has_build_ && !build_.mesh.vertices.empty() && !build_.mesh.indices.empty();
}

const PlanetLocalDetailMeshData& PlanetLocalDetailRuntime::mesh() const {
    if (!has_drawable_mesh()) {
        throw std::runtime_error("planet local detail mesh is not initialized");
    }
    return build_.mesh;
}

const PlanetLocalDetailDiagnostics& PlanetLocalDetailRuntime::diagnostics() const {
    static constexpr PlanetLocalDetailDiagnostics kInactiveDiagnostics{};
    if (!has_build_) {
        return kInactiveDiagnostics;
    }
    return build_.diagnostics;
}

} // namespace cubey::projects::planet
