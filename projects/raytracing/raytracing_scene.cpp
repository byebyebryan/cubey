#include "raytracing_scene.h"
#include <algorithm>
#include <cmath>
#include <cubey/asset/gltf_asset.h>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
namespace cubey::projects::raytracing {
Scene make_scene(const Config& c) {
    if (c.scene == "materials")
        throw std::invalid_argument("material workshop scene integration is pending; P0 provides --bsdf-contract only");
    if (c.scene.find("reflection-") == 0 || c.scene.find("refraction-") == 0 || c.scene == "slab")
        return make_optical_scene(c);
    Scene s;
    if (c.scene == "dragon") {
        const auto asset = asset::load_gltf_asset(
            c.model, {.generate_missing_tangents = false, .generate_missing_normals = false});
        if (asset.meshes.size() != 1 || asset.meshes[0].primitives.size() != 1 ||
            asset.nodes.size() != 1 || !asset.animations.empty() || !asset.skins.empty() ||
            !asset.images.empty())
            throw std::invalid_argument(
                "raytracing requires the prepared single-mesh, single-node untextured static GLB");
        const auto& node = asset.nodes[0];
        if (node.has_matrix || node.translation != math::Vec3(0) || node.scale != math::Vec3(1) ||
            node.rotation != math::Quat(1, 0, 0, 0) || !node.weights.empty())
            throw std::invalid_argument("prepared GLB node must be identity without morphs");
        const auto& p = asset.meshes[0].primitives[0];
        if (!p.morph_targets.empty() || p.vertices.size() > 2000000 || p.indices.size() > 12000000)
            throw std::invalid_argument("prepared geometry exceeds static raytracing admission");
        for (const auto& v : p.vertices) {
            if (glm::length(v.normal) < .99F || glm::length(v.normal) > 1.01F ||
                glm::any(glm::greaterThan(glm::abs(v.position), math::Vec3(.501F))))
                throw std::invalid_argument(
                    "prepared dragon requires normalized bounds and unit shading normals");
        }
        Mesh m;
        for (const auto& v : p.vertices)
            m.vertices.push_back({{v.position, 1}, {v.normal, 0}});
        m.indices = p.indices;
        s.meshes.push_back(std::move(m));
    } else {
        Mesh m;
        // Same primitive and winding as Khronos ray_tracing_basic.
        m.vertices = {{{1, 1, 0, 1}, {0, 0, 1, 0}},
                      {{-1, 1, 0, 1}, {0, 0, 1, 0}},
                      {{0, -1, 0, 1}, {0, 0, 1, 0}}};
        m.indices = {0, 1, 2};
        s.meshes.push_back(std::move(m));
    }
    s.instances.push_back({0, math::Mat4(1), {1, 1, 1, 0}});
    if (c.scene == "fixture") {
        auto t = glm::translate(math::Mat4(1), math::Vec3{.8F, .15F, -.6F});
        t = glm::rotate(t, .65F, math::Vec3{0, 1, 0});
        t = glm::scale(t, math::Vec3{.35F, .55F, .5F});
        s.instances.push_back({0, t, {.4F, .7F, 1, 0}});
    }
    if (c.scene != "triangle") {
        float low = 0;
        for (const auto& v : s.meshes[0].vertices)
            low = std::min(low, v.position.y);
        const float y = low - .015F;
        Mesh floor;
        floor.vertices = {{{-2, y, -2, 1}, {0, 1, 0, 0}},
                          {{-2, y, 2, 1}, {0, 1, 0, 0}},
                          {{2, y, 2, 1}, {0, 1, 0, 0}},
                          {{2, y, -2, 1}, {0, 1, 0, 0}}};
        floor.indices = {0, 1, 2, 0, 2, 3};
        s.meshes.push_back(std::move(floor));
        s.instances.push_back({1, math::Mat4(1), {1, 1, 1, 1}, 1});
    }
    for (const auto& m : s.meshes) {
        if (m.vertices.empty() || m.indices.empty() || m.indices.size() % 3)
            throw std::invalid_argument("empty/non-triangular geometry");
        for (auto i : m.indices)
            if (i >= m.vertices.size())
                throw std::invalid_argument("triangle index out of range");
        for (const auto& v : m.vertices)
            for (int i = 0; i < 3; ++i)
                if (!std::isfinite(v.position[i]) || !std::isfinite(v.normal[i]))
                    throw std::invalid_argument("nonfinite geometry");
    }
    return s;
}
} // namespace cubey::projects::raytracing
