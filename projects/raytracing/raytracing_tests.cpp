#include "raytracing_config.h"
#include "raytracing_layout.h"
#include "raytracing_scene.h"
#include <array>
#include <iostream>
#include <map>
#include <vector>
using namespace cubey::projects::raytracing;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "expected rejection");
}
Config parse(std::initializer_list<const char*> args) {
    std::vector<std::string> strings{"raytracing"};
    for (auto arg : args)
        strings.emplace_back(arg);
    std::vector<char*> argv;
    for (auto& s : strings)
        argv.push_back(s.data());
    return parse_config(static_cast<int>(argv.size()), argv.data());
}
} // namespace
int main() {
    try {
        require(sbt_stride(24, 32, 64, 4096) == 64, "SBT pads handles to base alignment");
        require(sbt_stride(96, 32, 64, 4096) == 128, "SBT large handle region");
        require(align_address(0x1010, 64) == 0x1040, "unaligned device base");
        rejects([] { (void)sbt_stride(32, 3, 64, 4096); });
        rejects([] { (void)sbt_stride(32, 32, 64, 32); });
        rejects([] { (void)align_address(UINT64_MAX, 64); });
        require(parse({}).scene == "fixture", "default generated fixture");
        rejects([] { (void)parse({"--scene", "dragon"}); });
        rejects([] { (void)parse({"--roughness", "0"}); });
        rejects([] { (void)parse({"--yaw", "nan"}); });
        rejects([] { (void)parse({"--fov", "0"}); });
        rejects([] { (void)parse({"--exercise-retry"}); });
        rejects([] { (void)parse({"--ior", ".9"}); });
        rejects([] { (void)parse({"--max-events", "13"}); });
        rejects([] { (void)parse({"--max-total-rays", "257"}); });
        rejects([] { (void)parse({"--attenuation-red", "0"}); });
        rejects([] { (void)parse({"--optics-contract"}); });
        for (const char* name : {"reflection-fixture", "refraction-fixture", "slab"}) {
            Config fixture;
            fixture.scene = name;
            const auto scene = make_scene(fixture);
            require(scene.instances.size() == 6, "optical fixture instance roster");
            for (const auto& instance : scene.instances) {
                require(instance.material < scene.materials.size(), "checked optical material ID");
                if (scene.materials[instance.material].optics.x != 2)
                    continue;
                const auto& mesh = scene.meshes[instance.mesh];
                std::map<std::pair<std::uint32_t, std::uint32_t>, int> edges;
                double volume = 0;
                for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
                    const auto a = mesh.indices[i], b = mesh.indices[i + 1],
                               c = mesh.indices[i + 2];
                    const auto x = glm::dvec3(mesh.vertices[a].position),
                               y = glm::dvec3(mesh.vertices[b].position),
                               z = glm::dvec3(mesh.vertices[c].position);
                    volume += glm::dot(x, glm::cross(y, z)) / 6;
                    for (auto edge : {std::pair{a, b}, std::pair{b, c}, std::pair{c, a}}) {
                        if (edge.first > edge.second)
                            std::swap(edge.first, edge.second);
                        ++edges[edge];
                    }
                }
                require(volume > 0, "glass volume outward winding and positive volume");
                for (const auto& [edge, count] : edges) {
                    (void)edge;
                    require(count == 2, "glass manifold edge incidence");
                }
            }
        }
        rejects([] { (void)parse({"--query-output", "test.bin"}); });
        rejects([] { (void)parse({"--headless", "--frames", "2", "--query-output", "test.bin"}); });
        auto c = parse({"--headless", "--frames", "1", "--view", "depth", "--no-shadows"});
        require(c.view == "depth" && !c.shadows, "typed controls");
        Config copy;
        schema(copy).apply_json(schema(c).template_json());
        require(copy.view == c.view && copy.shadows == c.shadows, "config roundtrip");
        std::cout << "raytracing: SBT boundaries and config admission pass\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
