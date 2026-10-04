#include "fluid_25d_geometry_study.h"

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
        throw std::runtime_error("CPU geometry study produced invalid water/momentum/tracer");
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

using Bed = Fluid25DGeometryStudyBedCell;

// Bilinear corner geometry derived from center point samples. It does not
// retain their values as effective finite-volume bed means.
std::vector<Bed> build_geometry(const Fluid25DScenarioData& scenario,
                                Fluid25DTransportStudyBoundary boundary, double datum) {
    const int width = static_cast<int>(scenario.width);
    const int height = static_cast<int>(scenario.height);
    const auto extended_row = [&](int x, int y) {
        const auto raw = [&](int xx) {
            return static_cast<double>(
                       scenario.terrain_height_m[static_cast<std::size_t>(y) * scenario.width +
                                                 static_cast<std::size_t>(xx)]) -
                   datum;
        };
        if (boundary == Fluid25DTransportStudyBoundary::PeriodicX) {
            return raw((x % width + width) % width);
        }
        if (width == 1)
            return raw(0);
        if (x < 0)
            return 2.0 * raw(0) - raw(1);
        if (x >= width)
            return 2.0 * raw(width - 1) - raw(width - 2);
        return raw(x);
    };
    const auto extended = [&](int x, int y) {
        if (height == 1)
            return extended_row(x, 0);
        if (y < 0)
            return 2.0 * extended_row(x, 0) - extended_row(x, 1);
        if (y >= height) {
            return 2.0 * extended_row(x, height - 1) - extended_row(x, height - 2);
        }
        return extended_row(x, y);
    };
    const auto corner_index = [width](int x, int y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(width + 1) +
               static_cast<std::size_t>(x);
    };
    std::vector<double> corners(static_cast<std::size_t>(width + 1) *
                                static_cast<std::size_t>(height + 1));
    for (int y = 0; y <= height; ++y) {
        for (int x = 0; x <= width; ++x) {
            corners[corner_index(x, y)] = 0.25 * (extended(x - 1, y - 1) + extended(x, y - 1) +
                                                  extended(x - 1, y) + extended(x, y));
        }
    }
    std::vector<Bed> result(scenario.terrain_height_m.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Bed& b =
                result[static_cast<std::size_t>(y) * scenario.width + static_cast<std::size_t>(x)];
            b.corner_bed_m = {corners[corner_index(x, y)], corners[corner_index(x + 1, y)],
                              corners[corner_index(x, y + 1)], corners[corner_index(x + 1, y + 1)]};
            const auto& c = b.corner_bed_m;
            b.mean_bed_m = 0.25 * (c[0] + c[1] + c[2] + c[3]);
            b.face_bed_m = {0.5 * (c[0] + c[2]), 0.5 * (c[1] + c[3]), 0.5 * (c[0] + c[1]),
                            0.5 * (c[2] + c[3])};
        }
    }
    return result;
}

double limited(double left, double right) {
    return minmod(minmod(1.3 * left, 0.5 * (left + right)), 1.3 * right);
}

struct Reconstruction {
    std::array<Cell, 4> faces{};
    std::array<double, 4> bed{};
};

std::vector<Reconstruction> reconstruct(const std::vector<Cell>& cells,
                                        const Fluid25DScenarioData& scenario,
                                        const std::vector<Bed>& geometry,
                                        Fluid25DTransportStudyBoundary boundary,
                                        double minimum_wet_depth, std::uint64_t& corrected) {
    std::vector<Reconstruction> result(cells.size());
    for (std::uint32_t y = 0; y < scenario.height; ++y) {
        for (std::uint32_t x = 0; x < scenario.width; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * scenario.width + x;
            const Cell& center = cells[i];
            const double b = geometry[i].mean_bed_m;
            const double wi = b + center.depth_m;
            const double u = velocity(center, false, minimum_wet_depth);
            const double v = velocity(center, true, minimum_wet_depth);
            Reconstruction& out = result[i];
            out.bed = geometry[i].face_bed_m;
            for (std::size_t axis = 0; axis < 2; ++axis) {
                std::size_t left = i;
                std::size_t right = i;
                if (axis == 0) {
                    if (x > 0)
                        left = i - 1;
                    else if (boundary == Fluid25DTransportStudyBoundary::PeriodicX)
                        left = i + scenario.width - 1;
                    if (x + 1 < scenario.width)
                        right = i + 1;
                    else if (boundary == Fluid25DTransportStudyBoundary::PeriodicX)
                        right = i - scenario.width + 1;
                } else {
                    if (y > 0)
                        left = i - scenario.width;
                    if (y + 1 < scenario.height)
                        right = i + scenario.width;
                }
                const std::size_t a = 2 * axis;
                double dw = 0.5 * limited((center.depth_m - cells[left].depth_m) +
                                              (b - geometry[left].mean_bed_m),
                                          (cells[right].depth_m - center.depth_m) +
                                              (geometry[right].mean_bed_m - b));
                double du = 0.5 * limited(u - velocity(cells[left], false, minimum_wet_depth),
                                          velocity(cells[right], false, minimum_wet_depth) - u);
                double dv = 0.5 * limited(v - velocity(cells[left], true, minimum_wet_depth),
                                          velocity(cells[right], true, minimum_wet_depth) - v);
                // Paper eq. 2.8, separately in each axis. A dry interior has
                // eta following the prescribed bed and zero reconstructed h.
                const bool dry_interior = center.depth_m == 0.0 && cells[left].depth_m == 0.0 &&
                                          cells[right].depth_m == 0.0;
                if (dry_interior) {
                    dw = 0.5 * (out.bed[a + 1] - out.bed[a]);
                    du = dv = 0.0;
                }
                std::array<double, 2> eta{wi - dw, wi + dw};
                std::array<double, 2> h{center.depth_m - dw + (b - out.bed[a]),
                                        center.depth_m + dw + (b - out.bed[a + 1])};
                if (dry_interior)
                    h = {0.0, 0.0};
                // BSGM eq. 2.15-2.22: paired eta/bed correction preserves
                // their cell means; it is not a depth clamp or terrain edit.
                for (std::size_t side = 0; side < 2; ++side) {
                    if (h[side] < 0.0) {
                        const std::size_t other = 1 - side;
                        const double z = wi + minmod(eta[side] - wi, out.bed[a + side] - wi);
                        eta[side] = out.bed[a + side] = z;
                        eta[other] = 2.0 * wi - z;
                        out.bed[a + other] = 2.0 * b - z;
                        h[side] = 0.0;
                        h[other] = 2.0 * center.depth_m;
                        ++corrected;
                        break;
                    }
                }
                for (std::size_t side = 0; side < 2; ++side) {
                    const double sign = side == 0 ? -1.0 : 1.0;
                    out.faces[a + side] = {h[side], h[side] * (u + sign * du),
                                           h[side] * (v + sign * dv),
                                           h[side] * concentration(center)};
                    const Cell& face = out.faces[a + side];
                    if (!std::isfinite(out.bed[a + side]) || !std::isfinite(h[side]) ||
                        h[side] < 0.0 || !std::isfinite(face.momentum_x_m2_per_s) ||
                        !std::isfinite(face.momentum_y_m2_per_s)) {
                        throw std::runtime_error("CPU geometry study reconstruction is invalid");
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

FaceFlux face_flux(Cell left, Cell right, double bl, double br, double nx, double ny, double g) {
    const double hl = left.depth_m;
    const double hr = right.depth_m;
    double ls = hl;
    double rs = hr;
    double zs = bl;
    if (bl != br) {
        // Chen/Noelle 2017 eq. 2.15-2.18, referenced by BSGM sec. 3.
        zs = std::min({bl + hl, br + hr, std::max(bl, br)});
        ls = std::max(0.0, std::min((bl - zs) + hl, hl));
        rs = std::max(0.0, std::min((br - zs) + hr, hr));
    }
    const auto scale = [](Cell& c, double h) {
        const double ratio = c.depth_m > 0.0 ? h / c.depth_m : 0.0;
        c.momentum_x_m2_per_s *= ratio;
        c.momentum_y_m2_per_s *= ratio;
        c.depth_m = h;
    };
    scale(left, ls);
    scale(right, rs);
    const auto un = [nx, ny](const Cell& c) {
        return c.depth_m > 0.0
                   ? (c.momentum_x_m2_per_s * nx + c.momentum_y_m2_per_s * ny) / c.depth_m
                   : 0.0;
    };
    const auto physical = [nx, ny, g, &un](const Cell& c) {
        const double speed = un(c);
        const double p = 0.5 * g * c.depth_m * c.depth_m;
        return Cell{c.depth_m * speed, c.momentum_x_m2_per_s * speed + p * nx,
                    c.momentum_y_m2_per_s * speed + p * ny, 0.0};
    };
    const Cell fl = physical(left);
    const Cell fr = physical(right);
    const double sm = std::min({0.0, un(left) - std::sqrt(g * ls), un(right) - std::sqrt(g * rs)});
    const double sp = std::max({0.0, un(left) + std::sqrt(g * ls), un(right) + std::sqrt(g * rs)});
    const auto hll = [sm, sp](double flv, double frv, double ul, double ur) {
        return sp > sm ? (sp * flv - sm * frv + sm * sp * (ur - ul)) / (sp - sm) : 0.0;
    };
    const bool jump = bl != br;
    return {
        .flux = {hll(fl.depth_m, fr.depth_m, left.depth_m, right.depth_m),
                 hll(fl.momentum_x_m2_per_s, fr.momentum_x_m2_per_s, left.momentum_x_m2_per_s,
                     right.momentum_x_m2_per_s),
                 hll(fl.momentum_y_m2_per_s, fr.momentum_y_m2_per_s, left.momentum_y_m2_per_s,
                     right.momentum_y_m2_per_s),
                 0.0},
        .left_pressure = jump ? 0.5 * g * (hl + ls) * (zs - bl) : 0.0,
        // Right starred depth is intentional; see 2017 eq. 2.18.
        .right_pressure = jump ? 0.5 * g * (hr + rs) * (zs - br) : 0.0,
        .clipped = (hl > 0.0 && ls == 0.0) || (hr > 0.0 && rs == 0.0),
    };
}

struct EulerResult {
    std::vector<Cell> cells{};
    std::vector<Transfer> transfers{};
    Fluid25DGeometryStudyObservation observation{};
    double cfl = 0.0;
    std::uint64_t clipped_faces = 0;
    double boundary_water_m3 = 0.0;
    double boundary_tracer_m3 = 0.0;
};

EulerResult euler(const std::vector<Cell>& cells, const Fluid25DConfig& config,
                  const Fluid25DScenarioData& scenario, const std::vector<Bed>& geometry,
                  Fluid25DTransportStudyBoundary boundary, double datum, double dt) {
    const double dx = config.cell_size_m;
    const double g = config.gravity_m_per_s2;
    const double minimum_wet = config.minimum_wet_depth_m;
    EulerResult result;
    auto& obs = result.observation;
    obs.rhs_per_second.resize(cells.size());
    const auto recon = reconstruct(cells, scenario, geometry, boundary, minimum_wet,
                                   obs.positivity_corrected_axis_pairs);
    double ax = 0.0;
    double ay = 0.0;
    for (std::size_t i = 0; i < recon.size(); ++i) {
        obs.maximum_mean_bed_change_m =
            std::max(obs.maximum_mean_bed_change_m,
                     std::abs(geometry[i].mean_bed_m -
                              (static_cast<double>(scenario.terrain_height_m[i]) - datum)));
        for (std::size_t f = 0; f < 4; ++f) {
            const Cell& s = recon[i].faces[f];
            const double a =
                (s.depth_m > 0.0
                     ? std::abs(f < 2 ? s.momentum_x_m2_per_s : s.momentum_y_m2_per_s) / s.depth_m
                     : 0.0) +
                std::sqrt(g * s.depth_m);
            if (f < 2)
                ax = std::max(ax, a);
            else
                ay = std::max(ay, a);
        }
    }
    result.cfl = dt / dx * (ax + ay);
    if (!std::isfinite(result.cfl) || result.cfl > Fluid25DGeometryStudy::kTargetCfl) {
        throw std::runtime_error("CPU geometry study reconstructed-stage CFL exceeds 0.225");
    }
    const auto apply = [&](std::size_t left, std::size_t right, FaceFlux flux, double nx, double ny,
                           bool open) {
        const double c = flux.flux.depth_m >= 0.0 || right == kFluid25DNoCell
                             ? concentration(cells[left])
                             : concentration(cells[right]);
        flux.flux.tracer_q_m = flux.flux.depth_m * c;
        const auto add = [&](std::size_t i, double sign, double pressure) {
            Cell& rhs = obs.rhs_per_second[i];
            rhs.depth_m += sign / dx * flux.flux.depth_m;
            rhs.momentum_x_m2_per_s += sign / dx * (flux.flux.momentum_x_m2_per_s + pressure * nx);
            rhs.momentum_y_m2_per_s += sign / dx * (flux.flux.momentum_y_m2_per_s + pressure * ny);
            rhs.tracer_q_m += sign / dx * flux.flux.tracer_q_m;
        };
        add(left, -1.0, flux.left_pressure);
        if (right != kFluid25DNoCell)
            add(right, 1.0, flux.right_pressure);
        const double rate = dx * flux.flux.depth_m;
        const double tracer_rate = dx * flux.flux.tracer_q_m;
        obs.face_rates.push_back({left, right, nx, ny, rate, tracer_rate});
        result.transfers.push_back({left, right, nx, ny, dt * rate, dt * tracer_rate});
        if (open) {
            result.boundary_water_m3 += dt * rate;
            result.boundary_tracer_m3 += dt * tracer_rate;
        }
        result.clipped_faces += flux.clipped ? 1U : 0U;
    };
    const auto internal = [&](std::size_t left, std::size_t right, std::size_t axis) {
        const std::size_t lf = axis * 2 + 1;
        const std::size_t rf = axis * 2;
        const double nx = axis == 0 ? 1.0 : 0.0;
        const double ny = axis == 1 ? 1.0 : 0.0;
        obs.maximum_reconstructed_bed_gap_m =
            std::max(obs.maximum_reconstructed_bed_gap_m,
                     std::abs(recon[left].bed[lf] - recon[right].bed[rf]));
        apply(left, right,
              face_flux(recon[left].faces[lf], recon[right].faces[rf], recon[left].bed[lf],
                        recon[right].bed[rf], nx, ny, g),
              nx, ny, false);
    };
    const auto edge = [&](std::size_t i, std::size_t f, double nx, double ny) {
        const Cell& interior = recon[i].faces[f];
        const bool open = (scenario.boundary_outflow_face_mask[i] & (1U << f)) != 0;
        Cell exterior{};
        if (!open) {
            exterior = interior;
            const double qn = interior.momentum_x_m2_per_s * nx + interior.momentum_y_m2_per_s * ny;
            exterior.momentum_x_m2_per_s -= 2.0 * qn * nx;
            exterior.momentum_y_m2_per_s -= 2.0 * qn * ny;
        }
        auto flux = face_flux(interior, exterior, recon[i].bed[f], recon[i].bed[f], nx, ny, g);
        if ((!open && std::abs(flux.flux.depth_m) > kRoundoff) ||
            (open && flux.flux.depth_m < -kRoundoff)) {
            throw std::runtime_error("CPU geometry study boundary violated no-inflow contract");
        }
        flux.flux.depth_m = open ? std::max(0.0, flux.flux.depth_m) : 0.0;
        apply(i, kFluid25DNoCell, flux, nx, ny, open);
    };
    for (std::uint32_t y = 0; y < scenario.height; ++y) {
        const std::size_t row = static_cast<std::size_t>(y) * scenario.width;
        for (std::uint32_t x = 0; x + 1 < scenario.width; ++x)
            internal(row + x, row + x + 1, 0);
        if (boundary == Fluid25DTransportStudyBoundary::PeriodicX)
            internal(row + scenario.width - 1, row, 0);
        else {
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
    result.cells = cells;
    obs.hydrostatic_zero_clipped_faces = result.clipped_faces;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        Cell& rhs = obs.rhs_per_second[i];
        rhs.momentum_x_m2_per_s -= g / dx * 0.5 *
                                   (recon[i].faces[0].depth_m + recon[i].faces[1].depth_m) *
                                   (recon[i].bed[1] - recon[i].bed[0]);
        rhs.momentum_y_m2_per_s -= g / dx * 0.5 *
                                   (recon[i].faces[2].depth_m + recon[i].faces[3].depth_m) *
                                   (recon[i].bed[3] - recon[i].bed[2]);
        if (!std::isfinite(rhs.depth_m) || !std::isfinite(rhs.momentum_x_m2_per_s) ||
            !std::isfinite(rhs.momentum_y_m2_per_s) || !std::isfinite(rhs.tracer_q_m)) {
            throw std::runtime_error("CPU geometry study RHS is invalid");
        }
        if (dt > 0.0) {
            result.cells[i] = {cells[i].depth_m + dt * rhs.depth_m,
                               cells[i].momentum_x_m2_per_s + dt * rhs.momentum_x_m2_per_s,
                               cells[i].momentum_y_m2_per_s + dt * rhs.momentum_y_m2_per_s,
                               cells[i].tracer_q_m + dt * rhs.tracer_q_m};
            check_cell(result.cells[i], minimum_wet);
        }
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

Fluid25DGeometryStudy::Fluid25DGeometryStudy(Fluid25DConfig config, Fluid25DScenarioData scenario,
                                             Fluid25DTransportStudyBoundary boundary,
                                             std::vector<Fluid25DMomentum> initial_momentum,
                                             std::vector<double> initial_tracer_q_m,
                                             Fluid25DGeometryStudyMethod method)
    : config_(std::move(config)), scenario_(std::move(scenario)), boundary_(boundary),
      method_(method), initial_momentum_(std::move(initial_momentum)),
      initial_tracer_q_m_(std::move(initial_tracer_q_m)) {
    validate_fluid_25d_config(config_);
    if (boundary_ != Fluid25DTransportStudyBoundary::ScenarioFaces &&
        boundary_ != Fluid25DTransportStudyBoundary::PeriodicX) {
        throw std::runtime_error("CPU geometry study boundary mode is invalid");
    }
    if (method_ != Fluid25DGeometryStudyMethod::BsgmSharedGeometry) {
        throw std::runtime_error("CPU geometry study method is invalid");
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
        throw std::runtime_error("CPU geometry study config/scenario fields do not match");
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
            throw std::runtime_error("CPU geometry study scenario is invalid");
        }
    }
    bed_datum_m_ =
        *std::min_element(scenario_.terrain_height_m.begin(), scenario_.terrain_height_m.end());
    geometry_ = build_geometry(scenario_, boundary_, bed_datum_m_);
    reset();
}

void Fluid25DGeometryStudy::reset() {
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

double Fluid25DGeometryStudy::total_water_volume_m3() const {
    return total(cells_, config_.cell_size_m, false);
}

double Fluid25DGeometryStudy::total_tracer_amount_m3() const {
    return total(cells_, config_.cell_size_m, true);
}

Fluid25DGeometryStudyObservation Fluid25DGeometryStudy::inspect_transport() const {
    return euler(cells_, config_, scenario_, geometry_, boundary_, bed_datum_m_, 0.0).observation;
}

Fluid25DTracerStepResult Fluid25DGeometryStudy::step_with_dye(double source_rate_scale) {
    return step(source_rate_scale, dye_schedule_.source_concentration(config_));
}

Fluid25DTracerStepResult Fluid25DGeometryStudy::step(double source_rate_scale,
                                                     double source_concentration) {
    if (!std::isfinite(source_rate_scale) || source_rate_scale < 0.0 ||
        !std::isfinite(source_concentration) || source_concentration < 0.0 ||
        source_concentration > 1.0) {
        throw std::runtime_error("CPU geometry study source controls are invalid");
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
                return euler(input, config_, scenario_, geometry_, boundary_, bed_datum_m_, dt);
            } catch (const std::exception& error) {
                throw std::runtime_error("CPU geometry study substep " + std::to_string(substep) +
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
