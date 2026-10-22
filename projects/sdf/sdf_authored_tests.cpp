#include "sdf_authored.h"
#include "sdf_history.h"
#include <algorithm>
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
    auto patterned_json = document(sphere("pattern-ball", {}, .2F));
    patterned_json["materials"][0]["procedural"] = {{"type", "marble"},
                                                    {"seed", 73},
                                                    {"scale", 4.5},
                                                    {"secondary_color", {.12, .16, .18}},
                                                    {"offset", {.03, .04, .05}}};
    const auto patterned = parse_authored_scene(patterned_json.dump());
    const auto patterned_reload = parse_authored_scene(serialize_authored_scene(patterned));
    require(authored_geometry_hash(patterned) == authored_geometry_hash(patterned_reload) &&
                authored_appearance_hash(patterned) == authored_appearance_hash(patterned_reload),
            "procedural material persistence changed identity");
    auto changed_seed = patterned;
    changed_seed.patterns[0].settings.y += 1;
    require(authored_geometry_hash(changed_seed) == authored_geometry_hash(patterned) &&
                authored_appearance_hash(changed_seed) != authored_appearance_hash(patterned),
            "procedural edit changed geometry or lost appearance identity");
    auto moved_pattern = patterned;
    std::swap(moved_pattern.patterns[0], moved_pattern.patterns[1]);
    require(authored_appearance_hash(moved_pattern) != authored_appearance_hash(patterned),
            "pattern material ownership lost its appearance identity");
    for (int mutation = 0; mutation < 5; ++mutation) {
        auto bad = patterned_json;
        auto& p = bad["materials"][0]["procedural"];
        if (mutation == 0)
            p["seed"] = .5;
        if (mutation == 1)
            p["type"] = "unknown";
        if (mutation == 2)
            p["grain_scale"] = 0;
        if (mutation == 3)
            p["vein_direction"] = {0, 0, 0};
        if (mutation == 4)
            p["bump_strength"] = .1;
        rejected(bad);
    }
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
    const Json torus_json{
        {"type", "torus"}, {"name", "seal"}, {"major_radius", .15}, {"minor_radius", .04}};
    const auto seal = scene(torus_json);
    close(authored_distance({}, seal), .11F);
    close(authored_distance({.15F, 0, 0}, seal), -.04F);
    close(authored_distance({.19F, 0, 0}, seal), 0);
    close(authored_distance({.15F, .1F, 0}, seal), .06F);
    const Json cone_json{{"type", "capped-cone"},
                         {"name", "taper"},
                         {"bottom_radius", .2},
                         {"top_radius", .1},
                         {"half_length", .2}};
    const auto taper = scene(cone_json);
    close(authored_distance({}, taper), -.15F / std::sqrt(1.0625F));
    close(authored_distance({.2F, -.2F, 0}, taper), 0);
    close(authored_distance({.1F, .2F, 0}, taper), 0);
    close(authored_distance({0, .3F, 0}, taper), .1F);
    close(authored_distance({.3F, 0, 0}, taper), .15F / std::sqrt(1.0625F));
    auto equal_ends = cone_json;
    equal_ends["bottom_radius"] = .1;
    for (const auto p : {Vec3{}, Vec3{.2F, .3F, 0}, Vec3{.05F, -.1F, .02F}})
        close(authored_distance(p, scene(equal_ends)), authored_distance(p, cylinder));
    auto tip = cone_json;
    tip["top_radius"] = 0;
    close(authored_distance({}, scene(tip)), -.1F / std::sqrt(1.25F));
    close(authored_distance({0, .2F, 0}, scene(tip)), 0);
    auto round_json = cone_json;
    round_json["type"] = "round-cone";
    const auto rounded = scene(round_json);
    close(authored_distance({0, -.4F, 0}, rounded), 0);
    close(authored_distance({0, .3F, 0}, rounded), 0);
    // Independent reference: minimize distance to linearly interpolated
    // endpoint spheres, rather than reusing the analytic tangent/cap split.
    for (const auto radii : {cubey::math::Vec2{.2F, .1F}, cubey::math::Vec2{.03F, .14F},
                             cubey::math::Vec2{.08F, 0}, cubey::math::Vec2{.3F, .01F}}) {
        auto json = round_json;
        json["bottom_radius"] = radii.x;
        json["top_radius"] = radii.y;
        json["half_length"] = .12;
        const auto test = scene(json);
        for (int x = 0; x <= 8; ++x) {
            for (int y = -8; y <= 8; ++y) {
                const Vec3 p{static_cast<float>(x) * .04F, static_cast<float>(y) * .04F, .017F};
                const auto sphere_family = [&](double t) {
                    const double dy = static_cast<double>(p.y) + .12 - .24 * t;
                    return std::sqrt(static_cast<double>(p.x) * p.x + dy * dy +
                                     static_cast<double>(p.z) * p.z) -
                           (static_cast<double>(radii.x) * (1 - t) + radii.y * t);
                };
                double low = 0, high = 1;
                for (int iteration = 0; iteration < 70; ++iteration) {
                    const double left = (2 * low + high) / 3, right = (low + 2 * high) / 3;
                    if (sphere_family(left) < sphere_family(right))
                        high = right;
                    else
                        low = left;
                }
                const float reference = static_cast<float>(sphere_family((low + high) / 2));
                close(authored_distance(p, test), reference, 2e-5F);
                Vec3 gradient{0};
                for (int axis = 0; axis < 3; ++axis) {
                    auto a = p, b = p;
                    a[axis] -= .0001F;
                    b[axis] += .0001F;
                    gradient[axis] =
                        (authored_distance(b, test) - authored_distance(a, test)) / .0002F;
                }
                require(glm::length(gradient) < 1.003F, "rounded taper violates distance bound");
            }
        }
    }
    auto round_equal = round_json;
    round_equal["bottom_radius"] = round_equal["top_radius"] = .05;
    round_equal["half_length"] = .15;
    for (const auto p : {Vec3{}, Vec3{.12F, .27F, .02F}, Vec3{.05F, -.1F, .02F}})
        close(authored_distance(p, scene(round_equal)), authored_distance(p, capsule));
    auto contained = round_json;
    contained["half_length"] = .03;
    const auto container_sphere = scene(sphere("container", {0, -.03F, 0}, .2F));
    for (const auto p : {Vec3{}, Vec3{.2F, .1F, .02F}, Vec3{.05F, -.1F, .02F}})
        close(authored_distance(p, scene(contained)), authored_distance(p, container_sphere));
    auto invalid_round = round_json;
    invalid_round["bottom_radius"] = invalid_round["top_radius"] = 0;
    rejected(document(invalid_round));
    for (const auto kind : {0, 1, 2, 3}) {
        auto bad = kind < 2 ? torus_json : cone_json;
        if (kind == 0)
            bad["minor_radius"] = .15;
        if (kind == 1)
            bad["major_radius"] = 0;
        if (kind == 2)
            bad["top_radius"] = -.1;
        if (kind == 3) {
            bad["bottom_radius"] = 0;
            bad["top_radius"] = 0;
        }
        rejected(document(bad));
    }
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
    // Smooth joins use the same world-distance tie band as hard union. Tiny
    // curve blends otherwise amplify CPU/GPU rounding into different parts.
    const auto tied = scene(smooth);
    const auto first_part = authored_surface({}, tied).part;
    for (float x : {-2e-7F, 0.0F, 2e-7F}) {
        const auto at_tie = authored_surface({x, 0, 0}, tied);
        require(at_tie.part == first_part, "smooth union lost its stable tie owner");
        close(at_tie.material.base_roughness.x, 1);
    }
    close(authored_surface({2e-5F, 0, 0}, tied).material.base_roughness.z, 1);
    Json soft_cut{{"type", "smooth-subtract"},
                  {"name", "rounded-recess"},
                  {"radius", .04},
                  {"cut_material", "blue"},
                  {"children", {sphere("body", {}, .2F), sphere("tool", {0, 0, .2F}, .1F)}}};
    const auto recess = scene(soft_cut);
    close(authored_distance({0, 0, .05F}, recess), -.05F);
    close(authored_surface({0, 0, .15F}, recess).material.base_roughness.z, 1);
    close(authored_surface({0, 0, -.1F}, recess).material.base_roughness.x, 1);
    auto coincident_cut = soft_cut;
    coincident_cut["children"][1] = sphere("tool", {}, .2F);
    const auto cut_tie = scene(coincident_cut);
    for (float x : {.2F - 2e-7F, .2F, .2F + 2e-7F})
        close(authored_surface({x, 0, 0}, cut_tie).material.base_roughness.x, 1);
    close(authored_surface({.2F - 2e-5F, 0, 0}, cut_tie).material.base_roughness.z, 1);
    auto hard_cut = soft_cut;
    hard_cut["type"] = "subtract";
    for (int i = -20; i <= 20; ++i) {
        const Vec3 p{.13F, 0, static_cast<float>(i) * .01F};
        const float extra = authored_distance(p, recess) - authored_distance(p, scene(hard_cut));
        require(extra >= -1e-6F && extra <= .010001F, "smooth subtraction exceeds its blend bound");
    }
    soft_cut.erase("cut_material");
    close(authored_surface({0, 0, .15F}, scene(soft_cut)).material.base_roughness.x, 1);
    soft_cut["radius"] = 0;
    rejected(document(soft_cut));
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
    for (std::size_t i = 0; i < authored_max_leaves; ++i)
        many.push_back(sphere("s" + std::to_string(i), {}, .01F));
    const auto maximum = scene({{"name", "max"}, {"type", "union"}, {"children", many}});
    require(maximum.program.size() == 2 * authored_max_leaves - 1 &&
                maximum.leaf_count == authored_max_leaves,
            "authored leaf boundary lost nodes");
    many.push_back(sphere("overflow", {}, .01F));
    rejected(document({{"name", "max"}, {"type", "union"}, {"children", many}}));
    require(authored_program_bytes(nullptr).size() >= 16 &&
                authored_program_bytes(&maximum).size() ==
                    16 + (2 * authored_max_leaves - 1) * sizeof(SceneInstruction),
            "GPU packing mismatch");
    auto maximum_history_json = Json::parse(maximum.document);
    maximum_history_json["history"] = Json::array();
    for (std::size_t i = 0; i < authored_max_leaves; ++i)
        maximum_history_json["history"].push_back(
            {{"node", "s" + std::to_string(i)}, {"label", "Capacity stroke"}});
    const auto maximum_history = parse_authored_scene(maximum_history_json.dump());
    for (const auto count :
         {std::size_t{0}, std::size_t{1}, authored_max_leaves / 2, authored_max_leaves})
        require(authored_history_scene(maximum_history, count).leaf_count == count,
                "maximum history lost leaves on seek/reset");
    close(authored_distance({}, maximum_history), -.01F);

    // Check the dependency proof against exhaustive prefix compilation for
    // every ordering of overlapping bodies, a nested negative tool and a
    // separate cut. This oracle uses the original reveal implementation.
    const Json masses = {{"name", "masses"},
                         {"type", "union"},
                         {"children", {sphere("a", {}, .2F), sphere("b", {}, .1F)}}};
    const Json tool = {{"name", "tool"},
                       {"type", "subtract"},
                       {"children", {sphere("c", {}, .08F), sphere("d", {}, .03F)}}};
    const Json shell = {{"name", "shell"}, {"type", "subtract"}, {"children", {masses, tool}}};
    const Json other = {{"name", "other"},
                        {"type", "subtract"},
                        {"children", {sphere("e", {}, .15F), sphere("f", {}, .04F)}}};
    const auto order_fixture =
        document({{"name", "all"}, {"type", "union"}, {"children", {shell, other}}});
    std::array<std::string, 6> order{"a", "b", "c", "d", "e", "f"};
    do {
        auto reference = parse_authored_scene(order_fixture.dump());
        auto candidate = order_fixture;
        candidate["history"] = Json::array();
        for (const auto& name : order) {
            reference.history.push_back(
                {.nodes = {name}, .label = name, .seconds = .65, .cutters = {}});
            candidate["history"].push_back({{"node", name}, {"label", name}});
        }
        bool expected = true;
        for (std::size_t count = 1; count < order.size(); ++count)
            expected &= authored_history_scene(reference, count).leaf_count == count;
        bool accepted = true;
        try {
            static_cast<void>(parse_authored_scene(candidate.dump()));
        } catch (const std::invalid_argument&) {
            accepted = false;
        }
        require(accepted == expected, "history dependency check disagrees with prefix oracle");
    } while (std::next_permutation(order.begin(), order.end()));
    // History must retain transform/material inheritance when a Boolean has
    // only its first visible child, and isolate later cuts from other parts.
    auto history_json = document(
        {{"name", "placed"},
         {"type", "union"},
         {"position", {.1, 0, 0}},
         {"scale", .8},
         {"material", "blue"},
         {"children",
          {{{"name", "shell"},
            {"type", "subtract"},
            {"children", {sphere("body", {}, .2F, "blue"), sphere("cut", {0, 0, .2F}, .1F)}}},
           sphere("neighbor", {0, 0, .2F}, .04F)}}});
    history_json["history"] = {{{"node", "body"}, {"label", "Body"}},
                               {{"node", "cut"}, {"label", "Cut"}, {"seconds", 1.5}},
                               {{"node", "neighbor"}, {"label", "Neighbor"}}};
    const auto history = parse_authored_scene(history_json.dump());
    const auto empty = authored_history_scene(history, 0);
    require(empty.program.empty() && authored_distance({}, empty) == 1000,
            "empty history state contains geometry");
    require(parse_authored_scene(serialize_authored_scene(empty)).program.empty(),
            "empty scene failed persistence");
    const auto body = authored_history_scene(history, 1);
    close(authored_distance({.1F, 0, .16F}, body), 0);
    close(authored_surface({.1F, 0, .16F}, body).material.base_roughness.z, 1);
    require(authored_distance({.1F, 0, .16F}, authored_history_scene(history, 2)) > 0,
            "history cut did not remove its local body");
    require(authored_distance({.1F, 0, .16F}, authored_history_scene(history, 3)) < 0,
            "history local cut erased the neighbor");
    require(authored_geometry_hash(authored_history_scene(history, 3)) ==
                    authored_geometry_hash(history) &&
                authored_appearance_hash(authored_history_scene(history, 3)) ==
                    authored_appearance_hash(history),
            "final history state differs from complete scene");
    const auto history_roundtrip = parse_authored_scene(serialize_authored_scene(history));
    require(history_roundtrip.history.size() == 3 && !history_roundtrip.history[1].cutters.empty(),
            "history persistence lost its cutter or steps");
    auto soft_history_json = history_json;
    soft_history_json["root"]["children"][0]["type"] = "smooth-subtract";
    soft_history_json["root"]["children"][0]["radius"] = .02;
    const auto soft_history = parse_authored_scene(soft_history_json.dump());
    require(!soft_history.history[1].cutters.empty(), "smooth subtraction lost its cut preview");
    require(authored_history_scene(soft_history, 1).leaf_count == 1 &&
                authored_history_scene(soft_history, 2).leaf_count == 2,
            "smooth subtraction lost body-first reveal semantics");
    auto reversed_soft_history = soft_history_json;
    std::swap(reversed_soft_history["history"][0], reversed_soft_history["history"][1]);
    rejected(reversed_soft_history);
    TimelineConfig timeline{"presentation", 12, 8, 1};
    for (std::size_t i = 0; i <= history.history.size(); ++i) {
        const auto time = authored_history_seek(timeline, history, i);
        const auto state = sample_authored_history(timeline, history, time);
        require(state.timeline.prefix == i && state.cutters.empty() && state.timeline.yaw == 0,
                "history seek did not inspect the requested committed edit");
    }
    const auto final = sample_authored_history(timeline, history, timeline.duration);
    require(final.timeline.prefix == 3 && final.timeline.yaw > 6.28F,
            "history orbit did not retain the completed object");
    bool saw_preview = false;
    std::size_t prior = 0;
    for (int frame = 0; frame <= 720; ++frame) {
        const auto state = sample_authored_history(timeline, history, frame / 60.0);
        require(state.timeline.prefix >= prior, "forward history skipped backwards");
        prior = state.timeline.prefix;
        if (!state.cutters.empty()) {
            require(state.timeline.prefix == 1, "cut preview committed the cut early");
            saw_preview = true;
        }
    }
    require(saw_preview, "history did not preview its subtractive edit");
    // One sculpting action may contain both reflected strokes and many
    // segments. Prefixes and previews must treat that action atomically.
    auto paired_json = history_json;
    paired_json["root"]["children"][0]["children"][1] = {
        {"name", "paired-tools"},
        {"type", "union"},
        {"children",
         {sphere("cut-left", {-.08F, 0, .18F}, .08F), sphere("cut-right", {.08F, 0, .18F}, .08F)}}};
    paired_json["history"][1].erase("node");
    paired_json["history"][1]["nodes"] = {"cut-left", "cut-right"};
    const auto paired = parse_authored_scene(paired_json.dump());
    require(paired.history.size() == 3 && paired.leaf_count == 4 &&
                paired.history[1].cutters.size() == 2,
            "grouped history did not collect both transformed cutters");
    const auto paired_before = authored_history_scene(paired, 1);
    const auto paired_after = authored_history_scene(paired, 2);
    require(paired_before.leaf_count == 1 && paired_after.leaf_count == 3,
            "grouped history exposed half an operation");
    for (float x : {-.08F, .08F}) {
        const Vec3 point{.1F + .8F * x, 0, .8F * .18F};
        require(authored_distance(point, paired_before) < 0 &&
                    authored_distance(point, paired_after) > 0 &&
                    authored_distance(point, authored_history_scene(paired, 1)) < 0,
                "paired cut/undo failed on a reflected side");
    }
    const auto paired_reload = parse_authored_scene(serialize_authored_scene(paired));
    require(paired_reload.history[1].nodes == paired.history[1].nodes &&
                paired_reload.history[1].cutters.size() == 2 &&
                authored_geometry_hash(paired_reload) == authored_geometry_hash(paired),
            "grouped history failed persistence");
    bool paired_preview = false;
    for (int frame = 0; frame <= 720; ++frame) {
        const auto state = sample_authored_history(timeline, paired, frame / 60.0);
        if (!state.cutters.empty()) {
            require(state.cutters.size() == 2 && state.timeline.prefix == 1,
                    "paired preview omitted a side or committed early");
            paired_preview = true;
        }
    }
    require(paired_preview, "paired history never previewed its cutters");
    for (std::size_t i = 0; i <= paired.history.size(); ++i) {
        const auto state =
            sample_authored_history(timeline, paired, authored_history_seek(timeline, paired, i));
        require(state.timeline.prefix == i && state.cutters.empty(),
                "grouped seek did not land on a committed operation");
    }
    for (int mutation = 0; mutation < 6; ++mutation) {
        auto bad = paired_json;
        auto& step = bad["history"][1];
        if (mutation == 0)
            step["node"] = "cut-left"; // Ambiguous encoding.
        if (mutation == 1)
            step["nodes"] = Json::array();
        if (mutation == 2)
            step["nodes"] = {"cut-left"}; // Missing primitive.
        if (mutation == 3)
            step["nodes"] = {"cut-left", "cut-left", "cut-right"};
        if (mutation == 4)
            step["nodes"] = {"cut-left", "unknown"};
        if (mutation == 5) { // Mixed-polarity preview would hide an addition.
            step["nodes"] = {"body", "cut-left", "cut-right"};
            bad["history"].erase(bad["history"].begin());
        }
        rejected(bad);
    }
    for (const auto mutation : {0, 1, 2, 3}) {
        auto bad = history_json;
        if (mutation == 0)
            std::swap(bad["history"][0], bad["history"][1]);
        if (mutation == 1)
            bad["history"][1]["node"] = "body";
        if (mutation == 2)
            bad["history"][1]["node"] = "shell";
        if (mutation == 3)
            bad["history"][1]["seconds"] = 0;
        rejected(bad);
    }
}
