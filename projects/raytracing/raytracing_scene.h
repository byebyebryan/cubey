#pragma once
#include "raytracing_config.h"
#include <cubey/core/math.h>
#include <vector>
namespace cubey::projects::raytracing {
struct Vertex {
    math::Vec4 position, normal;
};
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};
struct Instance {
    std::uint32_t mesh;
    math::Mat4 transform{1};
    math::Vec4 color{1};
};
struct Scene {
    std::vector<Mesh> meshes;
    std::vector<Instance> instances;
};
Scene make_scene(const Config& config);
} // namespace cubey::projects::raytracing
