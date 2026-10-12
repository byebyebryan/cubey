#include "fluid_25d_terrain_alignment.h"

#include <iostream>
#include <limits>
#include <string_view>

namespace {
void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void triangles() {
    using namespace cubey::projects::fluid::fluid_25d::alignment;
    cubey::terrain::TerrainBackdropSectorMesh mesh;
    // Plane y = 7 + 2*x - 3*z, two triangles with opposite windings.
    for (const auto p : {std::array<float, 2>{-4, -4}, {4, -4}, {-4, 4}, {4, 4}}) {
        mesh.vertices.push_back({.position = {p[0], 7 + 2 * p[0] - 3 * p[1], p[1]}});
    }
    mesh.indices = {0, 1, 3, 0, 2, 3};
    TriangleSampler sampler(5, 8);
    sampler.add(mesh);
    for (float x : {-4.0F, -2.2F, 0.0F, 1.75F, 4.0F}) {
        for (float z : {-4.0F, -1.5F, 0.0F, 2.9F, 4.0F}) {
            const auto value = sampler.height(x, z);
            require(value && std::abs(*value - (7 + 2 * x - 3 * z)) < 1.0e-5,
                    "triangle probe must reproduce a plane across bins and shared edges");
        }
    }
    require(!sampler.height(4.5F, 0) && !sampler.height(6, 0) &&
                !sampler.height(std::numeric_limits<float>::quiet_NaN(), 0),
            "outside/hole/nonfinite queries must not fabricate ground");
    bool rejected = false;
    mesh.indices.push_back(99);
    try {
        sampler.add(mesh);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "malformed triangle topology must fail");
    rejected = false;
    mesh.indices = {0, 1, 99};
    try {
        sampler.add(mesh);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "out-of-bounds triangle vertices must fail");
    for (const auto bins : {0U, 1025U, std::numeric_limits<unsigned>::max()}) {
        rejected = false;
        try {
            const TriangleSampler invalid(5, bins);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "invalid bin counts must fail before allocation");
    }
}

void splines() {
    using namespace cubey::projects::fluid::fluid_25d::alignment;
    std::vector<float> flat(64, 3.5F);
    require(std::abs(bspline(flat, 8, 8, 2.25F, 3.75F) - 3.5F) < 1.0e-6F,
            "B-spline must preserve a flat lake");
    require(std::abs(bspline(flat, 8, 8, -20, 40) - 3.5F) < 1.0e-6F,
            "B-spline must use the shader's clamped extension");
    for (std::uint32_t z = 0; z < 8; ++z) {
        for (std::uint32_t x = 0; x < 8; ++x) {
            flat[z * 8 + x] = 5 + 2 * static_cast<float>(x) - static_cast<float>(z);
        }
    }
    require(std::abs(bspline(flat, 8, 8, 3.25F, 3.5F) - 8.0F) < 1.0e-5F,
            "B-spline must reproduce interior affine slopes");
    std::vector<float> lake_depth(64);
    for (std::uint32_t z = 0; z < 8; ++z) {
        for (std::uint32_t x = 0; x < 8; ++x) {
            const auto index = z * 8 + x;
            flat[index] = 10 + 0.75F * static_cast<float>(x) + 1.5F * static_cast<float>(z);
            lake_depth[index] = 30 - flat[index];
        }
    }
    for (float x : {0.0F, 2.25F, 4.75F, 7.0F}) {
        for (float z : {0.0F, 1.25F, 3.5F, 7.0F}) {
            require(
                std::abs(bspline(flat, 8, 8, x, z) + bspline(lake_depth, 8, 8, x, z) - 30) <
                    1.0e-5F,
                "paired bed and depth must retain a flat lake over a steep bank and at boundaries");
        }
    }
    std::vector<float> impulse(64, 0.0F);
    impulse[3 * 8 + 3] = 9.0F;
    require(std::abs(bspline(impulse, 8, 8, 3, 3) - 4.0F) < 1.0e-6F,
            "native location must use positive 1/6 4/6 1/6 approximation, not interpolation");
    for (float x = 1; x < 6; x += 0.25F) {
        require(bspline(impulse, 8, 8, x, 3) >= 0.0F,
                "positive depth field must not acquire negative reconstruction");
    }
    bool rejected = false;
    try {
        static_cast<void>(bspline(impulse, 8, 7, 2, 3));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "B-spline probe must reject mismatched dimensions");
}
} // namespace

int main() {
    try {
        triangles();
        splines();
        std::cout << "fluid_25d_terrain_alignment: planar triangles, gaps, winding and B-spline "
                     "controls pass\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
