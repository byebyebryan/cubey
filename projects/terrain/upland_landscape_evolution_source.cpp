#include "upland_landscape_evolution_source.h"

#include "terrain_landscape_evolution.h"
#include "terrain_patch.h"
#include "upland_broad_noise_source.h"

#include <string>
#include <utility>

namespace cubey::projects::terrain {

cubey::procedural::FieldSet2D
sample_upland_landscape_evolution_fields_v1(cubey::procedural::Grid2DDesc desc,
                                            std::uint64_t seed) {
    const cubey::procedural::FieldSet2D broad = sample_upland_broad_noise_fields_v1(desc, seed);
    cubey::procedural::ScalarField2D uplift_rate(desc, 0.0F);
    const cubey::procedural::ScalarField2D& uplift_potential =
        broad.field(kTerrainFieldUpliftPotential);
    for (std::size_t index = 0U; index < uplift_rate.sample_count(); ++index) {
        uplift_rate.values()[index] = uplift_potential.values()[index] * 1.0e-3F;
    }

    TerrainLandscapeEvolutionConfig config{};
    config.seed = seed;
    TerrainLandscapeEvolutionResult evolved =
        evolve_terrain_landscape(broad.field(kTerrainFieldSourceHeightM), uplift_rate, config);

    cubey::procedural::FieldSet2D result(desc);
    for (const std::string_view name : {
             kTerrainFieldUpliftPotential,
             kTerrainFieldMacroMass,
             kTerrainFieldBaseReliefM,
             kTerrainFieldMountainSupport,
             kTerrainFieldSourceHeightM,
         }) {
        result.add_field(std::string(name), broad.field(name));
    }
    result.add_field(std::string(kTerrainFieldUpliftRateMPerYear),
                     std::move(evolved.uplift_rate_m_per_year));
    result.add_field(std::string(kTerrainFieldProcessDrainageAreaM2),
                     std::move(evolved.process_drainage_area_m2));
    result.add_field(std::string(kTerrainFieldProcessFlowDirectionX),
                     std::move(evolved.process_flow_direction_x));
    result.add_field(std::string(kTerrainFieldProcessFlowDirectionZ),
                     std::move(evolved.process_flow_direction_z));
    result.add_field(std::string(kTerrainFieldProcessBreachMask),
                     std::move(evolved.process_breach_mask));
    result.add_field(std::string(kTerrainFieldFluvialAdvectionRateMPerYear),
                     std::move(evolved.fluvial_advection_rate_m_per_year));
    result.add_field(std::string(kTerrainFieldHillslopeAdvectionRateMPerYear),
                     std::move(evolved.hillslope_advection_rate_m_per_year));
    result.add_field(std::string(kTerrainFieldThermalActiveMask),
                     std::move(evolved.thermal_active_mask));
    result.add_field(std::string(kTerrainFieldAnalyticalHeightM),
                     std::move(evolved.analytical_height_m));
    result.add_field(std::string(kTerrainFieldAltitudeCorrectionDeltaM),
                     std::move(evolved.altitude_correction_delta_m));
    result.add_field(std::string(kTerrainFieldProcessDeltaM), std::move(evolved.process_delta_m));
    result.add_field(std::string(kTerrainFieldHeightM), std::move(evolved.height_m));
    return result;
}

} // namespace cubey::projects::terrain
