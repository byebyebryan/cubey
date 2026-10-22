#include "sdf_authored.h"
#include <cmath>
#include <nlohmann/json.hpp>
#include <stdexcept>
using namespace cubey::projects::sdf;
using cubey::math::Vec3;
using Json = nlohmann::json;
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void close(float value, float expected, float tolerance = 1e-5F) {
    require(std::isfinite(value) && std::abs(value - expected) < tolerance,
            "authored distance or material disagrees");
}
Json sphere(std::string name, Vec3 p, float r, std::string material = "red") {
    return {{"name", name},
            {"type", "sphere"},
            {"position", {p.x, p.y, p.z}},
            {"radius", r},
            {"material", material}};
}
Json document(const Json& root) {
    return {{"format", "cubey.sdf.scene"},
            {"version", 2},
            {"name", "fixture"},
            {"root", root},
            {"materials",
             {{{"name", "red"}, {"base_color", {1, 0, 0}}, {"roughness", .5}},
              {{"name", "blue"}, {"base_color", {0, 0, 1}}, {"roughness", .8}}}}};
}
AuthoredScene scene(const Json& root) {
    return parse_authored_scene(document(root).dump());
}
void rejected(const Json& json) {
    bool failed = false;
    try {
        static_cast<void>(parse_authored_scene(json.dump()));
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, "invalid authored scene accepted");
}
} // namespace
int main() {
    const auto ball = scene(sphere("ball", {}, .2F));
    close(authored_distance({}, ball), -.2F);
    close(authored_distance({.3F, 0, 0}, ball), .1F);
    close(authored_distance({1, 0, 0}, ball), .8F); // Root clipping does not expand the shape.
    const auto capsule =
        scene({{"type", "capsule"}, {"name", "capsule"}, {"radius", .05}, {"half_length", .15}});
    close(authored_distance({0, .2F, 0}, capsule), 0);
    close(authored_distance({.05F, .1F, 0}, capsule), 0);
    close(authored_distance({0, 0, 0}, capsule), -.05F);
    const auto cylinder =
        scene({{"type", "cylinder"}, {"name", "cylinder"}, {"radius", .1}, {"half_length", .2}});
    close(authored_distance({0, .2F, 0}, cylinder), 0);
    close(authored_distance({.1F, .1F, 0}, cylinder), 0);
    close(authored_distance({.2F, .3F, 0}, cylinder), std::sqrt(.02F));
    auto moved = sphere("moved", {.1F, .2F, 0}, .05F);
    moved["scale"] = 2;
    close(authored_distance({.2F, .2F, 0}, scene(moved)), 0);
    const auto grouped =
        scene({{"name", "assembly"},
               {"type", "union"},
               {"children",
                {{{"name", "housing"},
                  {"type", "subtract"},
                  {"cut_material", "blue"},
                  {"children", {sphere("body", {}, .2F), sphere("cutter", {0, 0, .2F}, .1F)}}},
                 sphere("unaffected-part", {0, 0, .2F}, .04F, "blue")}}});
    require(authored_distance({0, 0, .2F}, grouped) < 0, "a local cut changed another group");
    close(authored_surface({0, 0, .1F}, grouped).material.base_roughness.z, 1);
    auto inherited_json = document(
        {{"name", "cut"},
         {"type", "subtract"},
         {"children", {sphere("body", {}, .2F), sphere("cutter", {0, 0, .2F}, .1F, "blue")}}});
    close(authored_surface({0, 0, .1F}, parse_authored_scene(inherited_json.dump()))
              .material.base_roughness.x,
          1);
    Json smooth{{"name", "blend"},
                {"type", "smooth-union"},
                {"radius", .08},
                {"children",
                 {sphere("left", {-.12F, 0, 0}, .1F), sphere("right", {.12F, 0, 0}, .1F, "blue")}}};
    auto blend = scene(smooth);
    close(authored_distance({}, blend), 0);
    close(authored_surface({}, blend).material.base_roughness.x, .5F);
    close(authored_surface({}, blend).material.base_roughness.z, .5F);
    smooth["blend_materials"] = false;
    close(authored_surface({}, scene(smooth)).material.base_roughness.x, 1);
    // A nested blend must retain three contributors, rather than truncating to two IDs.
    const auto nested =
        scene({{"name", "outer"},
               {"type", "smooth-union"},
               {"radius", .08},
               {"children",
                {Json{{"name", "inner"},
                      {"type", "smooth-union"},
                      {"radius", .08},
                      {"children", {sphere("a", {}, .1F), sphere("b", {}, .1F, "blue")}}},
                 sphere("c", {}, .12F)}}});
    const auto sample = authored_surface({}, nested);
    require(sample.material.base_roughness.x > .5F && sample.material.base_roughness.z > 0,
            "nested material blending lost a contributor");
    // The selected smooth polynomial has a convex combination of child gradients.
    for (int x = -20; x <= 20; ++x) {
        Vec3 p{static_cast<float>(x) * .01F, .06F, .03F}, gradient{0};
        for (int axis = 0; axis < 3; ++axis) {
            auto a = p, b = p;
            a[axis] -= .0001F;
            b[axis] += .0001F;
            gradient[axis] = (authored_distance(b, blend) - authored_distance(a, blend)) / .0002F;
        }
        require(glm::length(gradient) < 1.002F,
                "smooth operator violates conservative gradient bound");
    }
    Json transformed{
        {"name", "group"},
        {"type", "union"},
        {"position", {.1, 0, 0}},
        {"scale", 2},
        {"rotation_xyzw", {0, 0, std::sqrt(.5), std::sqrt(.5)}},
        {"children", {sphere("child", {.1F, 0, 0}, .03F), sphere("other", {-.1F, 0, 0}, .03F)}}};
    close(authored_distance({.1F, .2F, 0}, scene(transformed)), -.06F);
    const auto restored = parse_authored_scene(serialize_authored_scene(grouped));
    require(authored_geometry_hash(grouped) == authored_geometry_hash(restored) &&
                authored_appearance_hash(grouped) == authored_appearance_hash(restored) &&
                grouped.part_names == restored.part_names,
            "authored scene roundtrip lost content");
    auto recolored = restored;
    recolored.materials[0].base_roughness.x = .3F;
    require(authored_geometry_hash(recolored) == authored_geometry_hash(restored) &&
                authored_appearance_hash(recolored) != authored_appearance_hash(restored),
            "material changes invalidated geometry or lost their own identity");
    require(authored_appearance_hash(parse_authored_scene(serialize_authored_scene(recolored))) ==
                authored_appearance_hash(recolored),
            "edited material did not persist");
    auto corrupt = Json::parse(serialize_authored_scene(grouped));
    corrupt["geometry_hash"] = "bad";
    rejected(corrupt);
    corrupt = Json::parse(serialize_authored_scene(grouped));
    corrupt["appearance_hash"] = "bad";
    rejected(corrupt);
    for (const auto value : {0, -1}) {
        auto invalid = document(sphere("a", {}, .1F));
        invalid["root"]["scale"] = value;
        rejected(invalid);
    }
    auto invalid = document(sphere("a", {}, .1F));
    invalid["root"]["type"] = "unknown";
    rejected(invalid);
    invalid = document(sphere("a", {}, .1F));
    invalid["root"]["material"] = "missing";
    rejected(invalid);
    invalid = document(smooth);
    invalid["root"]["radius"] = 0;
    rejected(invalid);
    invalid = document(smooth);
    invalid["root"]["children"][1]["name"] = "left";
    rejected(invalid);
    invalid = document(smooth);
    invalid["root"]["children"].erase(1);
    rejected(invalid);
    Json many = Json::array();
    for (int i = 0; i < 64; ++i)
        many.push_back(sphere("s" + std::to_string(i), {}, .01F));
    const auto maximum = scene({{"name", "max"}, {"type", "union"}, {"children", many}});
    require(maximum.program.size() == 127 && maximum.leaf_count == 64,
            "64-leaf boundary lost nodes");
    many.push_back(sphere("overflow", {}, .01F));
    rejected(document({{"name", "max"}, {"type", "union"}, {"children", many}}));
    require(authored_program_bytes(nullptr).size() >= 16 &&
                authored_program_bytes(&maximum).size() == 16 + 127 * sizeof(SceneInstruction),
            "GPU packing mismatch");
}
