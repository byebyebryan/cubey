#pragma once
#include "raytracing_config.h"
#include <cubey/core/math.h>
#include <memory>
#include <vector>
namespace cubey::projects::raytracing {
struct WorkshopState;
struct Vertex {
    math::Vec4 position, normal;
};
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    struct Admission {
        math::Vec3 minimum{}, maximum{};
        std::uint32_t welded_vertices = 0, removed_triangles = 0, snapped_positions = 0;
        double signed_volume = 0, maximum_position_snap = 0;
        bool closed = false;
    } admission;
};
struct Instance {
    std::uint32_t mesh;
    math::Mat4 transform{1};
    math::Vec4 color{1};
    std::uint32_t material = 0;
};
struct alignas(16) OpticalMaterial {
    math::Vec4 base_roughness{1, 1, 1, .4F};
    math::Vec4 optics{0, 1.5F, 1,
                      0}; // kind: opaque/mirror/glass, IOR, attenuation distance, use global base
    math::Vec4 attenuation{1};
    math::Vec4 reflectance{1};
};
static_assert(sizeof(OpticalMaterial) == 64);
struct Scene {
    struct Route {
        std::string name;
        std::uint32_t first, count;
    };
    std::vector<Route> routes;
    std::shared_ptr<WorkshopState> workshop;
    std::vector<Mesh> meshes;
    std::vector<Instance> instances;
    std::vector<OpticalMaterial> materials{
        {{1, 1, 1, .4F}, {0, 1.5F, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 1}},
        {{.38F, .41F, .45F, .75F}, {0, 1.5F, 1, 0}, {1, 1, 1, 1}, {1, 1, 1, 1}}};
};
Scene make_scene(const Config& config);
Scene make_optical_scene(const Config& config);
} // namespace cubey::projects::raytracing
