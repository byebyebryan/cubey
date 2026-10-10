#pragma once

#include "fluid_25d_solver_state.h"
#include <cubey/procedural/noise.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {

// Shader layout: native cell + local slot, never compacted draw index.
struct Fluid25DWhitewaterSeed {
    std::uint32_t cell = 0U, slot = 0U;
    float weight = 1.0F, padding = 0.0F;
};
static_assert(sizeof(Fluid25DWhitewaterSeed) == 16U);
struct Fluid25DWhitewaterSelection {
    std::vector<Fluid25DWhitewaterSeed> seeds;
    std::size_t requested = 0U;
    float priority_cutoff = 0.0F;
};

// Artist placement proxy only. GPU current surface/coverage remains authoritative.
// Small halos cover nearby reconstructed support; they do not create water.
[[nodiscard]] inline Fluid25DWhitewaterSelection
fluid_25d_whitewater_seeds(std::uint32_t width, std::uint32_t height, float cell_m,
                           std::span<const float> bed, std::span<const float> h,
                           std::span<const Fluid25DVelocity> velocity, std::uint32_t budget) {
    const std::size_t count = std::size_t(width) * height;
    if (width < 2U || height < 2U || count > std::numeric_limits<std::uint32_t>::max() / 32U ||
        !std::isfinite(cell_m) || cell_m <= 0.0F || bed.size() != count || h.size() != count ||
        velocity.size() != count)
        throw std::invalid_argument("invalid whitewater display fields");
    for (std::size_t i = 0; i < count; ++i)
        if (!std::isfinite(bed[i]) || !std::isfinite(h[i]) || h[i] < 0.0F ||
            !std::isfinite(velocity[i].x_m_per_s) || !std::isfinite(velocity[i].y_m_per_s))
            throw std::invalid_argument("non-finite whitewater display field");
    Fluid25DWhitewaterSelection result;
    if (budget == 0U)
        return result;
    const auto smooth = [](double a, double b, double x) {
        const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    };
    std::vector<float> activity(count, 0.0F);
    for (std::uint32_t z = 0; z + 1U < height; ++z)
        for (std::uint32_t x = 0; x + 1U < width; ++x) {
            const auto i = std::size_t(z) * width + x;
            const std::array ids{i, i + 1U, i + width, i + width + 1U};
            std::array<double, 4> eta{};
            for (std::size_t c = 0; c < 4U; ++c)
                eta[c] = double(bed[ids[c]]) + h[ids[c]];
            // Two native triangle centroids, plus corner support at reduced
            // placement weight. No classification based on display exaggeration.
            for (unsigned t = 0; t < 2U; ++t) {
                const double fx = t == 0U ? 2.0 / 3.0 : 1.0 / 3.0;
                const double fy = 1.0 - fx;
                const std::array weights{(1.0 - fx) * (1.0 - fy), fx * (1.0 - fy), (1.0 - fx) * fy,
                                         fx * fy};
                const double gx = (t == 0U ? eta[1] - eta[0] : eta[3] - eta[2]) / cell_m;
                const double gz = (t == 0U ? eta[3] - eta[1] : eta[2] - eta[0]) / cell_m;
                double ux = 0.0, uz = 0.0, depth = 0.0;
                for (std::size_t c = 0; c < 4U; ++c) {
                    ux += velocity[ids[c]].x_m_per_s * weights[c];
                    uz += velocity[ids[c]].y_m_per_s * weights[c];
                    depth += h[ids[c]] *
                             (t == 0U ? (c == 2U ? 0.0 : 1.0 / 3.0) : (c == 1U ? 0.0 : 1.0 / 3.0));
                }
                const auto score = [&](double u, double v, double d) {
                    if (d <= 0.08)
                        return 0.0F;
                    const double speed2 = u * u + v * v;
                    const double descent = -(gx * u + gz * v);
                    // Exactly-zero gates before sqrt/remaps; this is the same
                    // proxy, not a new mask chosen to improve performance.
                    if (speed2 <= 0.08 * 0.08 || descent <= 0.0 ||
                        descent * descent <= 0.2 * 0.2 * speed2)
                        return 0.0F;
                    const double speed = std::sqrt(speed2);
                    const double grade = descent / speed;
                    return float(smooth(0.08, 1.4, speed) * smooth(0.2, 1.0, grade) *
                                 smooth(0.002, 0.08, d) * smooth(0.08, 0.20, d));
                };
                activity[i] = std::max(activity[i], score(ux, uz, depth));
                for (const auto c : ids)
                    activity[i] = std::max(activity[i], 0.25F * score(velocity[c].x_m_per_s,
                                                                      velocity[c].y_m_per_s, h[c]));
            }
        }
    struct Ranked {
        Fluid25DWhitewaterSeed seed;
        float priority;
    };
    std::vector<float> placement = activity;
    for (std::uint32_t z = 0; z + 1U < height; ++z)
        for (std::uint32_t x = 0; x + 1U < width; ++x) {
            const float halo = 0.25F * activity[z * width + x];
            if (halo <= 0.01F)
                continue;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx) {
                    const auto xx =
                        std::clamp(std::int64_t(x) + dx, std::int64_t(0), std::int64_t(width) - 2);
                    const auto zz =
                        std::clamp(std::int64_t(z) + dz, std::int64_t(0), std::int64_t(height) - 2);
                    auto& value = placement[std::size_t(zz) * width + std::size_t(xx)];
                    value = std::max(value, halo);
                }
        }
    std::vector<Ranked> ranked;
    ranked.reserve(std::min(count, std::size_t(budget)));
    for (std::uint32_t z = 0; z + 1U < height; ++z)
        for (std::uint32_t x = 0; x + 1U < width; ++x) {
            const auto cell = z * width + x;
            const float score = placement[cell];
            const float slots = 19.0F * float(smooth(0.01, 0.5, score));
            for (std::uint32_t slot = 0; slot < 19U; ++slot) {
                const float weight = std::clamp(slots - float(slot), 0.0F, 1.0F);
                if (weight <= 0.001F)
                    break;
                const float tie = cubey::procedural::hash_to_unit_masked_24(cell * 32U + slot);
                ranked.push_back({{cell, slot, weight, 0.0F},
                                  score + 0.15F * (1.0F - float(slot) / 19.0F) + 0.0001F * tie});
            }
        }
    result.requested = ranked.size();
    const auto stronger = [](const Ranked& a, const Ranked& b) {
        if (a.priority != b.priority)
            return a.priority > b.priority;
        return a.seed.cell != b.seed.cell ? a.seed.cell < b.seed.cell : a.seed.slot < b.seed.slot;
    };
    if (ranked.size() > budget) {
        std::nth_element(ranked.begin(), ranked.begin() + budget, ranked.end(), stronger);
        result.priority_cutoff = ranked[budget].priority;
        ranked.resize(budget);
        // Fade entries at the moving priority frontier, rather than popping
        // fully opaque flecks as the cap selects a new snapshot's candidates.
        for (auto& candidate : ranked)
            candidate.seed.weight *= float(smooth(
                result.priority_cutoff, result.priority_cutoff + 0.025F, candidate.priority));
    }
    // Order is not identity: retained cell/slot hashes are unchanged even when
    // nth_element reorders draws. Avoid an unnecessary full candidate sort.
    result.seeds.reserve(ranked.size());
    for (const auto& candidate : ranked)
        result.seeds.push_back(candidate.seed);
    return result;
}
} // namespace cubey::projects::fluid::fluid_25d
