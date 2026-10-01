#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace cubey::projects::fluid::fluid_25d {

// Each audit term is reduced on the GPU as a float high/low expansion. Keep
// this type independent of gpu_resources.h so diagnostic consumers do not
// create a GPU resource dependency or include cycle.
inline constexpr std::size_t kFluid25DMassAuditTerms = 12U;

struct Fluid25DMassAuditGpu {
    std::array<float, kFluid25DMassAuditTerms> water_hi{};
    std::array<float, kFluid25DMassAuditTerms> water_lo{};
    std::array<float, kFluid25DMassAuditTerms> tracer_hi{};
    std::array<float, kFluid25DMassAuditTerms> tracer_lo{};
};

struct Fluid25DMassAuditDiagnostics {
    // Terms 0-8 are volume totals in m3. Term 9 is the common committed-step
    // count carried by each cell; terms 10-11 are reserved and remain zero.
    std::array<double, kFluid25DMassAuditTerms> totals_m3{};
    double field_budget_residual_m3 = 0.0;
    double cumulative_ledger_rounding_m3 = 0.0;
    double source_representation_difference_m3 = 0.0;
    double sink_representation_difference_m3 = 0.0;
    double boundary_definition_difference_m3 = 0.0;
    double observed_conservation_residual_m3 = 0.0;
    std::uint64_t committed_substeps = 0U;
};

// Reduces one per-cell GPU audit snapshot. The selected channel's high and low
// components are summed in double precision on the host. Existing storage and
// cumulative ledger totals are supplied separately so the audit can distinguish
// field-update error from cumulative-ledger accumulation error.
[[nodiscard]] Fluid25DMassAuditDiagnostics
compute_fluid_25d_mass_audit(std::span<const Fluid25DMassAuditGpu> audit_cells,
                             double stored_volume_m3, double initial_volume_m3,
                             double source_ledger_m3, double sink_ledger_m3,
                             double boundary_ledger_m3, bool tracer = false);

} // namespace cubey::projects::fluid::fluid_25d
