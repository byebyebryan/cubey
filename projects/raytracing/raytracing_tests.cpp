#include "raytracing_config.h"
#include "raytracing_environment.h"
#include "raytracing_layout.h"
#include "raytracing_materials.h"
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
        auto workshop = parse({"--scene", "materials"});
        require(workshop.material == "bronze" && workshop.environment == "atmosphere" &&
                    workshop.reflection_source == "environment" && !workshop.shadows &&
                    workshop.yaw == 0 && workshop.pitch == .15F && workshop.distance == 1.05F &&
                    workshop.samples == 256 && workshop.seed == 7 &&
                    workshop.throughput_cutoff == 0,
                "opt-in workshop has matched defaults");
        auto gallery = parse({"--scene", "materials", "--material", "gallery"});
        require(gallery.pitch == 0 && gallery.distance == .855F, "gallery camera defaults");
        Config workshop_copy;
        schema(workshop_copy).apply_json(schema(workshop).template_json());
        require(schema(workshop_copy).template_json() == schema(workshop).template_json(),
                "workshop schema roundtrip");
        require(parse({"--scene", "materials", "--pitch", "-.2", "--samples", "4096",
                       "--reflection-source", "scene", "--shadows", "--no-bump"})
                        .pitch == -.2F,
                "explicit workshop overrides survive defaults");
        rejects([] { (void)parse({"--samples", "0"}); });
        rejects([] { (void)parse({"--samples", "8193"}); });
        rejects([] { (void)parse({"--scene", "materials", "--environment", "legacy"}); });
        rejects([] { (void)parse({"--scene", "materials", "--throughput-cutoff", ".0001"}); });
        rejects([] { (void)parse({"--material-roughness", "nan"}); });
        rejects([] {
            (void)parse(
                {"--scene", "materials", "--material", "gallery", "--recipe-input", "bronze.json"});
        });
        WorkshopState content(workshop);
        require(content.catalog.size() == 12 && content.recipes.at(content.selected).id == "bronze",
                "RT consumes the canonical shared material catalog");
        const auto bronze_source =
            cubey::asset::material_recipe_fingerprint(content.catalog.at(content.selected).recipe);
        const auto bronze_recipe = content.recipes.at(content.selected);
        const auto records = content.gpu_records();
        require(records.at(content.selected).optics.settings.x == 1 &&
                    records.at(content.selected).surface.base_roughness.w == .32F,
                "RT bronze preserves metallic and perceptual roughness content");
        auto edited = bronze_recipe;
        edited.surface.base_roughness.w = .6F;
        content.replace(content.selected, edited);
        require(cubey::asset::material_recipe_fingerprint(content.recipes.at(content.selected)) !=
                        bronze_source &&
                    cubey::asset::material_recipe_fingerprint(
                        content.catalog.at(content.selected).recipe) == bronze_source,
                "resolved edits retain canonical source identity");
        const auto good =
            cubey::asset::serialize_material_recipe(content.recipes.at(content.selected));
        auto invalid = edited;
        invalid.surface.base_roughness.w = -1;
        rejects([&] { content.replace(content.selected, invalid); });
        const auto glass_recipe =
            std::find_if(content.catalog.begin(), content.catalog.end(),
                         [](const auto& entry) { return entry.id == "glass"; });
        rejects([&] { content.replace(content.selected, glass_recipe->recipe); });
        require(cubey::asset::serialize_material_recipe(content.recipes.at(content.selected)) ==
                    good,
                "invalid and routing-changing edits preserve the last snapshot");
        content.reload();
        require(cubey::asset::material_recipe_fingerprint(content.recipes.at(content.selected)) ==
                    bronze_source,
                "atomic catalog reload restores source values");
        for (float azimuth : {-3.F, -.6F, 0.F, .6F, 3.F}) {
            for (float elevation : {-.1F, .3F, 1.2F}) {
                const auto shared = cubey::render::atmosphere_environment_direction_from_alt_az(
                    glm::degrees(elevation), shared_sun_azimuth_degrees(azimuth));
                const auto legacy =
                    cubey::math::Vec3(std::cos(elevation) * std::sin(azimuth), std::sin(elevation),
                                      std::cos(elevation) * std::cos(azimuth));
                require(glm::length(shared - legacy) < 1e-6F,
                        "shared atmosphere preserves legacy RT sun direction");
            }
        }
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
        rejects([] { (void)parse({"--bsdf-contract"}); });
        rejects([] {
            (void)parse({"--headless", "--bsdf-contract", "--optics-contract", "--optics-output",
                         "probe.bin"});
        });
        require(parse({"--headless", "--width", "32", "--height", "16", "--bsdf-contract",
                       "--optics-output", "probe.bin"})
                    .bsdf_contract,
                "isolated BSDF contract admission");
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
