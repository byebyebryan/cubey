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
    float max_error = 0;
    for (const auto& c : manifest.at("cases")) {
        const auto scene = parse_authored_scene(c.at("scene").dump());
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
    std::cout << "region comparisons=" << checked << " max_error=" << max_error
              << " fallback_cases=32 nested_complement_points=4096\n";
}
