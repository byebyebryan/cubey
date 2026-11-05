#include "raytracing_scene.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <numbers>

namespace cubey::projects::raytracing {
namespace {
Mesh polyhedron(std::vector<math::Vec3> points, std::vector<std::uint32_t> faces) {
    Mesh mesh;
    math::Vec3 center(0);
    for (auto p : points)
        center += p;
    center /= float(points.size());
    for (auto p : points)
        mesh.vertices.push_back({{p, 1}, {glm::normalize(p - center), 0}});
    for (std::size_t i = 0; i < faces.size(); i += 3) {
        auto a = points[faces[i]], b = points[faces[i + 1]], c = points[faces[i + 2]];
        if (glm::dot(glm::cross(b - a, c - a), (a + b + c) / 3.F - center) < 0)
            std::swap(faces[i + 1], faces[i + 2]);
    }
    mesh.indices = std::move(faces);
    return mesh;
}
Mesh box() {
    return polyhedron({{-.5F, -.5F, -.5F},
                       {.5F, -.5F, -.5F},
                       {.5F, .5F, -.5F},
                       {-.5F, .5F, -.5F},
                       {-.5F, -.5F, .5F},
                       {.5F, -.5F, .5F},
                       {.5F, .5F, .5F},
                       {-.5F, .5F, .5F}},
                      {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7, 0, 4, 7, 0, 7, 3,
                       1, 5, 6, 1, 6, 2, 0, 1, 5, 0, 5, 4, 3, 2, 6, 3, 6, 7});
}
Mesh sphere() {
    Mesh mesh;
    constexpr int rings = 64, segments = 128;
    mesh.vertices.push_back({{0, 1, 0, 1}, {0, 1, 0, 0}});
    for (int r = 1; r < rings; ++r)
        for (int s = 0; s < segments; ++s) {
            const float theta = std::numbers::pi_v<float> * float(r) / rings;
            const float phi = 2 * std::numbers::pi_v<float> * float(s) / segments;
            math::Vec3 p{std::sin(theta) * std::cos(phi), std::cos(theta),
                         std::sin(theta) * std::sin(phi)};
            mesh.vertices.push_back({{p, 1}, {p, 0}});
        }
    const auto bottom = std::uint32_t(mesh.vertices.size());
    mesh.vertices.push_back({{0, -1, 0, 1}, {0, -1, 0, 0}});
    auto triangle = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        const auto p = math::Vec3(mesh.vertices[a].position),
                   q = math::Vec3(mesh.vertices[b].position),
                   r = math::Vec3(mesh.vertices[c].position);
        if (glm::dot(glm::cross(q - p, r - p), p + q + r) < 0)
            std::swap(b, c);
        mesh.indices.insert(mesh.indices.end(), {a, b, c});
    };
    for (int s = 0; s < segments; ++s) {
        const auto next = (s + 1) % segments;
        triangle(0, std::uint32_t(1 + s), std::uint32_t(1 + next));
        for (int r = 0; r < rings - 2; ++r) {
            auto a = std::uint32_t(1 + r * segments + s),
                 b = std::uint32_t(1 + r * segments + next);
            triangle(a, a + segments, b);
            triangle(b, a + segments, b + segments);
        }
        triangle(bottom, std::uint32_t(1 + (rings - 2) * segments + s),
                 std::uint32_t(1 + (rings - 2) * segments + next));
    }
    return mesh;
}
} // namespace
Scene make_optical_scene(const Config& config) {
    Scene scene;
    const bool glass = config.scene.find("refraction-") == 0 || config.scene == "slab";
    scene.materials = {{{.66F, .36F, .16F, .55F}, {0, 1.5F, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 1}},
                       {{.35F, .38F, .42F, .7F},
                        {glass ? 0.F : 1.F, 1.5F, 1, 0},
                        {1, 1, 1, 1},
                        {.86F, .88F, .91F, 1}},
                       {{1, 1, 1, 0},
                        {glass ? 2.F : 1.F, 1.5F, 1, 0},
                        {.94F, .98F, .99F, 1},
                        {.96F, .97F, .99F, 1}},
                       {{.85F, .035F, .025F, .5F}, {0, 1.5F, 1, 0}, {1, 1, 1, 1}, {1, 1, 1, 1}},
                       {{.03F, .16F, .8F, .5F}, {0, 1.5F, 1, 0}, {1, 1, 1, 1}, {1, 1, 1, 1}},
                       {{.03F, .7F, .12F, .5F}, {0, 1.5F, 1, 0}, {1, 1, 1, 1}, {1, 1, 1, 1}}};
    auto add = [&](Mesh mesh, math::Vec3 position, math::Vec3 scale, std::uint32_t material) {
        const auto index = std::uint32_t(scene.meshes.size());
        scene.meshes.push_back(std::move(mesh));
        scene.instances.push_back({index,
                                   glm::scale(glm::translate(math::Mat4(1), position), scale),
                                   {1, 1, 1, 1},
                                   material});
    };
    if (config.scene.find("dragon") != std::string::npos) {
        auto source = config;
        source.scene = "dragon";
        auto dragon = make_scene(source);
        float low = 0;
        for (const auto& v : dragon.meshes[0].vertices)
            low = std::min(low, v.position.y);
        add(std::move(dragon.meshes[0]), {-.45F, -low + .001F, -.25F}, {1, 1, 1}, 0);
    } else
        add(box(), {-.5F, .5F, -.5F}, {.5F, 1, .4F}, 0);
    Mesh floor;
    floor.vertices = {{{-16, 0, -16, 1}, {0, 1, 0, 0}},
                      {{-16, 0, 16, 1}, {0, 1, 0, 0}},
                      {{16, 0, 16, 1}, {0, 1, 0, 0}},
                      {{16, 0, -16, 1}, {0, 1, 0, 0}}};
    floor.indices = {0, 1, 2, 0, 2, 3};
    add(std::move(floor), {0, 0, 0}, {1, 1, 1}, 1);
    if (config.scene == "slab")
        add(box(), {0, .8F, 0}, {2, 1.5F, .3F}, 2);
    else if (glass) {
        auto prism =
            polyhedron({{-.35F, 0, -.22F},
                        {.35F, 0, -.22F},
                        {0, 0, .38F},
                        {-.35F, .8F, -.22F},
                        {.35F, .8F, -.22F},
                        {0, .8F, .38F}},
                       {0, 1, 2, 3, 4, 5, 0, 1, 4, 0, 4, 3, 1, 2, 5, 1, 5, 4, 2, 0, 3, 2, 3, 5});
        add(std::move(prism), {.28F, .025F, .45F}, {1, 1, 1}, 2);
    } else
        add(sphere(), {.65F, .38F, .1F}, {.38F, .38F, .38F}, 2);
    add(box(), {.35F, .4F, -.65F}, {.4F, .8F, .2F}, 3);
    add(box(), {1.35F, .6F, -.55F}, {.3F, 1.2F, .3F}, 4);
    add(box(), {-.2F, .3F, 3.8F}, {.35F, .6F, .2F}, 5);
    return scene;
}
} // namespace cubey::projects::raytracing
