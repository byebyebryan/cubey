#include "raytracing_dragon_support.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace cubey::projects::raytracing {
namespace {
float cross2(math::Vec2 a, math::Vec2 b) {
    return a.x * b.y - a.y * b.x;
}
std::vector<math::Vec2> convex_hull(std::vector<math::Vec2> points) {
    std::sort(points.begin(), points.end(),
              [](auto a, auto b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    points.erase(std::unique(points.begin(), points.end()), points.end());
    if (points.size() < 3)
        throw std::invalid_argument("stone slab needs a two-dimensional outline");
    std::vector<math::Vec2> hull;
    const auto append = [&](math::Vec2 point, std::size_t floor) {
        while (hull.size() > floor &&
               cross2(hull.back() - hull[hull.size() - 2], point - hull.back()) <= 0)
            hull.pop_back();
        hull.push_back(point);
    };
    for (const auto point : points)
        append(point, 1);
    const auto lower_size = hull.size();
    for (auto it = points.rbegin() + 1; it != points.rend(); ++it)
        append(*it, lower_size);
    hull.pop_back();
    if (hull.size() < 3)
        throw std::invalid_argument("stone slab needs a two-dimensional outline");
    return hull;
}
} // namespace

Mesh make_dragon_rock_footing(const Mesh& dragon, float dragon_y) {
    if (dragon.vertices.empty() || !std::isfinite(dragon_y))
        throw std::invalid_argument("stone slab requires finite subject geometry");
    float low = std::numeric_limits<float>::max();
    for (const auto& vertex : dragon.vertices) {
        const auto point = math::Vec3(vertex.position);
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
            throw std::invalid_argument("stone slab requires finite subject geometry");
        low = std::min(low, point.y);
    }
    std::vector<math::Vec2> contacts;
    math::Vec2 minimum(std::numeric_limits<float>::max()), maximum(-minimum);
    for (const auto& vertex : dragon.vertices) {
        const auto point = math::Vec3(vertex.position);
        if (point.y > low + .015F)
            continue;
        const math::Vec2 horizontal{point.x, point.z};
        if (point.y <= low + .005F)
            contacts.push_back(horizontal);
        minimum = glm::min(minimum, horizontal);
        maximum = glm::max(maximum, horizontal);
    }
    if (contacts.size() < 3 || glm::any(glm::lessThan(maximum - minimum, math::Vec2(.01F))))
        throw std::invalid_argument("stone slab requires a two-dimensional contact region");

    const auto center = (minimum + maximum) * .5F;
    const auto size = maximum - minimum;
    // Asymmetric outer silhouette candidates, fitted to the subject below.
    const std::array profile{
        math::Vec2{-.045F, .25F}, math::Vec2{.16F, -.12F},  math::Vec2{.37F, -.07F},
        math::Vec2{.71F, -.14F},  math::Vec2{.99F, -.05F},  math::Vec2{1.08F, .21F},
        math::Vec2{1.04F, .65F},  math::Vec2{.92F, 1.08F},  math::Vec2{.68F, 1.14F},
        math::Vec2{.18F, 1.13F},  math::Vec2{-.04F, 1.05F}, math::Vec2{-.10F, .67F}};
    std::vector<math::Vec2> outline;
    for (const auto point : profile)
        outline.push_back(minimum + point * size);
    // Expand only the necessary silhouette sectors to retain a margin around
    // actual foot contacts. Radial expansion preserves the simple star-shaped cap.
    for (const auto contact : contacts) {
        const auto delta = contact - center;
        for (std::size_t i = 0; i < outline.size(); ++i) {
            const auto next = (i + 1) % outline.size();
            const auto a = outline[i] - center, b = outline[next] - center;
            if (cross2(a, delta) < -1e-8F || cross2(delta, b) < -1e-8F)
                continue;
            const auto edge = b - a;
            const auto outward = glm::normalize(math::Vec2(edge.y, -edge.x));
            const float distance = glm::dot(a, outward);
            const float needed = glm::dot(delta, outward) + .012F;
            if (needed > distance) {
                const float scale = needed / distance;
                outline[i] = center + a * scale;
                outline[next] = center + b * scale;
            }
            break;
        }
    }
    // Convexification after fitting keeps every supported contact inside and
    // guarantees that every perimeter corner turns outward.
    outline = convex_hull(std::move(outline));
    const float top = low + dragon_y - .001F, bottom = top - .030F;
    Mesh result;
    const auto triangle = [&](math::Vec3 a, math::Vec3 b, math::Vec3 c, math::Vec3 outward) {
        auto normal = glm::cross(b - a, c - a);
        if (glm::dot(normal, outward) < 0) {
            std::swap(b, c);
            normal = -normal;
        }
        if (glm::length(normal) < 1e-10F)
            throw std::runtime_error("degenerate stone slab face");
        normal = glm::normalize(normal);
        for (const auto point : {a, b, c}) {
            result.indices.push_back(static_cast<std::uint32_t>(result.vertices.size()));
            result.vertices.push_back({{point, 1}, {normal, 0}});
        }
    };
    for (std::size_t i = 0; i < outline.size(); ++i) {
        const auto a = outline[i], b = outline[(i + 1) % outline.size()];
        const math::Vec3 at{a.x, top, a.y}, bt{b.x, top, b.y};
        const math::Vec3 ab{a.x, bottom, a.y}, bb{b.x, bottom, b.y};
        const math::Vec3 outward{a.x + b.x - 2 * center.x, 0, a.y + b.y - 2 * center.y};
        triangle({center.x, top, center.y}, at, bt, {0, 1, 0});
        triangle({center.x, bottom, center.y}, ab, bb, {0, -1, 0});
        triangle(at, ab, bt, outward);
        triangle(bt, ab, bb, outward);
    }
    return result;
}
} // namespace cubey::projects::raytracing
