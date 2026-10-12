#include <cmath>
#include <cubey/terrain/terrain_surface_patch_lod.h>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>

int main() {
    try {
        using namespace cubey;
        for (const auto focus : {math::Vec2{0, 0}, math::Vec2{120, 200}, math::Vec2{-480, -320}}) {
            const asset::TerrainWaterCaptureGrid grid{65, 49, 16, {-512, -384}};
            if (focus.y < -384)
                continue;
            const auto mesh = terrain::make_terrain_surface_patch_lod(grid, focus, 256);
            double area = 0;
            std::map<std::pair<std::uint32_t, std::uint32_t>, unsigned> edges;
            for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
                const auto a = mesh.coordinates.at(mesh.indices[i]),
                           b = mesh.coordinates.at(mesh.indices[i + 1]),
                           c = mesh.coordinates.at(mesh.indices[i + 2]);
                const double triangle = (double(b.y) - a.y) * (double(c.x) - a.x) -
                                        (double(b.x) - a.x) * (double(c.y) - a.y);
                if (triangle <= 0)
                    throw std::runtime_error("nonpositive fixed topology triangle");
                area += triangle * .5;
                for (unsigned j = 0; j < 3; ++j) {
                    const auto u = mesh.indices[i + j], v = mesh.indices[i + (j + 1) % 3];
                    ++edges[{std::min(u, v), std::max(u, v)}];
                }
            }
            if (std::abs(area - 64 * 48) > 0.01)
                throw std::runtime_error("fixed topology area mismatch");
            for (const auto p : mesh.coordinates)
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x < 0 || p.y < 0 || p.x > 64 ||
                    p.y > 48)
                    throw std::runtime_error("fixed topology coordinate outside capture");
            for (const auto& [edge, count] : edges) {
                if (count == 2)
                    continue;
                const auto a = mesh.coordinates[edge.first], b = mesh.coordinates[edge.second];
                if (count != 1 || !((a.x == 0 && b.x == 0) || (a.y == 0 && b.y == 0) ||
                                    (a.x == 64 && b.x == 64) || (a.y == 48 && b.y == 48)))
                    throw std::runtime_error("unmatched fixed topology interior edge");
            }
        }
        const auto wide = terrain::make_terrain_surface_patch_lod(
            {1913, 1913, 32, {-30592, -30592}}, {11520, 19200});
        if (wide.indices.size() / 3 >= 29245952 / 2)
            throw std::runtime_error("no useful fixed mesh reduction");
        double wide_area = 0;
        for (std::size_t i = 0; i < wide.indices.size(); i += 3) {
            const auto a = wide.coordinates.at(wide.indices[i]),
                       b = wide.coordinates.at(wide.indices[i + 1]),
                       c = wide.coordinates.at(wide.indices[i + 2]);
            wide_area += ((double(b.y) - a.y) * (double(c.x) - a.x) -
                          (double(b.x) - a.x) * (double(c.y) - a.y)) *
                         .5;
        }
        if (std::abs(wide_area - 1912.0 * 1912.0) > .01)
            throw std::runtime_error("wide topology area mismatch");
        for (unsigned fixture = 0; fixture < 4; ++fixture) {
            asset::TerrainWaterCaptureGrid bad{65, 49, 16, {-512, -384}};
            math::Vec2 focus{0};
            float span = 256;
            if (fixture == 0)
                bad.cell_size_m = 0;
            if (fixture == 1)
                bad.sample_origin_xz_m.x = std::numeric_limits<float>::quiet_NaN();
            if (fixture == 2)
                focus.x = 1024;
            if (fixture == 3)
                span = std::numeric_limits<float>::infinity();
            bool rejected = false;
            try {
                (void)terrain::make_terrain_surface_patch_lod(bad, focus, span);
            } catch (const std::exception&) {
                rejected = true;
            }
            if (!rejected)
                throw std::runtime_error("invalid fixed topology admitted");
        }
        std::cout
            << "terrain_surface_patch_lod: PASS coverage/winding/edge contracts; wide vertices="
            << wide.coordinates.size() << " triangles=" << wide.indices.size() / 3 << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
