#include "fluid_25d_transport_study.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace cubey::projects::fluid::fluid_25d {
namespace {

using Cell = Fluid25DTransportStudyCell;
using Transfer = Fluid25DTransportStudyFaceTransfer;
constexpr double kRoundoff = 1.0e-12;

double minmod(double left, double right) {
    if (left > 0.0 && right > 0.0) {
        return std::min(left, right);
    }
    if (left < 0.0 && right < 0.0) {
        return std::max(left, right);
    }
    return 0.0;
}

double velocity(const Cell& cell, bool y, double minimum_wet_depth) {
    return cell.depth_m > minimum_wet_depth
               ? (y ? cell.momentum_y_m2_per_s : cell.momentum_x_m2_per_s) / cell.depth_m
               : 0.0;
}

double concentration(const Cell& cell) {
    return cell.depth_m > 0.0 ? cell.tracer_q_m / cell.depth_m : 0.0;
}

void check_cell(Cell& cell, double minimum_wet_depth) {
    if (!std::isfinite(cell.depth_m) || cell.depth_m < -kRoundoff ||
        !std::isfinite(cell.momentum_x_m2_per_s) || !std::isfinite(cell.momentum_y_m2_per_s) ||
        !std::isfinite(cell.tracer_q_m) || cell.tracer_q_m < -kRoundoff ||
        cell.tracer_q_m > cell.depth_m + kRoundoff) {
        throw std::runtime_error("CPU transport study produced invalid water/momentum/tracer");
    }
    cell.depth_m = std::max(0.0, cell.depth_m);
    cell.tracer_q_m = std::clamp(cell.tracer_q_m, 0.0, cell.depth_m);
    if (cell.depth_m <= minimum_wet_depth) {
        cell.momentum_x_m2_per_s = 0.0;
        cell.momentum_y_m2_per_s = 0.0;
    }
}

double total(const std::vector<Cell>& cells, double cell_size, bool tracer) {
    double sum = 0.0;
    for (const Cell& cell : cells) {
        sum += tracer ? cell.tracer_q_m : cell.depth_m;
    }
    return sum * cell_size * cell_size;
}

struct Reconstruction {
    std::array<Cell, 4> faces{};
    std::array<double, 4> bed{};
};

std::vector<Reconstruction> reconstruct(const std::vector<Cell>& cells,
                                        const Fluid25DScenarioData& scenario,
                                        Fluid25DTransportStudyBoundary boundary,
                                        double minimum_wet_depth) {
    std::vector<Reconstruction> result(cells.size());
    for (std::uint32_t y = 0; y < scenario.height; ++y) {
        for (std::uint32_t x = 0; x < scenario.width; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * scenario.width + x;
            const Cell& center = cells[i];
            const double bed = scenario.terrain_height_m[i];
            Reconstruction& output = result[i];
            output.faces.fill(center);
            output.bed.fill(bed);
            // A zero-volume cell cannot expose a fictitious wet subcell.
            // This also preserves the existing cellwise wet/dry lake contract.
            if (center.depth_m == 0.0) {
                continue;
            }
            for (std::size_t axis = 0; axis < 2; ++axis) {
                std::size_t left = i;
                std::size_t right = i;
                if (axis == 0) {
                    if (x > 0) {
                        left = i - 1;
                    } else if (boundary == Fluid25DTransportStudyBoundary::PeriodicX) {
                        left = i + scenario.width - 1;
                    }
                    if (x + 1 < scenario.width) {
                        right = i + 1;
                    } else if (boundary == Fluid25DTransportStudyBoundary::PeriodicX) {
                        right = i - scenario.width + 1;
                    }
                } else {
                    if (y > 0) {
                        left = i - scenario.width;
                    }
                    if (y + 1 < scenario.height) {
                        right = i + scenario.width;
                    }
                }
                const double dh = 0.5 * minmod(center.depth_m - cells[left].depth_m,
                                               cells[right].depth_m - center.depth_m);
                // Never form an absolute float h+z. The input bed and its
                // per-cell mean stay unchanged; reconstructed bed = eta-h.
                const double deta = 0.5 * minmod((center.depth_m - cells[left].depth_m) +
                                                     (bed - scenario.terrain_height_m[left]),
                                                 (cells[right].depth_m - center.depth_m) +
                                                     (scenario.terrain_height_m[right] - bed));
                const double db = deta - dh;
                const double u = velocity(center, false, minimum_wet_depth);
                const double v = velocity(center, true, minimum_wet_depth);
                const double du =
                    0.5 * minmod(u - velocity(cells[left], false, minimum_wet_depth),
                                 velocity(cells[right], false, minimum_wet_depth) - u);
                const double dv = 0.5 * minmod(v - velocity(cells[left], true, minimum_wet_depth),
                                               velocity(cells[right], true, minimum_wet_depth) - v);
                for (std::size_t side = 0; side < 2; ++side) {
                    const double sign = side == 0 ? -1.0 : 1.0;
                    const double h = center.depth_m + sign * dh;
                    // Opposite-face weighting preserves the cell mean hu/hv.
                    const double opposite_fraction = (center.depth_m - sign * dh) / center.depth_m;
                    const std::size_t face = axis * 2 + side;
                    output.faces[face] = {
                        .depth_m = h,
                        .momentum_x_m2_per_s = h * (u + sign * opposite_fraction * du),
                        .momentum_y_m2_per_s = h * (v + sign * opposite_fraction * dv),
                        .tracer_q_m = h * concentration(center),
                    };
                    output.bed[face] = bed + sign * db;
                    if (!std::isfinite(output.bed[face]) || !std::isfinite(h) || h < 0.0 ||
                        !std::isfinite(output.faces[face].momentum_x_m2_per_s) ||
                        !std::isfinite(output.faces[face].momentum_y_m2_per_s) ||
                        !std::isfinite(output.faces[face].tracer_q_m)) {
                        throw std::runtime_error("CPU transport study reconstruction is invalid");
                    }
                }
            }
        }
    }
    return result;
}

struct FaceFlux {
    Cell flux{};
    double left_pressure = 0.0;
    double right_pressure = 0.0;
    bool clipped = false;
};

FaceFlux face_flux(Cell left, Cell right, double left_bed, double right_bed, double nx, double ny,
                   double gravity) {
    const double left_h = left.depth_m;
    const double right_h = right.depth_m;
    const double left_star = std::max(0.0, left_h - std::max(0.0, right_bed - left_bed));
    const double right_star = std::max(0.0, right_h - std::max(0.0, left_bed - right_bed));
    const auto scale = [](Cell& cell, double h) {
        const double fraction = cell.depth_m > 0.0 ? h / cell.depth_m : 0.0;
        cell.momentum_x_m2_per_s *= fraction;
        cell.momentum_y_m2_per_s *= fraction;
        cell.depth_m = h;
    };
    scale(left, left_star);
    scale(right, right_star);
    const auto flux = [nx, ny, gravity](const Cell& cell) {
        const double speed =
            cell.depth_m > 0.0
                ? (cell.momentum_x_m2_per_s * nx + cell.momentum_y_m2_per_s * ny) / cell.depth_m
                : 0.0;
        const double pressure = 0.5 * gravity * cell.depth_m * cell.depth_m;
        return Cell{cell.depth_m * speed, cell.momentum_x_m2_per_s * speed + pressure * nx,
                    cell.momentum_y_m2_per_s * speed + pressure * ny, 0.0};
    };
    const auto wave = [nx, ny, gravity](const Cell& cell) {
        const double speed =
            cell.depth_m > 0.0
                ? (cell.momentum_x_m2_per_s * nx + cell.momentum_y_m2_per_s * ny) / cell.depth_m
                : 0.0;
        return std::abs(speed) + std::sqrt(gravity * cell.depth_m);
    };
    const Cell fl = flux(left);
    const Cell fr = flux(right);
    const double a = std::max(wave(left), wave(right));
    return {
        .flux = {0.5 * (fl.depth_m + fr.depth_m) - 0.5 * a * (right.depth_m - left.depth_m),
                 0.5 * (fl.momentum_x_m2_per_s + fr.momentum_x_m2_per_s) -
                     0.5 * a * (right.momentum_x_m2_per_s - left.momentum_x_m2_per_s),
                 0.5 * (fl.momentum_y_m2_per_s + fr.momentum_y_m2_per_s) -
                     0.5 * a * (right.momentum_y_m2_per_s - left.momentum_y_m2_per_s),
                 0.0},
        .left_pressure = 0.5 * gravity * (left_h * left_h - left_star * left_star),
        .right_pressure = 0.5 * gravity * (right_h * right_h - right_star * right_star),
        .clipped = (left_h > 0.0 && left_star == 0.0) || (right_h > 0.0 && right_star == 0.0),
    };
}

// Bounded first-order fallback: Berthon/Foucher eq. (5.11), on Cartesian
// cells. H and X are numerical free-surface variables, not modified terrain.
FaceFlux free_surface_flux(const Cell& left, const Cell& right, double left_bed, double right_bed,
                           double nx, double ny, double gravity, double dt_over_dx,
                           bool right_active) {
    const double hl = left.depth_m + left_bed;
    const double hr = right.depth_m + right_bed;
    const double xl = left.depth_m / hl;
    const double xr = right.depth_m / hr;
    const Cell wl{hl, left.depth_m > 0.0 ? hl * left.momentum_x_m2_per_s / left.depth_m : 0.0,
                  left.depth_m > 0.0 ? hl * left.momentum_y_m2_per_s / left.depth_m : 0.0, 0.0};
    const Cell wr{hr, right.depth_m > 0.0 ? hr * right.momentum_x_m2_per_s / right.depth_m : 0.0,
                  right.depth_m > 0.0 ? hr * right.momentum_y_m2_per_s / right.depth_m : 0.0, 0.0};
    const auto physical = [nx, ny, gravity](const Cell& state) {
        const double un =
            (state.momentum_x_m2_per_s * nx + state.momentum_y_m2_per_s * ny) / state.depth_m;
        const double pressure = 0.5 * gravity * state.depth_m * state.depth_m;
        return Cell{state.depth_m * un, state.momentum_x_m2_per_s * un + pressure * nx,
                    state.momentum_y_m2_per_s * un + pressure * ny, 0.0};
    };
    const auto wave = [nx, ny, gravity](const Cell& state) {
        return std::abs((state.momentum_x_m2_per_s * nx + state.momentum_y_m2_per_s * ny) /
                        state.depth_m) +
               std::sqrt(gravity * state.depth_m);
    };
    const auto fl = physical(wl);
    const auto fr = physical(wr);
    const double a = std::max(wave(wl), wave(wr));
    const Cell raw{0.5 * (fl.depth_m + fr.depth_m) - 0.5 * a * (hr - hl),
                   0.5 * (fl.momentum_x_m2_per_s + fr.momentum_x_m2_per_s) -
                       0.5 * a * (wr.momentum_x_m2_per_s - wl.momentum_x_m2_per_s),
                   0.5 * (fl.momentum_y_m2_per_s + fr.momentum_y_m2_per_s) -
                       0.5 * a * (wr.momentum_y_m2_per_s - wl.momentum_y_m2_per_s),
                   0.0};
    // On a square cell |face|/|cell-center triangle| = 4/dx.
    // Check the additional homogeneous-H outflow restriction, eq. (5.13),
    // on each active side. Exterior ghosts are not evolved cells.
    const double left_out = std::max(0.0, raw.depth_m) - std::min(0.0, fl.depth_m);
    const double right_out = std::max(0.0, -raw.depth_m) - std::min(0.0, -fr.depth_m);
    if (!std::isfinite(raw.depth_m) || !std::isfinite(raw.momentum_x_m2_per_s) ||
        !std::isfinite(raw.momentum_y_m2_per_s) || 4.0 * dt_over_dx * left_out > hl ||
        (right_active && 4.0 * dt_over_dx * right_out > hr)) {
        throw std::runtime_error("CPU free-surface study violates homogeneous-H draining bound");
    }
    const bool donor_left = raw.depth_m > 0.0;
    const double xf = donor_left ? xl : xr;
    const double hf = donor_left ? hl : hr;
    return {
        .flux = {xf * raw.depth_m, xf * raw.momentum_x_m2_per_s, xf * raw.momentum_y_m2_per_s, 0.0},
        // apply() subtracts/ adds these with the outward/ inward normal.
        // These signed correction fields encode the matched cell source.
        .left_pressure = -0.5 * gravity * hl * hf * (xf - xl),
        .right_pressure = -0.5 * gravity * hr * hf * (xf - xr),
        .clipped = false,
    };
}

struct EulerResult {
    std::vector<Cell> cells{};
    std::vector<Transfer> transfers{};
    double cfl = 0.0;
    std::uint64_t clipped_faces = 0;
    double boundary_water_m3 = 0.0;
    double boundary_tracer_m3 = 0.0;
};

EulerResult euler(const std::vector<Cell>& cells, const Fluid25DConfig& config,
                  const Fluid25DScenarioData& scenario, Fluid25DTransportStudyBoundary boundary,
                  Fluid25DTransportStudyMethod method, double minimum_bed, double dt) {
    const double dx = config.cell_size_m;
    const double gravity = config.gravity_m_per_s2;
    const double minimum_wet = config.minimum_wet_depth_m;
    const bool fallback = method == Fluid25DTransportStudyMethod::FreeSurfaceUpwind;
    std::vector<Reconstruction> recon;
    if (fallback) {
        recon.resize(cells.size());
        for (std::size_t i = 0; i < cells.size(); ++i) {
            recon[i].faces.fill(cells[i]);
            // Difference first: high absolute elevations must not consume
            // the positive datum padding or the shallow water film.
            recon[i].bed.fill((static_cast<double>(scenario.terrain_height_m[i]) - minimum_bed) +
                              Fluid25DTransportStudy::kFallbackDatumPaddingM);
        }
    } else {
        recon = reconstruct(cells, scenario, boundary, minimum_wet);
    }
    EulerResult result;
    result.cells = cells;
    double ax = 0.0;
    double ay = 0.0;
    for (const auto& cell : recon) {
        for (std::size_t face = 0; face < 4; ++face) {
            const Cell& state = cell.faces[face];
            const double speed =
                state.depth_m > 0.0
                    ? std::abs(face < 2 ? state.momentum_x_m2_per_s : state.momentum_y_m2_per_s) /
                          state.depth_m
                    : 0.0;
            const double wave_depth = state.depth_m + (fallback ? cell.bed[face] : 0.0);
            const double a = speed + std::sqrt(gravity * wave_depth);
            if (face < 2) {
                ax = std::max(ax, a);
            } else {
                ay = std::max(ay, a);
            }
        }
    }
    result.cfl = dt / dx * (ax + ay);
    if (!std::isfinite(result.cfl) || result.cfl > Fluid25DTransportStudy::kTargetCfl) {
        throw std::runtime_error("CPU transport study reconstructed-stage CFL exceeds 0.225");
    }
    if (fallback && dt / dx * std::max(ax, ay) > Fluid25DTransportStudy::kFallbackAxisCfl) {
        throw std::runtime_error("CPU free-surface study violates H-wave axis CFL 0.125");
    }

    const auto apply = [&](std::size_t left, std::size_t right, FaceFlux face, double nx, double ny,
                           bool open) {
        const double c = face.flux.depth_m >= 0.0 || right == kFluid25DNoCell
                             ? concentration(cells[left])
                             : concentration(cells[right]);
        face.flux.tracer_q_m = face.flux.depth_m * c;
        const auto add = [&](std::size_t i, double sign, double pressure) {
            Cell& output = result.cells[i];
            output.depth_m += sign * dt / dx * face.flux.depth_m;
            output.momentum_x_m2_per_s +=
                sign * dt / dx * (face.flux.momentum_x_m2_per_s + pressure * nx);
            output.momentum_y_m2_per_s +=
                sign * dt / dx * (face.flux.momentum_y_m2_per_s + pressure * ny);
            output.tracer_q_m += sign * dt / dx * face.flux.tracer_q_m;
        };
        add(left, -1.0, face.left_pressure);
        if (right != kFluid25DNoCell) {
            add(right, 1.0, face.right_pressure);
        }
        const double volume = dt * dx * face.flux.depth_m;
        const double tracer = dt * dx * face.flux.tracer_q_m;
        result.transfers.push_back({left, right, nx, ny, volume, tracer});
        if (open) {
            result.boundary_water_m3 += volume;
            result.boundary_tracer_m3 += tracer;
        }
        result.clipped_faces += face.clipped ? 1U : 0U;
    };
    const auto internal = [&](std::size_t left, std::size_t right, std::size_t axis) {
        const std::size_t lf = axis * 2 + 1;
        const std::size_t rf = axis * 2;
        const double nx = axis == 0 ? 1.0 : 0.0;
        const double ny = axis == 1 ? 1.0 : 0.0;
        const auto flux =
            fallback ? free_surface_flux(recon[left].faces[lf], recon[right].faces[rf],
                                         recon[left].bed[lf], recon[right].bed[rf], nx, ny, gravity,
                                         dt / dx, true)
                     : face_flux(recon[left].faces[lf], recon[right].faces[rf], recon[left].bed[lf],
                                 recon[right].bed[rf], nx, ny, gravity);
        apply(left, right, flux, nx, ny, false);
    };
    const auto edge = [&](std::size_t i, std::size_t f, double nx, double ny) {
        const Cell& interior = recon[i].faces[f];
        const bool open = (scenario.boundary_outflow_face_mask[i] & (1U << f)) != 0;
        Cell exterior{};
        if (!open) {
            exterior = interior;
            const double normal_momentum =
                interior.momentum_x_m2_per_s * nx + interior.momentum_y_m2_per_s * ny;
            exterior.momentum_x_m2_per_s -= 2.0 * normal_momentum * nx;
            exterior.momentum_y_m2_per_s -= 2.0 * normal_momentum * ny;
        }
        FaceFlux flux =
            fallback
                ? free_surface_flux(interior, exterior, recon[i].bed[f], recon[i].bed[f], nx, ny,
                                    gravity, dt / dx, false)
                : face_flux(interior, exterior, recon[i].bed[f], recon[i].bed[f], nx, ny, gravity);
        if ((!open && std::abs(flux.flux.depth_m) > kRoundoff) ||
            (open && flux.flux.depth_m < -kRoundoff)) {
            throw std::runtime_error("CPU transport study boundary violated no-inflow contract");
        }
        flux.flux.depth_m = open ? std::max(0.0, flux.flux.depth_m) : 0.0;
        apply(i, kFluid25DNoCell, flux, nx, ny, open);
    };
    for (std::uint32_t y = 0; y < scenario.height; ++y) {
        const std::size_t row = static_cast<std::size_t>(y) * scenario.width;
        for (std::uint32_t x = 0; x + 1 < scenario.width; ++x) {
            internal(row + x, row + x + 1, 0);
        }
        if (boundary == Fluid25DTransportStudyBoundary::PeriodicX) {
            internal(row + scenario.width - 1, row, 0);
        } else {
            edge(row, 0, -1.0, 0.0);
            edge(row + scenario.width - 1, 1, 1.0, 0.0);
        }
    }
    for (std::uint32_t y = 0; y + 1 < scenario.height; ++y) {
        for (std::uint32_t x = 0; x < scenario.width; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * scenario.width + x;
            internal(i, i + scenario.width, 1);
        }
    }
    for (std::uint32_t x = 0; x < scenario.width; ++x) {
        edge(x, 2, 0.0, -1.0);
        edge(static_cast<std::size_t>(scenario.height - 1) * scenario.width + x, 3, 0.0, 1.0);
    }
    for (std::size_t i = 0; i < cells.size(); ++i) {
        if (fallback) {
            check_cell(result.cells[i], minimum_wet);
            continue;
        }
        // Matched cell source uses the same reconstructed faces as pressure
        // fluxes. This term vanishes in the production first-order method.
        result.cells[i].momentum_x_m2_per_s -=
            dt / dx * gravity * 0.5 * (recon[i].faces[0].depth_m + recon[i].faces[1].depth_m) *
            (recon[i].bed[1] - recon[i].bed[0]);
        result.cells[i].momentum_y_m2_per_s -=
            dt / dx * gravity * 0.5 * (recon[i].faces[2].depth_m + recon[i].faces[3].depth_m) *
            (recon[i].bed[3] - recon[i].bed[2]);
        check_cell(result.cells[i], minimum_wet);
    }
    return result;
}

void forcing(std::vector<Cell>& cells, const Fluid25DConfig& config,
             const Fluid25DScenarioData& scenario, double dt, double source_scale,
             double source_concentration, Fluid25DTracerStepResult& ledger) {
    const double area = static_cast<double>(config.cell_size_m) * config.cell_size_m;
    const double drag = std::exp(-static_cast<double>(config.flow_damping_per_second) * dt);
    for (std::size_t i = 0; i < cells.size(); ++i) {
        Cell& cell = cells[i];
        const double input = scenario.source_depth_rate_m_per_s[i] * source_scale * dt;
        cell.depth_m += input;
        cell.tracer_q_m += input * source_concentration;
        ledger.water.source_volume_m3 += input * area;
        ledger.tracer.source_amount_m3 += input * source_concentration * area;
        const double removed = std::min(cell.depth_m, scenario.sink_depth_rate_m_per_s[i] * dt);
        const double fraction = cell.depth_m > 0.0 ? removed / cell.depth_m : 0.0;
        ledger.water.sink_volume_m3 += removed * area;
        ledger.tracer.sink_amount_m3 += cell.tracer_q_m * fraction * area;
        cell.depth_m -= removed;
        cell.tracer_q_m *= 1.0 - fraction;
        cell.momentum_x_m2_per_s *= (1.0 - fraction) * drag;
        cell.momentum_y_m2_per_s *= (1.0 - fraction) * drag;
        check_cell(cell, config.minimum_wet_depth_m);
    }
}

} // namespace

Fluid25DTransportStudy::Fluid25DTransportStudy(Fluid25DConfig config, Fluid25DScenarioData scenario,
                                               Fluid25DTransportStudyBoundary boundary,
                                               std::vector<Fluid25DMomentum> initial_momentum,
                                               std::vector<double> initial_tracer_q_m,
                                               Fluid25DTransportStudyMethod method)
    : config_(std::move(config)), scenario_(std::move(scenario)), boundary_(boundary),
      method_(method), initial_momentum_(std::move(initial_momentum)),
      initial_tracer_q_m_(std::move(initial_tracer_q_m)) {
    validate_fluid_25d_config(config_);
    if (boundary_ != Fluid25DTransportStudyBoundary::ScenarioFaces &&
        boundary_ != Fluid25DTransportStudyBoundary::PeriodicX) {
        throw std::runtime_error("CPU transport study boundary mode is invalid");
    }
    if (method_ != Fluid25DTransportStudyMethod::ReconstructedHydrostatic &&
        method_ != Fluid25DTransportStudyMethod::FreeSurfaceUpwind) {
        throw std::runtime_error("CPU transport study method is invalid");
    }
    const std::size_t count = fluid_25d_cell_count(config_);
    if (config_.solver != Fluid25DSolver::FiniteVolume || scenario_.width != config_.grid_width ||
        scenario_.height != config_.grid_height || scenario_.cell_size_m != config_.cell_size_m ||
        scenario_.terrain_height_m.size() != count ||
        scenario_.initial_water_depth_m.size() != count ||
        scenario_.source_depth_rate_m_per_s.size() != count ||
        scenario_.sink_depth_rate_m_per_s.size() != count ||
        scenario_.boundary_outflow_face_mask.size() != count ||
        (!initial_momentum_.empty() && initial_momentum_.size() != count) ||
        (!initial_tracer_q_m_.empty() && initial_tracer_q_m_.size() != count)) {
        throw std::runtime_error("CPU transport study config/scenario fields do not match");
    }
    validate_fluid_25d_boundary_outflow_face_mask(config_.grid_width, config_.grid_height,
                                                  scenario_.boundary_outflow_face_mask);
    if (boundary_ == Fluid25DTransportStudyBoundary::PeriodicX) {
        for (const auto mask : scenario_.boundary_outflow_face_mask) {
            if ((mask & (kFluid25DBoundaryOutflowLeft | kFluid25DBoundaryOutflowRight)) != 0) {
                throw std::runtime_error("CPU periodic-X study cannot also open x boundaries");
            }
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::isfinite(scenario_.terrain_height_m[i]) ||
            !std::isfinite(scenario_.initial_water_depth_m[i]) ||
            scenario_.initial_water_depth_m[i] < 0.0F ||
            !std::isfinite(scenario_.source_depth_rate_m_per_s[i]) ||
            scenario_.source_depth_rate_m_per_s[i] < 0.0F ||
            !std::isfinite(scenario_.sink_depth_rate_m_per_s[i]) ||
            scenario_.sink_depth_rate_m_per_s[i] < 0.0F) {
            throw std::runtime_error("CPU transport study scenario is invalid");
        }
    }
    minimum_bed_m_ =
        *std::min_element(scenario_.terrain_height_m.begin(), scenario_.terrain_height_m.end());
    reset();
}

void Fluid25DTransportStudy::reset() {
    std::vector<Cell> initial(scenario_.terrain_height_m.size());
    for (std::size_t i = 0; i < initial.size(); ++i) {
        initial[i].depth_m = scenario_.initial_water_depth_m[i];
        if (!initial_momentum_.empty()) {
            initial[i].momentum_x_m2_per_s = initial_momentum_[i].x_m2_per_s;
            initial[i].momentum_y_m2_per_s = initial_momentum_[i].y_m2_per_s;
        }
        if (!initial_tracer_q_m_.empty()) {
            initial[i].tracer_q_m = initial_tracer_q_m_[i];
        }
        check_cell(initial[i], config_.minimum_wet_depth_m);
    }
    cells_ = std::move(initial);
    last_face_transfers_.clear();
    last_cfl_ = 0.0;
    last_clipped_faces_ = 0;
    dye_schedule_.reset();
}

double Fluid25DTransportStudy::total_water_volume_m3() const {
    return total(cells_, config_.cell_size_m, false);
}

double Fluid25DTransportStudy::total_tracer_amount_m3() const {
    return total(cells_, config_.cell_size_m, true);
}

Fluid25DTracerStepResult Fluid25DTransportStudy::step_with_dye(double source_rate_scale) {
    return step(source_rate_scale, dye_schedule_.source_concentration(config_));
}

Fluid25DTracerStepResult Fluid25DTransportStudy::step(double source_rate_scale,
                                                      double source_concentration) {
    if (!std::isfinite(source_rate_scale) || source_rate_scale < 0.0 ||
        !std::isfinite(source_concentration) || source_concentration < 0.0 ||
        source_concentration > 1.0) {
        throw std::runtime_error("CPU transport study source controls are invalid");
    }
    // The entire public step, including forcing, both RK stages, all substeps,
    // diagnostics and the dye clock, is published only after full acceptance.
    auto working = cells_;
    std::vector<Transfer> transfers;
    Fluid25DTracerStepResult result;
    result.water.volume_before_m3 = total_water_volume_m3();
    result.tracer.amount_before_m3 = total_tracer_amount_m3();
    double max_cfl = 0.0;
    std::uint64_t clipped_faces = 0;
    const double dt =
        static_cast<double>(config_.fixed_delta_seconds) / config_.simulation_substeps;
    for (std::uint32_t substep = 0; substep < config_.simulation_substeps; ++substep) {
        forcing(working, config_, scenario_, dt * 0.5, source_rate_scale, source_concentration,
                result);
        const auto initial = working;
        const auto checked_euler = [&](const std::vector<Cell>& input, unsigned int stage) {
            try {
                return euler(input, config_, scenario_, boundary_, method_, minimum_bed_m_, dt);
            } catch (const std::exception& error) {
                throw std::runtime_error("CPU transport study substep " + std::to_string(substep) +
                                         " RK stage " + std::to_string(stage) + ": " +
                                         error.what());
            }
        };
        const EulerResult first = checked_euler(initial, 1U);
        const EulerResult second = checked_euler(first.cells, 2U);
        for (std::size_t i = 0; i < working.size(); ++i) {
            working[i] = {
                0.5 * (initial[i].depth_m + second.cells[i].depth_m),
                0.5 * (initial[i].momentum_x_m2_per_s + second.cells[i].momentum_x_m2_per_s),
                0.5 * (initial[i].momentum_y_m2_per_s + second.cells[i].momentum_y_m2_per_s),
                0.5 * (initial[i].tracer_q_m + second.cells[i].tracer_q_m),
            };
            check_cell(working[i], config_.minimum_wet_depth_m);
        }
        if (transfers.empty()) {
            transfers = first.transfers;
            for (auto& transfer : transfers) {
                transfer.water_volume_m3 = 0.0;
                transfer.tracer_amount_m3 = 0.0;
            }
        }
        for (std::size_t f = 0; f < transfers.size(); ++f) {
            transfers[f].water_volume_m3 +=
                0.5 * (first.transfers[f].water_volume_m3 + second.transfers[f].water_volume_m3);
            transfers[f].tracer_amount_m3 +=
                0.5 * (first.transfers[f].tracer_amount_m3 + second.transfers[f].tracer_amount_m3);
        }
        result.water.boundary_outflow_volume_m3 +=
            0.5 * (first.boundary_water_m3 + second.boundary_water_m3);
        result.tracer.boundary_outflow_amount_m3 +=
            0.5 * (first.boundary_tracer_m3 + second.boundary_tracer_m3);
        max_cfl = std::max({max_cfl, first.cfl, second.cfl});
        clipped_faces += first.clipped_faces + second.clipped_faces;
        forcing(working, config_, scenario_, dt * 0.5, source_rate_scale, source_concentration,
                result);
    }
    result.water.volume_after_m3 = total(working, config_.cell_size_m, false);
    result.tracer.amount_after_m3 = total(working, config_.cell_size_m, true);
    auto accepted_schedule = dye_schedule_;
    accepted_schedule.advance_fixed_step();
    cells_ = std::move(working);
    last_face_transfers_ = std::move(transfers);
    last_cfl_ = max_cfl;
    last_clipped_faces_ = clipped_faces;
    dye_schedule_ = accepted_schedule;
    return result;
}

} // namespace cubey::projects::fluid::fluid_25d
