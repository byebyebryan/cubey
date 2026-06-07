#include "planet_local_detail_runtime.h"

#include <stdexcept>

namespace cubey::projects::planet {

void PlanetLocalDetailRuntime::rebuild(const PlanetConfig& config, const PlanetFrame& frame) {
    build_ = make_planet_local_detail_mesh(config, frame);
    build_config_ = config;
    has_build_ = true;
}

bool PlanetLocalDetailRuntime::topology_changed(const PlanetConfig& config) const {
    if (!has_build_) {
        return true;
    }
    return build_config_.local_detail_lod_levels != config.local_detail_lod_levels ||
           build_config_.local_detail_cells_per_axis != config.local_detail_cells_per_axis ||
           build_config_.local_detail_outer_half_extent_m !=
               config.local_detail_outer_half_extent_m;
}

const PlanetLocalDetailMeshData& PlanetLocalDetailRuntime::mesh() const {
    if (!has_build_ || build_.mesh.vertices.empty() || build_.mesh.indices.empty()) {
        throw std::runtime_error("planet local detail mesh is not initialized");
    }
    return build_.mesh;
}

const PlanetLocalDetailDiagnostics& PlanetLocalDetailRuntime::diagnostics() const {
    if (!has_build_) {
        throw std::runtime_error("planet local detail diagnostics are not initialized");
    }
    return build_.diagnostics;
}

} // namespace cubey::projects::planet
