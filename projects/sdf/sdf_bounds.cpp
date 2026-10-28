#include "sdf_regions.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace cubey::projects::sdf {
ModelBounds authored_model_bounds(const AuthoredScene& scene) {
    if (scene.program.empty())
        return {math::Vec3{0}, math::Vec3{0}, true};
    struct Children {
        int a = -1, b = -1;
    };
    std::vector<Children> children(scene.program.size());
    std::array<int, 32> stack{};
    unsigned size = 0;
    for (std::size_t i = 0; i < scene.program.size(); ++i) {
        if (scene.program[i].center_code.w >= 10) {
            children[i].b = stack.at(--size);
            children[i].a = stack.at(--size);
        }
        stack.at(size++) = static_cast<int>(i);
    }
    if (size != 1)
        return {};
    struct Box {
        glm::dvec3 low, high;
        bool known = true;
    };
    const auto bounds = [&](auto&& self, int index, double tau) -> Box {
        const auto& n = scene.program.at(static_cast<std::size_t>(index));
        const auto child = children.at(static_cast<std::size_t>(index));
        const auto code = static_cast<unsigned>(n.center_code.w);
        if (code >= 10) {
            if (code == 11 || code == 13)
                return self(self, child.a, tau);
            if (code != 10 && code != 12)
                return {{}, {}, false};
            const double next = tau + (code == 12 ? double(n.settings.y) / 4 : 0);
            const auto a = self(self, child.a, next);
            const auto b = self(self, child.b, next);
            return {glm::min(a.low, b.low), glm::max(a.high, b.high), a.known && b.known};
        }
        const auto p = glm::dvec4(n.parameters);
        glm::dvec3 extent;
        if (code == 1)
            extent = glm::dvec3(p) + p.w;
        else if (code == 2)
            extent = glm::dvec3(p.x);
        else if (code == 3)
            extent = {p.x, p.y + p.x, p.x};
        else if (code == 4)
            extent = {p.x, p.y, p.x};
        else if (code == 5)
            extent = {p.x + p.y, p.y, p.x + p.y};
        else if (code == 6)
            extent = {std::max(p.x, p.y), p.z, std::max(p.x, p.y)};
        else if (code == 7) {
            const double r = std::max(p.x, p.y);
            extent = {r, p.z + r, r};
        } else
            return {{}, {}, false};
        // Exact finite primitive offsets are enclosed by a local AABB expansion.
        // Recurse tau through smooth operators before this world-space conversion.
        extent = extent * double(n.settings.x) + tau;
        const auto q = n.inverse_rotation;
        const glm::dquat rotation{q.w, -q.x, -q.y, -q.z};
        auto matrix = glm::mat3_cast(rotation);
        for (int i = 0; i < 3; ++i)
            matrix[i] = glm::abs(matrix[i]);
        extent = matrix * extent;
        // Cover the float transform and outward conversion; field evaluation
        // uncertainty is already included in the requested root sublevel.
        extent = extent * (1 + 64 * double(std::numeric_limits<float>::epsilon())) + 2e-6;
        const auto center = glm::dvec3(n.center_code);
        return {center - extent, center + extent};
    };
    constexpr double epsilon = 1.0 / (256 * 7 * 32);
    const auto b = bounds(bounds, stack[0], epsilon + 2 * authored_evaluation_guard(scene));
    if (!b.known)
        return {};
    ModelBounds result;
    for (int i = 0; i < 3; ++i) {
        result.low[i] = std::max(-.4995F, std::nextafter(float(b.low[i]), -INFINITY));
        result.high[i] = std::min(.4995F, std::nextafter(float(b.high[i]), INFINITY));
    }
    result.empty = glm::any(glm::greaterThan(result.low, result.high));
    return result;
}
} // namespace cubey::projects::sdf
