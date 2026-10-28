#include "sdf_regions.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <random>
#include <stdexcept>
using namespace cubey::projects::sdf;
int main(int argc, char** argv) {
    if (argc != 2)
        throw std::invalid_argument("expected frozen fixture manifest");
    std::ifstream input(argv[1]);
    const auto manifest = nlohmann::json::parse(input);
    std::mt19937 random(4271);
    std::uniform_real_distribution<float> distribution(-.5F, .5F);
    std::size_t checked = 0;
    std::size_t bound_points = 0, far_points = 0, known_surface_points = 0;
    float max_error = 0;
    for (const auto& c : manifest.at("cases")) {
        const auto scene = parse_authored_scene(c.at("scene").dump());
        const auto root = authored_model_bounds(scene);
        const auto far = build_region_programs(scene, 16, region_reference_limit, true);
        const auto bound_check = [&](cubey::math::Vec3 p) {
            const float d = authored_distance(p, scene);
            ++bound_points;
            if (d <= 1.F / (256 * 7 * 32) && (root.empty || glm::any(glm::lessThan(p, root.low)) ||
                                              glm::any(glm::greaterThan(p, root.high))))
                throw std::runtime_error("model bound excludes source sublevel");
            const auto cell =
                glm::clamp(glm::ivec3(glm::floor((p + .5F) * 16.F)), glm::ivec3{0}, glm::ivec3{15});
            const auto h =
                far.headers.at(static_cast<std::size_t>((cell.z * 16 + cell.y) * 16 + cell.x));
            if (h.kind == 1) {
                ++far_points;
                if (!std::isfinite(h.bound) || h.bound * d <= 0 ||
                    std::abs(h.bound) > std::abs(d) + 1e-6F)
                    throw std::runtime_error("far certificate inequality failed");
                if (std::abs(d) <= 1.F / (256 * 7 * 32))
                    throw std::runtime_error("source hit band certified far");
            }
            if (std::abs(region_distance(p, scene, far) - d) > 2e-6F)
                throw std::runtime_error("far-cell exact query changed source value");
        };
        // Independently specified surface positions, plus both sides of every
        // far cell and the interior. At seams the fresh point lookup owns the cell.
        for (const auto& q : c.at("queries")) {
            const auto vec = [](const auto& v) { return cubey::math::Vec3{v[0], v[1], v[2]}; };
            if (q.at("kind") == "point")
                bound_check(vec(q.at("position")));
            else if (!q.at("expected_t").is_null()) {
                bound_check(vec(q.at("origin")) +
                            float(q.at("expected_t")) * vec(q.at("direction")));
                ++known_surface_points;
            }
        }
        for (unsigned z = 0; z < 16; ++z)
            for (unsigned y = 0; y < 16; ++y)
                for (unsigned x = 0; x < 16; ++x)
                    for (float offset : {.00001F, .5F, .99999F})
                        bound_check((cubey::math::Vec3{x, y, z} + offset) / 16.F - .5F);
        for (unsigned axis : {4U, 16U, 64U}) {
            if (axis == 64 && c.at("name") != "nested-smooth-left" &&
                c.at("name") != "smooth-subtract-0.1" && c.at("name") != "subgrid-cavity")
                continue;
            const auto regions = build_region_programs(scene, axis);
            const auto check = [&](cubey::math::Vec3 p) {
                const float error =
                    std::abs(region_distance(p, scene, regions) - authored_distance(p, scene));
                max_error = std::max(max_error, error);
                ++checked;
                if (!std::isfinite(error) || error > 2e-6F)
                    throw std::runtime_error("regional equivalence failed: " +
                                             c.at("name").get<std::string>());
            };
            for (unsigned i = 0; i < 1024; ++i)
                check({distribution(random), distribution(random), distribution(random)});
            for (unsigned i = 0; i <= axis; ++i)
                for (int a = 0; a < 3; ++a)
                    for (float delta : {-1.F / (256 * 7 * 16), 0.F, 1.F / (256 * 7 * 16)}) {
                        cubey::math::Vec3 p{.03125F};
                        p[a] = static_cast<float>(i) / static_cast<float>(axis) - .5F + delta;
                        check(p);
                    }
        }
        const auto fallback = build_region_programs(scene, 16, 0);
        if (fallback.fallback_cells != 4096 || !fallback.references.empty())
            throw std::runtime_error("bounded fallback not explicit");
        for (unsigned i = 0; i < 16; ++i) {
            const cubey::math::Vec3 p{distribution(random), distribution(random),
                                      distribution(random)};
            if (region_distance(p, scene, fallback) != authored_distance(p, scene))
                throw std::runtime_error("capacity fallback changed source");
        }
    }
    // A complemented operator subtree exercises sign propagation through a skipped
    // subtraction, rather than only complementing a primitive cutter.
    auto doc = manifest.at("cases").at(0).at("scene");
    const auto ball = [](std::string name, float radius) {
        return nlohmann::json{{"name", name}, {"type", "sphere"}, {"radius", radius}};
    };
    doc["root"] = {{"name", "outer"},
                   {"type", "subtract"},
                   {"children",
                    {ball("body", .3F),
                     {{"name", "nested"},
                      {"type", "subtract"},
                      {"children", {ball("cutter", .2F), ball("island", .1F)}}}}}};
    const auto scene = parse_authored_scene(doc.dump());
    const auto regions = build_region_programs(scene, 16);
    for (unsigned i = 0; i < 4096; ++i) {
        const cubey::math::Vec3 p{distribution(random), distribution(random), distribution(random)};
        const auto error =
            std::abs(region_distance(p, scene, regions) - authored_distance(p, scene));
        if (error > 2e-6F)
            throw std::runtime_error("nested complement pruning failed");
    }
    for (unsigned code = 1; code <= 7; ++code) {
        AuthoredScene transformed;
        const auto q = glm::normalize(cubey::math::Quat{.8F, .2F, -.3F, .1F});
        transformed.program.push_back({{.02F, -.03F, .04F, float(code)},
                                       {q.x, q.y, q.z, q.w},
                                       {.1F, .08F, .13F, .02F},
                                       {1.1F, 0, 0, 0}});
        const auto box = authored_model_bounds(transformed);
        for (unsigned i = 0; i < 8192; ++i) {
            const cubey::math::Vec3 p{distribution(random), distribution(random),
                                      distribution(random)};
            if (authored_distance(p, transformed) <= 1.F / (256 * 7 * 32) &&
                (box.empty || glm::any(glm::lessThan(p, box.low)) ||
                 glm::any(glm::greaterThan(p, box.high))))
                throw std::runtime_error("transformed primitive sublevel excludes source");
        }
    }
    if (!authored_model_bounds(AuthoredScene{}).empty)
        throw std::runtime_error("empty source bound is not explicit");
    std::cout << "region comparisons=" << checked << " max_error=" << max_error
              << " fallback_cases=32 nested_complement_points=4096 bound_points=" << bound_points
              << " far_points=" << far_points << " known_surface_points=" << known_surface_points
              << '\n';
}
