#include <algorithm>
#include <array>
#include <cmath>
#include <cubey/terrain/terrain_surface_context.h>
#include <limits>
#include <stdexcept>

namespace cubey::terrain {
namespace {
math::Vec4 basis(float t) {
    return {std::pow(1.0F - t, 3.0F) / 6.0F, (3 * t * t * t - 6 * t * t + 4) / 6,
            (-3 * t * t * t + 3 * t * t + 3 * t + 1) / 6, t * t * t / 6};
}
math::Vec4 derivative(float t) {
    return {-0.5F * (1 - t) * (1 - t), 1.5F * t * t - 2 * t, -1.5F * t * t + t + 0.5F,
            0.5F * t * t};
}
float smooth(float t) {
    t = std::clamp(t, 0.0F, 1.0F);
    return t * t * (3 - 2 * t);
}
struct Ring {
    std::array<std::vector<std::uint32_t>, 4> sides;
};
} // namespace
TerrainSurfaceContextSample terrain_surface_context_core_sample(const asset::TerrainWaterCapture& c,
                                                                math::Vec2 p) {
    p = glm::clamp(p, math::Vec2{0}, math::Vec2{c.grid.width - 1U, c.grid.height - 1U});
    const auto base = glm::ivec2(glm::floor(p)) - glm::ivec2(1);
    const auto wx = basis(glm::fract(p).x), wz = basis(glm::fract(p).y);
    const auto dx = derivative(glm::fract(p).x), dz = derivative(glm::fract(p).y);
    TerrainSurfaceContextSample r;
    for (int z = 0; z < 4; ++z)
        for (int x = 0; x < 4; ++x) {
            const auto cell = glm::clamp(base + glm::ivec2{x, z}, glm::ivec2{0},
                                         glm::ivec2{c.grid.width - 1U, c.grid.height - 1U});
            const auto i = std::size_t(cell.y) * c.grid.width + std::size_t(cell.x);
            r.bed_m += c.bed_m[i] * wx[x] * wz[z];
            r.depth_m += c.depth_m[i] * wx[x] * wz[z];
            r.bed_gradient +=
                math::Vec2{dx[x] * wz[z], wx[x] * dz[z]} * (c.bed_m[i] / c.grid.cell_size_m);
        }
    return r;
}
TerrainSurfaceContextProduct
make_terrain_surface_context(const asset::TerrainWaterCapture& c,
                             const asset::TerrainRasterHeightSource& source,
                             const TerrainSurfaceField& field, float collar) {
    asset::validate_terrain_water_capture_source(c, source);
    if (!std::isfinite(collar) || collar < 16 || collar > 512)
        throw std::runtime_error("invalid exterior terrain collar width");
    const auto lo = c.grid.sample_origin_xz_m;
    const auto hi = lo + math::Vec2{c.grid.width - 1U, c.grid.height - 1U} * c.grid.cell_size_m;
    const auto bounds = source.bounds();
    const float fine = c.grid.cell_size_m * 0.5F;
    if (fine < 1 || fine > 128 ||
        glm::any(glm::lessThan(lo - bounds.minimum_xz, math::Vec2{fine})) ||
        glm::any(glm::lessThan(bounds.maximum_xz - hi, math::Vec2{fine})))
        throw std::runtime_error("captured patch lacks a supported exterior ground margin");
    TerrainSurfaceContextProduct product;
    product.collar_width_m = collar;
    product.minimum = math::Vec3{std::numeric_limits<float>::max()};
    product.maximum = -product.minimum;
    const auto focus = c.grid.center_xz_m();
    const auto core = [&](math::Vec2 p) {
        return terrain_surface_context_core_sample(c, (p - lo) / c.grid.cell_size_m);
    };
    const auto outside = [&](math::Vec2 p) {
        return std::max({lo.x - p.x, p.x - hi.x, lo.y - p.y, p.y - hi.y, 0.0F});
    };
    const auto height = [&](math::Vec2 p, float spacing) {
        p = glm::clamp(p, bounds.minimum_xz, bounds.maximum_xz);
        const auto q = glm::clamp(p, lo, hi);
        const auto sample = core(q);
        const float extended = sample.bed_m + glm::dot(p - q, sample.bed_gradient);
        const float original = source.sample_height({.world_xz = p, .footprint_m = spacing});
        return std::lerp(extended, original, smooth(outside(p) / collar));
    };
    const auto vertex = [&](math::Vec2 p, float spacing) {
        p = glm::clamp(p, bounds.minimum_xz, bounds.maximum_xz);
        const auto local = p - focus;
        const auto sample = core(p);
        const float d = outside(p);
        auto gradient = sample.bed_gradient;
        if (d > 0) {
            const float e = std::max(1.0F, std::min(8.0F, spacing * 0.25F));
            const auto x0 = glm::clamp(p - math::Vec2{e, 0}, bounds.minimum_xz, bounds.maximum_xz);
            const auto x1 = glm::clamp(p + math::Vec2{e, 0}, bounds.minimum_xz, bounds.maximum_xz);
            const auto z0 = glm::clamp(p - math::Vec2{0, e}, bounds.minimum_xz, bounds.maximum_xz);
            const auto z1 = glm::clamp(p + math::Vec2{0, e}, bounds.minimum_xz, bounds.maximum_xz);
            gradient = {(height(x1, spacing) - height(x0, spacing)) / (x1.x - x0.x),
                        (height(z1, spacing) - height(z0, spacing)) / (z1.y - z0.y)};
        }
        const auto n = glm::normalize(math::Vec3{-gradient.x, 1, -gradient.y});
        const auto weights = field.sample(p);
        const math::Vec3 position{local.x, height(p, spacing), local.y};
        product.minimum = glm::min(product.minimum, position);
        product.maximum = glm::max(product.maximum, position);
        const float wet = smooth((sample.depth_m - 0.0001F) / (0.012F - 0.0001F)) *
                          (1 - smooth((sample.depth_m - 0.02F) / 0.03F));
        product.vertices.push_back({{position, wet * (1 - smooth(d / 64.0F))}, {n, 0}, weights});
        return std::uint32_t(product.vertices.size() - 1);
    };
    const auto make_ring = [&](math::Vec2 lower, math::Vec2 upper, float spacing, bool inner) {
        Ring ring;
        // Clockwise in XZ; triangle winding is qualified separately below.
        const std::array<math::Vec2, 4> corners{
            math::Vec2{lower.x, lower.y}, {upper.x, lower.y}, upper, {lower.x, upper.y}};
        std::array<std::uint32_t, 4> corner_ids;
        for (unsigned side = 0; side < 4; ++side)
            corner_ids[side] = vertex(corners[side], spacing);
        for (unsigned side = 0; side < 4; ++side) {
            const auto a = corners[side], b = corners[(side + 1) % 4];
            const auto segments = inner ? 2U * ((side % 2 == 0 ? c.grid.width : c.grid.height) - 1U)
                                        : std::uint32_t(std::ceil(glm::length(b - a) / spacing));
            ring.sides[side].push_back(corner_ids[side]);
            for (std::uint32_t i = 1; i < segments; ++i)
                ring.sides[side].push_back(
                    vertex(inner ? a + glm::normalize(b - a) * (float(i) * fine)
                                 : a + (b - a) * (float(i) / float(segments)),
                           spacing));
            ring.sides[side].push_back(corner_ids[(side + 1) % 4]);
        }
        return ring;
    };
    auto previous = make_ring(lo, hi, fine, true);
    product.inner_perimeter_vertices = std::uint32_t(product.vertices.size());
    const auto triangle = [&](std::uint32_t a, std::uint32_t b, std::uint32_t d) {
        const auto p = product.vertices[a].position_wet_hint;
        const auto q = product.vertices[b].position_wet_hint;
        const auto r = product.vertices[d].position_wet_hint;
        const double signed_area =
            (double(q.z) - p.z) * (double(r.x) - p.x) - (double(q.x) - p.x) * (double(r.z) - p.z);
        if (std::abs(signed_area) < 0.000001)
            return; // Source boundary already reached.
        if (signed_area < 0)
            std::swap(b, d);
        product.indices.insert(product.indices.end(), {a, b, d});
    };
    float offset = 0;
    while (true) {
        const float step = offset < 128     ? fine
                           : offset < 512   ? fine * 4
                           : offset < 2048  ? fine * 8
                           : offset < 8192  ? fine * 16
                           : offset < 16384 ? fine * 32
                                            : fine * 64;
        offset += step;
        const auto lower = glm::max(lo - math::Vec2{offset}, bounds.minimum_xz);
        const auto upper = glm::min(hi + math::Vec2{offset}, bounds.maximum_xz);
        auto next = make_ring(lower, upper, step, false);
        for (unsigned side = 0; side < 4; ++side) {
            const auto& a = previous.sides[side];
            const auto& b = next.sides[side];
            std::size_t i = 0, j = 0;
            while (i + 1 < a.size() || j + 1 < b.size()) {
                if (j + 1 == b.size() ||
                    (i + 1 < a.size() && double(i + 1) / double(a.size() - 1) <=
                                             double(j + 1) / double(b.size() - 1))) {
                    triangle(a[i], b[j], a[i + 1]);
                    ++i;
                } else {
                    triangle(a[i], b[j], b[j + 1]);
                    ++j;
                }
            }
        }
        previous = std::move(next);
        if (lower == bounds.minimum_xz && upper == bounds.maximum_xz)
            break;
        if (product.vertices.size() > 2'000'000 || offset > 200'000)
            throw std::runtime_error("terrain context exceeds bounded mesh budget");
    }
    return product;
}
} // namespace cubey::terrain
