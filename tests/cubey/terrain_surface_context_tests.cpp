#include <algorithm>
#include <chrono>
#include <cmath>
#include <cubey/terrain/terrain_surface_context.h>
#include <iostream>
#include <map>
#include <stdexcept>

using namespace cubey;
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
void check(const asset::TerrainWaterCapture& c, const asset::TerrainRasterHeightSource& source,
           const terrain::TerrainSurfaceField& field) {
    const auto started = std::chrono::steady_clock::now();
    const auto p = terrain::make_terrain_surface_context(c, source, field);
    const auto focus = c.grid.center_xz_m();
    const auto lo = c.grid.sample_origin_xz_m - focus;
    const auto hi = lo + math::Vec2{c.grid.width - 1U, c.grid.height - 1U} * c.grid.cell_size_m;
    const auto slo = source.bounds().minimum_xz - focus, shi = source.bounds().maximum_xz - focus;
    require(p.inner_perimeter_vertices == 4U * (c.grid.width + c.grid.height - 2U),
            "incorrect inner partition");
    for (std::size_t i = 0; i < p.vertices.size(); ++i) {
        const auto& v = p.vertices[i];
        for (int component = 0; component < 4; ++component)
            require(std::isfinite(v.position_wet_hint[component]) &&
                        std::isfinite(v.normal[component]) && std::isfinite(v.surface[component]),
                    "nonfinite context vertex");
        require(std::abs(glm::length(math::Vec3(v.normal)) - 1) < 0.001F, "invalid normal");
        require(v.position_wet_hint.x >= slo.x && v.position_wet_hint.x <= shi.x &&
                    v.position_wet_hint.z >= slo.y && v.position_wet_hint.z <= shi.y,
                "outside source");
        if (i < p.inner_perimeter_vertices) {
            require(v.position_wet_hint.x == lo.x || v.position_wet_hint.x == hi.x ||
                        v.position_wet_hint.z == lo.y || v.position_wet_hint.z == hi.y,
                    "not on core boundary");
            const auto sample = terrain::terrain_surface_context_core_sample(
                c, (math::Vec2(v.position_wet_hint.x, v.position_wet_hint.z) - lo) /
                       c.grid.cell_size_m);
            require(std::abs(sample.bed_m - v.position_wet_hint.y) < .001F,
                    "core perimeter height mismatch");
        }
    }
    std::map<std::pair<std::uint32_t, std::uint32_t>, unsigned> edges;
    double area = 0;
    for (std::size_t i = 0; i < p.indices.size(); i += 3) {
        const auto a = math::Vec3(p.vertices.at(p.indices[i]).position_wet_hint);
        const auto b = math::Vec3(p.vertices.at(p.indices[i + 1]).position_wet_hint);
        const auto d = math::Vec3(p.vertices.at(p.indices[i + 2]).position_wet_hint);
        const double twice =
            (double(b.z) - a.z) * (double(d.x) - a.x) - (double(b.x) - a.x) * (double(d.z) - a.z);
        require(twice > 0, "bad winding or zero-area triangle");
        area += twice * .5;
        const auto center = (a + b + d) / 3.0F;
        require(!(center.x > lo.x && center.x < hi.x && center.z > lo.y && center.z < hi.y),
                "overlaps captured core");
        for (unsigned edge = 0; edge < 3; ++edge) {
            auto x = p.indices[i + edge], z = p.indices[i + (edge + 1) % 3];
            if (x > z)
                std::swap(x, z);
            require(++edges[{x, z}] <= 2, "nonmanifold edge");
        }
    }
    const double expected =
        double(shi.x - slo.x) * (shi.y - slo.y) - double(hi.x - lo.x) * (hi.y - lo.y);
    require(std::abs(area - expected) < std::max(.01, expected * .000001),
            "exterior coverage area mismatch");
    for (const auto& [edge, count] : edges)
        if (count == 1) {
            const auto a = p.vertices[edge.first].position_wet_hint,
                       b = p.vertices[edge.second].position_wet_hint;
            const auto on = [&](math::Vec2 lower, math::Vec2 upper) {
                return (a.x == b.x && (a.x == lower.x || a.x == upper.x)) ||
                       (a.z == b.z && (a.z == lower.y || a.z == upper.y));
            };
            if (!(on(lo, hi) || on(slo, shi))) {
                std::cerr << "unmatched edge " << edge.first << ',' << edge.second << " XZ " << a.x
                          << ',' << a.z << " -> " << b.x << ',' << b.z << '\n';
                throw std::runtime_error("unmatched interior edge");
            }
        }
    std::cout << "terrain_surface_context: PASS vertices=" << p.vertices.size()
              << " triangles=" << p.indices.size() / 3
              << " perimeter=" << p.inner_perimeter_vertices
              << " projected_area_error_m2=" << area - expected << " mesh_bytes="
              << p.vertices.size() * sizeof(terrain::TerrainSurfaceContextVertex) +
                     p.indices.size() * 4
              << " build_ms="
              << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                           started)
                     .count()
              << '\n';
}
int main(int argc, char** argv) {
    try {
        if (argc == 3) {
            asset::TerrainRasterHeightSource source(argv[2]);
            const auto c = asset::load_terrain_water_capture(argv[1]);
            check(c, source, terrain::make_terrain_surface_field(source, source.bounds()));
            return 0;
        }
        asset::TerrainRasterHeightSource source(std::filesystem::path(CUBEY_TEST_ASSET_DIR) /
                                                "terrain/backdrop-smoke/heightfield.json");
        const auto field =
            terrain::make_terrain_surface_field(source, source.bounds(), nullptr, 64);
        for (unsigned offset : {0U, 256U}) {
            asset::TerrainWaterCapture c;
            c.grid = {7U, 5U, 128.0F, {4096.0F + float(offset), 1024.0F}};
            c.source_elevation_sha256 = source.provenance().elevation_sha256;
            c.recording_sha256 = std::string(64, 'a');
            c.frame_sha256 = std::string(64, 'b');
            c.capture_time_s = 100;
            c.rain_mm_per_hour = 512;
            for (unsigned z = 0; z < 5; ++z)
                for (unsigned x = 0; x < 7; ++x) {
                    c.bed_m.push_back(
                        source.sample_height({.world_xz = c.grid.sample_origin_xz_m +
                                                          math::Vec2{x, z} * c.grid.cell_size_m}));
                    c.depth_m.push_back(float(x) * .5F);
                    c.velocity_xz_m_per_s.push_back({});
                }
            check(c, source, field);
            const auto wet = terrain::make_terrain_surface_context(c, source, field);
            const auto dry = terrain::make_terrain_surface_context(
                terrain::TerrainSurfaceContextInput{c.grid, c.bed_m, {}}, source, field);
            require(wet.indices == dry.indices && wet.vertices.size() == dry.vertices.size(),
                    "terrain-only context changed geometry topology");
            for (std::size_t i = 0; i < dry.vertices.size(); ++i) {
                require(dry.vertices[i].position_wet_hint.y == wet.vertices[i].position_wet_hint.y,
                        "terrain-only context changed bed geometry");
                require(dry.vertices[i].position_wet_hint.w == 0,
                        "terrain-only context invented a wet hint");
            }
            bool rejected = false;
            try {
                (void)terrain::make_terrain_surface_context(
                    terrain::TerrainSurfaceContextInput{c.grid, {}, {}}, source, field);
            } catch (const std::exception&) {
                rejected = true;
            }
            require(rejected, "missing terrain-only bed accepted");
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
