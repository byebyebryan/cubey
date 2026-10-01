#include "fluid_25d_mass_audit.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace cubey::projects::fluid::fluid_25d {
namespace {

class CompensatedSum {
  public:
    void add(double value) noexcept {
        const double next = sum_ + value;
        if (std::abs(sum_) >= std::abs(value)) {
            correction_ += (sum_ - next) + value;
        } else {
            correction_ += (value - next) + sum_;
        }
        sum_ = next;
    }

    [[nodiscard]] double value() const noexcept {
        return sum_ + correction_;
    }

  private:
    double sum_ = 0.0;
    double correction_ = 0.0;
};

void validate_nonnegative_finite(double value, const char* name) {
    if (!std::isfinite(value) || value < 0.0) {
        throw std::runtime_error(name);
    }
}

[[nodiscard]] std::uint64_t checked_committed_count(double high, double low) {
    if (!std::isfinite(high) || !std::isfinite(low)) {
        throw std::runtime_error("fluid 2.5D mass audit committed-step expansion is nonfinite");
    }
    const double count = high + low;
    if (!std::isfinite(count) || count < 0.0 || std::floor(count) != count ||
        count >= std::ldexp(1.0, 64)) {
        throw std::runtime_error("fluid 2.5D mass audit committed-step count is invalid");
    }
    return static_cast<std::uint64_t>(count);
}

void require_finite_diagnostics(const Fluid25DMassAuditDiagnostics& diagnostics) {
    for (std::size_t term = 0U; term < kFluid25DMassAuditTerms; ++term) {
        if (!std::isfinite(diagnostics.totals_m3[term])) {
            throw std::runtime_error("fluid 2.5D mass audit total is nonfinite");
        }
    }
    if (!std::isfinite(diagnostics.field_budget_residual_m3) ||
        !std::isfinite(diagnostics.cumulative_ledger_rounding_m3) ||
        !std::isfinite(diagnostics.source_representation_difference_m3) ||
        !std::isfinite(diagnostics.sink_representation_difference_m3) ||
        !std::isfinite(diagnostics.boundary_definition_difference_m3) ||
        !std::isfinite(diagnostics.observed_conservation_residual_m3)) {
        throw std::runtime_error("fluid 2.5D mass audit residual is nonfinite");
    }
}

} // namespace

Fluid25DMassAuditDiagnostics
compute_fluid_25d_mass_audit(std::span<const Fluid25DMassAuditGpu> audit_cells,
                             double stored_volume_m3, double initial_volume_m3,
                             double source_ledger_m3, double sink_ledger_m3,
                             double boundary_ledger_m3, bool tracer) {
    if (audit_cells.empty()) {
        throw std::runtime_error("fluid 2.5D mass audit requires at least one cell");
    }
    validate_nonnegative_finite(stored_volume_m3, "fluid 2.5D mass audit stored volume is invalid");
    validate_nonnegative_finite(initial_volume_m3,
                                "fluid 2.5D mass audit initial volume is invalid");
    validate_nonnegative_finite(source_ledger_m3, "fluid 2.5D mass audit source ledger is invalid");
    validate_nonnegative_finite(sink_ledger_m3, "fluid 2.5D mass audit sink ledger is invalid");
    validate_nonnegative_finite(boundary_ledger_m3,
                                "fluid 2.5D mass audit boundary ledger is invalid");

    std::array<CompensatedSum, kFluid25DMassAuditTerms> sums{};
    std::uint64_t committed_substeps = 0U;
    bool has_committed_count = false;
    for (std::size_t cell_index = 0U; cell_index < audit_cells.size(); ++cell_index) {
        const Fluid25DMassAuditGpu& cell = audit_cells[cell_index];
        const std::array<float, kFluid25DMassAuditTerms>& high =
            tracer ? cell.tracer_hi : cell.water_hi;
        const std::array<float, kFluid25DMassAuditTerms>& low =
            tracer ? cell.tracer_lo : cell.water_lo;

        for (std::size_t term = 0U; term < kFluid25DMassAuditTerms; ++term) {
            const float high_value = high[term];
            const float low_value = low[term];
            if (!std::isfinite(high_value) || !std::isfinite(low_value)) {
                throw std::runtime_error("fluid 2.5D mass audit expansion is nonfinite");
            }
            if (term == 9U) {
                const std::uint64_t cell_count = checked_committed_count(high_value, low_value);
                if (has_committed_count && cell_count != committed_substeps) {
                    throw std::runtime_error(
                        "fluid 2.5D mass audit committed-step count differs between cells");
                }
                committed_substeps = cell_count;
                has_committed_count = true;
                continue;
            }
            if (term >= 10U) {
                if (high_value != 0.0F || low_value != 0.0F) {
                    throw std::runtime_error("fluid 2.5D mass audit reserved term is nonzero");
                }
                continue;
            }
            sums[term].add(static_cast<double>(high_value));
            sums[term].add(static_cast<double>(low_value));
        }
    }

    Fluid25DMassAuditDiagnostics diagnostics;
    for (std::size_t term = 0U; term < 9U; ++term) {
        diagnostics.totals_m3[term] = sums[term].value();
    }
    diagnostics.totals_m3[9] = static_cast<double>(committed_substeps);
    diagnostics.committed_substeps = committed_substeps;

    constexpr std::array<std::size_t, 6U> nonnegative_volume_terms{0U, 1U, 2U, 3U, 4U, 6U};
    for (const std::size_t term : nonnegative_volume_terms) {
        if (diagnostics.totals_m3[term] < 0.0) {
            throw std::runtime_error("fluid 2.5D mass audit volume term is negative");
        }
    }

    const double source_delta = diagnostics.totals_m3[3U];
    const double sink_delta = diagnostics.totals_m3[4U];
    const double internal_transport = diagnostics.totals_m3[5U];
    const double boundary_transport = diagnostics.totals_m3[6U];
    const double update_arithmetic = diagnostics.totals_m3[7U];
    const double clamp_correction = diagnostics.totals_m3[8U];
    diagnostics.field_budget_residual_m3 =
        stored_volume_m3 - initial_volume_m3 -
        (source_delta - sink_delta + internal_transport - boundary_transport + update_arithmetic +
         clamp_correction);
    diagnostics.cumulative_ledger_rounding_m3 = (diagnostics.totals_m3[0U] - source_ledger_m3) +
                                                (sink_ledger_m3 - diagnostics.totals_m3[1U]) +
                                                (boundary_ledger_m3 - diagnostics.totals_m3[2U]);
    diagnostics.source_representation_difference_m3 =
        diagnostics.totals_m3[3U] - diagnostics.totals_m3[0U];
    diagnostics.sink_representation_difference_m3 =
        diagnostics.totals_m3[1U] - diagnostics.totals_m3[4U];
    diagnostics.boundary_definition_difference_m3 =
        diagnostics.totals_m3[2U] - diagnostics.totals_m3[6U];
    diagnostics.observed_conservation_residual_m3 = stored_volume_m3 - initial_volume_m3 -
                                                    source_ledger_m3 + sink_ledger_m3 +
                                                    boundary_ledger_m3;
    require_finite_diagnostics(diagnostics);
    return diagnostics;
}

} // namespace cubey::projects::fluid::fluid_25d
