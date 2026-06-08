#pragma once

#include "planet_config.h"
#include "planet_frame.h"
#include "planet_local_detail.h"

namespace cubey::projects::planet {

class PlanetLocalDetailRuntime {
  public:
    void rebuild(const PlanetConfig& config, const PlanetFrame& frame);
    void rebuild(const PlanetConfig& config, const PlanetFrame& frame, PlanetLocalDetailView view);
    void clear();
    [[nodiscard]] bool topology_changed(const PlanetConfig& config) const;
    [[nodiscard]] bool topology_changed(const PlanetConfig& config,
                                        PlanetLocalDetailView view) const;

    [[nodiscard]] bool has_drawable_mesh() const;
    [[nodiscard]] const PlanetLocalDetailMeshData& mesh() const;
    [[nodiscard]] const PlanetLocalDetailDiagnostics& diagnostics() const;

  private:
    PlanetLocalDetailBuildResult build_{};
    PlanetConfig build_config_{};
    PlanetLocalDetailView build_view_{};
    bool has_build_ = false;
};

} // namespace cubey::projects::planet
