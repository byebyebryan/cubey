#include "raytracing_config.h"
#include "raytracing_dragon_support.h"
#include "raytracing_environment.h"
#include "raytracing_forward.h"
#include "raytracing_layout.h"
#include "raytracing_material_geometry.h"
#include "raytracing_materials.h"
#include "raytracing_optical_asset.h"
#include "raytracing_scene.h"
#include "raytracing_trace_space.h"
#include <array>
#include <cstring>
#include <cubey/render/material_specimen_mesh.h>
#include <iostream>
#include <map>
#include <tuple>
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
void check_trace_space() {
    using cubey::math::Mat4;
    using cubey::math::Vec3;
    using cubey::math::Vec4;
    const TraceSpace space{{0, .3534899950027466F, 0}};
    Mat4 body(1);
    body[3] = Vec4(space.world_origin, 1);
    const auto local_body = space.from_world(body);
    require(local_body == Mat4(1), "body origin cancels exactly without changing its vertices");
    Mat4 physical(1);
    physical[0] = {0, 2, 0, 0};
    physical[1] = {-3, .25F, 0, 0};
    physical[2] = {0, 0, .5F, 0};
    physical[3] = {.125F, -.0625F, .25F, 1};
    const auto traced = space.from_world(physical);
    for (int i = 0; i < 3; ++i)
        require(traced[i] == physical[i], "translation preserves rotations, scale and shear");
    for (const Vec3 point : {Vec3(0), Vec3(.25F, .5F, -.125F), Vec3(-.5F, .125F, .25F)}) {
        const auto world_point = Vec3(physical * Vec4(point, 1));
        const auto trace_point = Vec3(traced * Vec4(point, 1));
        require(glm::length(space.to_world(trace_point) - world_point) < 2e-7F,
                "instance transform and point conversion describe the same physical scene");
        require(glm::length(space.from_world(world_point) - trace_point) < 2e-7F,
                "camera/ray points share the instance translation");
    }
    const TraceSpace unchanged;
    const auto same = unchanged.from_world(physical);
    require(std::memcmp(&physical, &same, sizeof(physical)) == 0,
            "unselected zero-origin transforms remain bit-identical");
}
void check_dragon_footing() {
    using cubey::math::Vec3;
    Mesh subject;
    for (const auto p : {Vec3{-.3F, -.2F, -.16F}, Vec3{.36F, -.2F, -.12F}, Vec3{.29F, -.194F, .18F},
                         Vec3{-.25F, -.2F, .14F}, Vec3{-.7F, .5F, .6F}})
        subject.vertices.push_back({{p, 1}, {0, 1, 0, 0}});
    const auto rock = make_dragon_rock_footing(subject, .201F);
    const auto lifted = make_dragon_rock_footing(subject, .451F);
    require(!rock.indices.empty() && rock.indices.size() % 3 == 0,
            "footing has complete triangles");
    std::map<std::tuple<float, float, float>, std::uint32_t> positions;
    std::vector<std::uint32_t> welded;
    Vec3 minimum(100), maximum(-100);
    for (std::size_t i = 0; i < rock.vertices.size(); ++i) {
        const auto p = Vec3(rock.vertices[i].position);
        const auto n = Vec3(rock.vertices[i].normal);
        require(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
                    std::abs(glm::length(n) - 1) < 1e-5F,
                "footing positions and unit normals are finite");
        require(std::abs(p.y) < 1e-6F || std::abs(p.y + .030F) < 1e-6F,
                "slab vertices lie on exactly two horizontal planes");
        require(glm::length(Vec3(lifted.vertices.at(i).position) - p - Vec3(0, .25F, 0)) < 1e-6F,
                "footing follows subject vertical placement");
        minimum = glm::min(minimum, p);
        maximum = glm::max(maximum, p);
        const auto [entry, inserted] = positions.try_emplace(
            std::tuple{p.x, p.y, p.z}, static_cast<std::uint32_t>(positions.size()));
        welded.push_back(entry->second);
    }
    require(maximum.x - minimum.x < .82F && maximum.z - minimum.z < .5F &&
                maximum.y - minimum.y < .12F && minimum.x > -.4F && maximum.x > .36F,
            "footing fits the low contact region rather than the entire subject");
    struct Edge {
        int count = 0;
        int direction = 0;
    };
    std::map<std::pair<std::uint32_t, std::uint32_t>, Edge> edges;
    double volume = 0;
    for (std::size_t i = 0; i < rock.indices.size(); i += 3) {
        const auto a = Vec3(rock.vertices.at(rock.indices[i]).position);
        const auto b = Vec3(rock.vertices.at(rock.indices[i + 1]).position);
        const auto c = Vec3(rock.vertices.at(rock.indices[i + 2]).position);
        const auto normal = glm::cross(b - a, c - a);
        const auto supplied = Vec3(rock.vertices.at(rock.indices[i]).normal);
        if (std::abs(supplied.y) > .5F)
            require(a.y == b.y && b.y == c.y && std::abs(std::abs(supplied.y) - 1) < 1e-6F,
                    "slab caps are flat with constant vertical normals");
        else
            require(std::abs(supplied.y) < 1e-6F, "slab cuts are vertical planes");
        for (const auto& vertex : rock.vertices)
            require(glm::dot(supplied, Vec3(vertex.position) - a) <= 2e-6F,
                    "slab is convex: all vertices lie inside every outward face plane");
        require(glm::length(normal) > 1e-10F &&
                    glm::dot(normal, Vec3(rock.vertices.at(rock.indices[i]).normal)) > 0,
                "footing triangles have nonzero area and consistent normals");
        volume += glm::dot(glm::dvec3(a), glm::cross(glm::dvec3(b), glm::dvec3(c))) / 6.;
        for (std::size_t j = 0; j < 3; ++j) {
            const auto from = welded.at(rock.indices[i + j]);
            const auto to = welded.at(rock.indices[i + (j + 1) % 3]);
            auto& edge = edges[std::minmax(from, to)];
            ++edge.count;
            edge.direction += from < to ? 1 : -1;
        }
    }
    for (const auto& [key, edge] : edges)
        require(edge.count == 2 && edge.direction == 0,
                "footing is a closed consistently wound surface");
    require(volume > 0, "footing has outward winding");
    rejects([] { (void)make_dragon_rock_footing({}, 0); });
    rejects([&] { (void)make_dragon_rock_footing(subject, NAN); });
    subject.vertices.resize(1);
    rejects([&] { (void)make_dragon_rock_footing(subject, 0); });
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
        require(parse({}).dragon_support == "plinth", "existing dragon support default");
        require(parse({}).support_material == "matte", "existing support material default");
        rejects([] { (void)parse({"--dragon-support", "unknown"}); });
        rejects([] { (void)parse({"--dragon-support", "rock"}); });
        rejects([] { (void)parse({"--subject-material", "marble"}); });
        rejects([] {
            (void)parse({"--scene", "reflection-dragon-solo", "--dragon-support", "rock",
                         "--support-material", "marble"});
        });
        rejects([] { (void)parse({"--support-material", "marble"}); });
        Config rock_config;
        schema(rock_config)
            .apply_json({{"scene", "reflection-dragon-solo"},
                         {"dragon-support", "rock"},
                         {"subject-material", "clay"},
                         {"support-material", "marble"},
                         {"environment", "atmosphere"}});
        Config rock_copy;
        schema(rock_copy).apply_json(schema(rock_config).template_json());
        require(rock_copy.dragon_support == "rock", "support survives saved config roundtrip");
        require(rock_copy.subject_material == "clay" && rock_copy.support_material == "marble",
                "marble base and clay dragon survive saved config roundtrip independently");
        check_dragon_footing();
        check_trace_space();
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
        // Admission is explicit and asset-bound; config tests need no downloaded mesh.
        const auto existing_file =
            (std::filesystem::path(CUBEY_MATERIAL_RECIPE_DIR) / "glass.json").string();
        auto fixed_opaque_off = parse({"--scene", "dragon-materials", "--model",
                                       existing_file.c_str(), "--transport-mode", "fixed-opaque"});
        require(fixed_opaque_transport(fixed_opaque_off) &&
                    fixed_opaque_off.anti_aliasing == "off" &&
                    frame_sample_target(fixed_opaque_off) == 4 &&
                    per_frame_fixed_transport(fixed_opaque_off),
                "fixed-opaque admits the bronze dragon with four fixed primary lanes and AA off");
        Config fixed_opaque_copy;
        schema(fixed_opaque_copy).apply_json(schema(fixed_opaque_off).template_json());
        require(fixed_opaque_transport(fixed_opaque_copy) &&
                    fixed_opaque_copy.anti_aliasing == "off" &&
                    frame_sample_target(fixed_opaque_copy) == 4,
                "fixed-opaque Off selection survives saved config roundtrip");
        auto fixed_opaque_taa = parse({"--scene", "dragon-materials", "--model",
                                       existing_file.c_str(), "--transport-mode", "fixed-opaque",
                                       "--anti-aliasing", "taa", "--fixed-opaque-samples", "2"});
        require(fixed_opaque_transport(fixed_opaque_taa) &&
                    fixed_opaque_taa.anti_aliasing == "taa" &&
                    frame_sample_target(fixed_opaque_taa) == 2,
                "fixed-opaque TAA keeps the same selected two primary lanes");
        Config fixed_opaque_taa_copy;
        schema(fixed_opaque_taa_copy).apply_json(schema(fixed_opaque_taa).template_json());
        require(fixed_opaque_taa_copy.anti_aliasing == "taa" &&
                    frame_sample_target(fixed_opaque_taa_copy) == 2,
                "fixed-opaque TAA and sample count survive saved config roundtrip");
        auto fixed_sequence = parse({"--scene", "dragon-materials", "--model",
                                     existing_file.c_str(), "--transport-mode", "fixed-opaque",
                                     "--headless", "--headless-render-only", "--frames", "14",
                                     "--fixed-opaque-sequence"});
        require(fixed_sequence.fixed_opaque_sequence &&
                    fixed_opaque_sequence_phase(0, 14) == "hold" &&
                    fixed_opaque_sequence_phase(4, 14) == "orbit" &&
                    fixed_opaque_sequence_phase(9, 14) == "stop" &&
                    fixed_opaque_sequence_phase(11, 14) == "cut" &&
                    fixed_opaque_sequence_yaw_offset(12, 14) == -.16F,
                "fixed-opaque deterministic sequence covers hold, orbit, stop and cut");
        auto fixed_video_sequence = parse({"--scene", "dragon-materials", "--model",
                                           existing_file.c_str(), "--transport-mode", "fixed-opaque",
                                           "--headless", "--capture", "video", "--frames", "96",
                                           "--fps", "30", "--fixed-opaque-sequence"});
        require(fixed_opaque_sequence_phase(20, 96) == "orbit" &&
                    fixed_opaque_sequence_phase(60, 96) == "stop" &&
                    fixed_opaque_sequence_phase(72, 96) == "cut" &&
                    fixed_opaque_sequence_phase(80, 96) == "post-cut-hold" &&
                    fixed_video_sequence.fixed_opaque_sequence,
                "fixed-opaque 96-frame video sequence uses stable pose and cut indices");
        rejects([&] {
            (void)parse({"--scene", "dragon-materials", "--model", existing_file.c_str(),
                         "--anti-aliasing", "taa"});
        });
        rejects([&] {
            (void)parse({"--scene", "dragon-optics", "--model", existing_file.c_str(),
                         "--transport-mode", "fixed-opaque"});
        });
        rejects([&] {
            (void)parse({"--scene", "dragon-optics", "--model", existing_file.c_str(),
                         "--renderer", "forward", "--transport-mode", "fixed-opaque",
                         "--no-shadows"});
        });
        rejects([&] {
            (void)parse({"--scene", "dragon-materials", "--model", existing_file.c_str(),
                         "--transport-mode", "fixed-opaque", "--material", "glass"});
        });
        rejects([&] {
            (void)parse({"--scene", "dragon-materials", "--model", existing_file.c_str(),
                         "--transport-mode", "fixed-opaque", "--no-bump"});
        });
        rejects([&] {
            (void)parse({"--scene", "dragon-materials", "--model", existing_file.c_str(),
                         "--transport-mode", "fixed-opaque", "--fixed-opaque-samples", "3"});
        });
        rejects([&] {
            (void)parse({"--scene", "dragon-materials", "--model", existing_file.c_str(),
                         "--transport-mode", "fixed-opaque", "--samples", "8"});
        });
        rejects([&] {
            (void)parse({"--scene", "dragon-materials", "--model", existing_file.c_str(),
                         "--transport-mode", "fixed-opaque", "--demo-samples", "2"});
        });
        rejects([&] {
            (void)parse({"--scene", "dragon-materials", "--model", existing_file.c_str(),
                         "--transport-mode", "fixed-opaque", "--fixed-opaque-sequence"});
        });
        auto optics_config = parse({"--scene", "dragon-optics", "--model", existing_file.c_str()});
        require(optics_config.renderer == "rt" && optics_config.transport_mode == "sampled",
                "sampled RT remains the ordinary default");
        require(optics_config.controls, "native controls remain visible by default");
        auto clean_config = parse({"--no-controls"});
        Config clean_copy;
        schema(clean_copy).apply_json(schema(clean_config).template_json());
        require(!clean_config.controls && !clean_copy.controls,
                "clean presentation is explicit and survives saved views");
        auto demo_config = parse({"--scene", "dragon-optics", "--model", existing_file.c_str(),
                                  "--transport-mode", "demo", "--samples", "8192"});
        require(frame_sample_target(demo_config) == 4,
                "demo frames never acquire an offline sample advantage");
        Config demo_copy;
        schema(demo_copy).apply_json(schema(demo_config).template_json());
        require(demo_copy.transport_mode == "demo" && frame_sample_target(demo_copy) == 4,
                "demo policy survives saved views");
        auto forward_config =
            parse({"--scene", "dragon-optics", "--model", existing_file.c_str(), "--renderer",
                   "forward", "--no-shadows", "--forward-thickness", ".2"});
        require(forward_config.renderer == "forward" && !demo_transport(forward_config),
                "forward comparison is a separate non-RT rendering path");
        forward_config.forward_transmission_source = "environment";
        Config forward_copy;
        schema(forward_copy).apply_json(schema(forward_config).template_json());
        require(forward_copy.forward_transmission_source == "environment",
                "forward transmission source survives saved views");
        schema(forward_copy).apply_json({{"forward-transmission-source", "hybrid"}});
        Config hybrid_copy;
        schema(hybrid_copy).apply_json(schema(forward_copy).template_json());
        require(hybrid_copy.forward_transmission_source == "hybrid",
                "hybrid transmission survives saved views");
        for (auto n :
             {cubey::math::Vec3(1, 0, 0), cubey::math::Vec3(0, 1, 0), cubey::math::Vec3(0, 0, 1),
              glm::normalize(cubey::math::Vec3(.3F, .8F, .2F))}) {
            Vertex source{{.1F, .2F, .3F, 1}, {n, 0}};
            const auto converted = forward_vertex(source);
            const cubey::math::Vec3 tangent(converted.tangent);
            require(converted.position == cubey::math::Vec3(source.position) &&
                        converted.normal == n && std::abs(glm::length(tangent) - 1) < 1e-6F &&
                        std::abs(glm::dot(n, tangent)) < 1e-6F && converted.tangent.w == 1,
                    "forward conversion preserves geometry and supplies a valid tangent frame");
        }
        auto forward_debug = parse({"--scene", "dragon-optics", "--model", existing_file.c_str(),
                                    "--renderer", "forward", "--no-shadows", "--headless", "--view",
                                    "transmission-ray", "--hdr-output", "audit.bin"});
        require(forward_debug.view == "transmission-ray",
                "forward exposes production ray diagnostics");
        rejects([] { (void)parse({"--view", "transmission-ray"}); });
        rejects([] { (void)parse({"--transport-mode", "demo"}); });
        rejects([] { (void)parse({"--renderer", "forward"}); });
        rejects([&] {
            (void)parse({"--scene", "dragon-optics", "--model", existing_file.c_str(), "--renderer",
                         "forward"});
        });
        rejects([&] {
            (void)parse({"--scene", "dragon-optics", "--model", existing_file.c_str(),
                         "--forward-thickness", "nan"});
        });
        require(optics_config.material == "glass" && optics_config.max_events == 128 &&
                    optics_config.smooth_normals && optics_config.max_path_rays == 1024 &&
                    optics_config.max_total_rays == 4096 &&
                    optics_config.reflection_source == "scene" &&
                    optics_config.transmission_source == "scene",
                "optical dragon uses full-scene sharp glass defaults");
        auto geometric_config = parse(
            {"--scene", "dragon-optics", "--model", existing_file.c_str(), "--no-smooth-normals"});
        require(!geometric_config.smooth_normals, "geometric glass remains selectable");
        Config geometric_copy;
        schema(geometric_copy).apply_json(schema(geometric_config).template_json());
        require(!geometric_copy.smooth_normals, "glass normal choice survives saved config");
        require(optics_config.firefly_clamp == 100,
                "optical dragon defaults to the reviewed HDR contribution cap");
        auto raw_config = parse(
            {"--scene", "dragon-optics", "--model", existing_file.c_str(), "--firefly-clamp", "0"});
        Config raw_copy;
        schema(raw_copy).apply_json(schema(raw_config).template_json());
        require(raw_config.firefly_clamp == 0 && raw_copy.firefly_clamp == 0,
                "explicit raw transport survives scene defaults and saved config");
        require(parse({}).firefly_clamp == 0 && workshop.firefly_clamp == 0 &&
                    parse({"--scene", "dragon-materials", "--model", existing_file.c_str()})
                            .firefly_clamp == 0,
                "other scenes retain raw defaults");
        auto clamped_config = parse({"--scene", "dragon-optics", "--model", existing_file.c_str(),
                                     "--firefly-clamp", "1000"});
        Config clamped_copy;
        schema(clamped_copy).apply_json(schema(clamped_config).template_json());
        require(clamped_copy.firefly_clamp == 1000,
                "HDR clamp survives saved config independently");
        rejects([] { (void)parse({"--firefly-clamp", "1"}); });
        for (const char* invalid : {"-1", "nan", "inf", "1000001"})
            rejects([&] {
                (void)parse({"--scene", "dragon-optics", "--model", existing_file.c_str(),
                             "--firefly-clamp", invalid});
            });
        require(parse({"--scene", "dragon-optics", "--model", existing_file.c_str(), "--max-events",
                       "256", "--max-path-rays", "2048", "--max-total-rays", "8192"})
                        .max_events == 256,
                "deep budget is isolated to optical dragon");
        rejects([&] {
            (void)parse({"--scene", "dragon-optics", "--model", existing_file.c_str(),
                         "--reflection-source", "environment"});
        });
        rejects([] { (void)parse({"--hide-dragon"}); });
        Mesh unqualified;
        rejects([&] { admit_optical_dragon(unqualified, existing_file); });
        WorkshopState optics(optics_config);
        require(optics.sharp_only && !optics.opaque_only,
                "optical policy is independent of opaque dragon");
        std::size_t sharp_supported = 0;
        for (std::size_t i = 0; i < optics.catalog.size(); ++i)
            sharp_supported += optics.supports(i);
        require(sharp_supported == 11, "optical dragon excludes only rough-interface glass");
        const auto last_good =
            cubey::asset::serialize_material_recipe(optics.recipes.at(optics.selected));
        auto rough_interface = optics.recipes.at(optics.selected);
        rough_interface.library.optical_options.x = 1;
        rejects([&] { optics.replace(optics.selected, rough_interface); });
        require(cubey::asset::serialize_material_recipe(optics.recipes.at(optics.selected)) ==
                    last_good,
                "unsupported interface edit preserves last valid recipe");
        std::vector<WorkshopVolume> optical_volumes{
            {{-1, 0, -1}, {1, 1, 1}, 0, Instance::RecipeRole::Selected},
            {{-1, -.1F, -1},
             {1, -.01F, 1},
             static_cast<std::uint32_t>(optics.recipes.size()),
             Instance::RecipeRole::Support}};
        const std::vector<Scene::Route> optical_routes{{"dragon", 0, 2}};
        validate_workshop_view(optical_volumes, optical_routes, optics, optics_config, {0, 2, 2});
        rejects([&] {
            validate_workshop_view(optical_volumes, optical_routes, optics, optics_config,
                                   {0, .5F, 0});
        });
        optical_volumes[1].maximum.y = .01F;
        rejects([&] {
            validate_workshop_view(optical_volumes, optical_routes, optics, optics_config,
                                   {0, 2, 2});
        });
        Config sampled_config;
        sampled_config.scene = "dragon-materials";
        WorkshopState sampled(sampled_config);
        require(sampled.opaque_only && sampled.recipes.at(sampled.selected).id == "bronze" &&
                    workshop_route(sampled_config) == "dragon",
                "sampled dragon has independent scene routing and opaque material policy");
        std::size_t supported = 0;
        for (std::size_t i = 0; i < sampled.catalog.size(); ++i) {
            if (sampled.supports(i)) {
                ++supported;
                require(sampled.catalog[i].recipe.optics.settings.y == 0,
                        "every admitted dragon material is opaque");
            } else
                rejects([&] { sampled.replace(i, sampled.catalog[i].recipe); });
        }
        require(supported == 10, "ten canonical opaque materials are available on the dragon");
        for (const std::string id : {"glass", "frosted-glass", "gallery"}) {
            sampled_config.material = id;
            rejects([&] { WorkshopState rejected(sampled_config); });
        }
        for (const auto& recipe : {marble_base_recipe()}) {
            const auto canonical =
                std::find_if(content.catalog.begin(), content.catalog.end(),
                             [&](const auto& entry) { return entry.id == recipe.id; });
            require(canonical != content.catalog.end() &&
                        cubey::asset::material_recipe_fingerprint(recipe) ==
                            cubey::asset::material_recipe_fingerprint(canonical->recipe),
                    "marble base uses the unmodified canonical shared recipe");
            const auto packed = gpu_recipe(recipe);
            require(packed.surface.base_roughness == recipe.surface.base_roughness &&
                        packed.pattern.settings == recipe.pattern.settings &&
                        packed.material_from_object == recipe.material_from_object &&
                        packed.routing.y == 0,
                    "dragon recipe packing retains surface, pattern, domain and opaque routing");
        }
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
        const auto specimens = make_scene(workshop);
        require(specimens.routes.size() == 5 && specimens.meshes.size() == 5,
                "prepared opaque specimen routes");
        for (const auto& mesh : specimens.meshes) {
            const auto adjacent = optical_edge_neighbors(mesh);
            require(adjacent.size() == mesh.indices.size() / 3, "edge table covers each face");
            for (std::size_t face = 0; face < adjacent.size(); ++face)
                for (int edge = 0; edge < 3; ++edge) {
                    const auto other = adjacent[face][edge];
                    require(other < adjacent.size() && other != face &&
                                (adjacent[other].x == face || adjacent[other].y == face ||
                                 adjacent[other].z == face),
                            "closed edge adjacency is reciprocal");
                }
            require(mesh.admission.closed && mesh.admission.signed_volume > 0 &&
                        mesh.admission.maximum_position_snap < 1.1e-7,
                    "closed prepared workshop bodies");
            for (std::size_t j = 0; j < mesh.indices.size(); j += 3) {
                const auto& a = mesh.vertices.at(mesh.indices[j]);
                const auto& b = mesh.vertices.at(mesh.indices[j + 1]);
                const auto& c = mesh.vertices.at(mesh.indices[j + 2]);
                const auto winding = glm::cross(cubey::math::Vec3(b.position - a.position),
                                                cubey::math::Vec3(c.position - a.position));
                require(glm::dot(winding, cubey::math::Vec3(a.normal + b.normal + c.normal)) >=
                            -1e-10F,
                        "RT specimens orient geometric normals consistently");
            }
        }
        auto damaged = cubey::render::make_material_beveled_block_mesh();
        damaged.indices.resize(damaged.indices.size() - 3);
        rejects([&] { (void)prepare_workshop_mesh(damaged); });
        auto open_boundary = specimens.meshes.at(0);
        open_boundary.indices.resize(open_boundary.indices.size() - 3);
        rejects([&] { (void)optical_edge_neighbors(open_boundary); });
        auto glass_view = workshop;
        glass_view.material = "glass";
        glass_view.distance = .1F;
        rejects([&] { (void)make_scene(glass_view); });
        const auto library_scene = make_scene(gallery);
        require(library_scene.instances.size() == 16,
                "four isolated instances and twelve gallery instances");
        auto overlapping = workshop_volumes(library_scene);
        overlapping[15].minimum = overlapping[10].minimum;
        overlapping[15].maximum = overlapping[10].maximum;
        rejects([&] {
            validate_workshop_view(overlapping, library_scene.routes, *library_scene.workshop,
                                   gallery, {0, 0, .855F});
        });
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
        rejects([] { (void)parse({"--dielectric-contract"}); });
        rejects([] {
            (void)parse({"--headless", "--dielectric-contract", "--bsdf-contract",
                         "--optics-output", "probe.bin"});
        });
        require(parse({"--headless", "--dielectric-contract", "--width", "512", "--height", "256",
                       "--optics-output", "probe.bin"})
                    .dielectric_contract,
                "isolated dielectric probe admission");
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
