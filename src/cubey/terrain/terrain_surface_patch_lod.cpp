#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cubey/terrain/terrain_surface_patch_lod.h>
#include <stdexcept>
#include <unordered_map>

namespace cubey::terrain {
TerrainSurfacePatchLod make_terrain_surface_patch_lod(const asset::TerrainWaterCaptureGrid& grid,
                                                      math::Vec2 focus, float span) {
    if (grid.width < 2 || grid.height < 2 || grid.width > 16384 || grid.height > 16384 ||
        !std::isfinite(grid.cell_size_m) || grid.cell_size_m < 2 || grid.cell_size_m > 256 ||
        !std::isfinite(grid.sample_origin_xz_m.x) || !std::isfinite(grid.sample_origin_xz_m.y) ||
        !std::isfinite(focus.x) || !std::isfinite(focus.y) || !std::isfinite(span) ||
        span < grid.cell_size_m || span > 65536)
        throw std::runtime_error("invalid fixed paired terrain topology request");
    const auto lower = grid.sample_origin_xz_m;
    const auto upper = lower + math::Vec2{grid.width - 1U, grid.height - 1U} * grid.cell_size_m;
    if (glm::any(glm::lessThan(focus, lower)) || glm::any(glm::greaterThan(focus, upper)))
        throw std::runtime_error("fixed terrain topology focus is outside capture");
    const float fine = grid.cell_size_m * .5F;
    const auto lo = glm::max(lower, focus - math::Vec2{span * .5F});
    const auto hi = glm::min(upper, focus + math::Vec2{span * .5F});
    TerrainSurfacePatchLod result;
    result.fine_minimum_world = lo;
    result.fine_maximum_world = hi;
    std::unordered_map<std::uint64_t, std::uint32_t> boundary_vertices;
    const auto nx = std::uint32_t(std::ceil((hi.x - lo.x) / fine));
    const auto nz = std::uint32_t(std::ceil((hi.y - lo.y) / fine));
    if (std::uint64_t(nx + 1) * (nz + 1) > 2000000)
        throw std::runtime_error("fixed topology fine region exceeds vertex budget");
    const auto vertex = [&](math::Vec2 p) {
        const auto coordinate = glm::clamp((p - lower) / grid.cell_size_m, math::Vec2{0},
                                           math::Vec2{grid.width - 1U, grid.height - 1U});
        const bool boundary = coordinate.x == 0 || coordinate.y == 0 ||
                              coordinate.x == float(grid.width - 1) ||
                              coordinate.y == float(grid.height - 1);
        const auto key = (std::uint64_t(std::bit_cast<std::uint32_t>(coordinate.x)) << 32U) |
                         std::bit_cast<std::uint32_t>(coordinate.y);
        if (boundary) {
            const auto found = boundary_vertices.find(key);
            if (found != boundary_vertices.end())
                return found->second;
        }
        if (result.coordinates.size() >= 2000000)
            throw std::runtime_error("fixed topology exceeds vertex budget");
        result.coordinates.push_back(coordinate);
        const auto id = std::uint32_t(result.coordinates.size() - 1);
        if (boundary)
            boundary_vertices.emplace(key, id);
        return id;
    };
    const auto triangle = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        const auto p = result.coordinates[a], q = result.coordinates[b], r = result.coordinates[c];
        const double area =
            (double(q.y) - p.y) * (double(r.x) - p.x) - (double(q.x) - p.x) * (double(r.y) - p.y);
        if (std::abs(area) < 1e-12)
            return;
        if (area < 0)
            std::swap(b, c);
        result.indices.insert(result.indices.end(), {a, b, c});
    };
    for (std::uint32_t z = 0; z <= nz; ++z)
        for (std::uint32_t x = 0; x <= nx; ++x)
            vertex(lo + (hi - lo) * math::Vec2{float(x) / float(nx), float(z) / float(nz)});
    for (std::uint32_t z = 0; z < nz; ++z)
        for (std::uint32_t x = 0; x < nx; ++x) {
            const auto a = z * (nx + 1) + x, b = a + 1, c = a + nx + 1, d = c + 1;
            triangle(a, d, b);
            triangle(a, c, d);
        }
    using Ring = std::array<std::vector<std::uint32_t>, 4>;
    Ring previous;
    for (std::uint32_t x = 0; x <= nx; ++x)
        previous[0].push_back(x);
    for (std::uint32_t z = 0; z <= nz; ++z)
        previous[1].push_back(z * (nx + 1) + nx);
    for (std::uint32_t x = nx + 1; x-- > 0;)
        previous[2].push_back(nz * (nx + 1) + x);
    for (std::uint32_t z = nz + 1; z-- > 0;)
        previous[3].push_back(z * (nx + 1));
    float offset = 0;
    while (lo != lower || hi != upper) {
        const float requested_step = offset < 128     ? fine
                                     : offset < 512   ? fine * 2
                                     : offset < 2048  ? fine * 4
                                     : offset < 8192  ? fine * 8
                                     : offset < 16384 ? fine * 16
                                                      : fine * 32;
        // The first coarse pilot exposed exaggerated lake-edge depth and wet
        // silhouettes at 128-1024 m. Keep this bounded alternative at 64 m.
        const float step = std::min(requested_step, 64.0F);
        offset += step;
        const auto next_lo = glm::max(lower, lo - math::Vec2{offset});
        const auto next_hi = glm::min(upper, hi + math::Vec2{offset});
        const bool last = next_lo == lower && next_hi == upper;
        // Preserve every original 2x boundary vertex, allowing the existing
        // exterior collar to attach without T-junctions or a new seam contract.
        const float spacing = last ? fine : step;
        const std::array<math::Vec2, 4> corners{
            next_lo, {next_hi.x, next_lo.y}, next_hi, {next_lo.x, next_hi.y}};
        std::array<std::uint32_t, 4> ids;
        for (unsigned side = 0; side < 4; ++side)
            ids[side] = vertex(corners[side]);
        Ring next;
        for (unsigned side = 0; side < 4; ++side) {
            const auto a = corners[side], b = corners[(side + 1) % 4];
            const auto segments = std::uint32_t(std::ceil(glm::length(b - a) / spacing));
            next[side].push_back(ids[side]);
            for (std::uint32_t i = 1; i < segments; ++i)
                next[side].push_back(vertex(a + glm::normalize(b - a) * (float(i) * spacing)));
            next[side].push_back(ids[(side + 1) % 4]);
            const auto& inner = previous[side];
            const auto& outer = next[side];
            std::size_t i = 0, j = 0;
            while (i + 1 < inner.size() || j + 1 < outer.size()) {
                if (j + 1 == outer.size() ||
                    (i + 1 < inner.size() && double(i + 1) / double(inner.size() - 1) <=
                                                 double(j + 1) / double(outer.size() - 1))) {
                    triangle(inner[i], outer[j], inner[i + 1]);
                    ++i;
                } else {
                    triangle(inner[i], outer[j], outer[j + 1]);
                    ++j;
                }
            }
        }
        previous = std::move(next);
        if (last)
            break;
        if (offset > 200000)
            throw std::runtime_error("fixed terrain topology exceeds extent budget");
    }
    return result;
}
} // namespace cubey::terrain
