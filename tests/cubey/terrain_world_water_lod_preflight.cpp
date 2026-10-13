#include <chrono>
#include <cmath>
#include <cubey/terrain/terrain_surface_patch_lod.h>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        using namespace cubey;
        for (const auto [n, spacing] : {std::pair{1024U, 128.F}, {2048U, 64.F}, {2048U, 32.F}}) {
            const math::Vec2 center{15375.F, -30705.F};
            const auto half = float(n - 1) * spacing * .5F;
            const asset::TerrainWaterCaptureGrid grid{n, n, spacing, center - math::Vec2{half}};
            const auto started = std::chrono::steady_clock::now();
            const auto mesh =
                terrain::make_terrain_surface_patch_lod(grid, center, 16384.F, 4500000U);
            double area = 0;
            for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
                const auto a = mesh.coordinates.at(mesh.indices[i]);
                const auto b = mesh.coordinates.at(mesh.indices[i + 1]);
                const auto c = mesh.coordinates.at(mesh.indices[i + 2]);
                const double triangle = (double(b.y) - a.y) * (double(c.x) - a.x) -
                                        (double(b.x) - a.x) * (double(c.y) - a.y);
                if (triangle <= 0)
                    throw std::runtime_error("world LOD nonpositive triangle");
                area += triangle * .5;
            }
            if (std::abs(area - double(n - 1) * (n - 1)) > .01)
                throw std::runtime_error("world LOD area mismatch");
            for (const auto p : mesh.coordinates)
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x < 0 || p.y < 0 ||
                    p.x > float(n - 1) || p.y > float(n - 1))
                    throw std::runtime_error("world LOD coordinate outside capture");
            std::cout << "{\"n\":" << n << ",\"spacing_m\":" << spacing
                      << ",\"vertices\":" << mesh.coordinates.size()
                      << ",\"triangles\":" << mesh.indices.size() / 3 << ",\"mesh_bytes\":"
                      << mesh.coordinates.size() * sizeof(math::Vec2) +
                             mesh.indices.size() * sizeof(std::uint32_t)
                      << ",\"wall_ms\":"
                      << std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - started)
                             .count()
                      << ",\"area_and_winding_pass\":true}\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
