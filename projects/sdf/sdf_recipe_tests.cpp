#include "sdf_recipe.h"
#include "sdf_timeline.h"

#include <cmath>
#include <limits>
#include <stdexcept>

using namespace cubey::projects::sdf;
using cubey::math::Vec3;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void close(float value, float expected) {
    require(std::abs(value - expected) < 1.0e-5F, "box distance disagrees with known geometry");
}
} // namespace

int main() {
    BoxEdit box;
    close(edit_distance({0, 0, 0}, box), -0.2005F);
    close(edit_distance({0.3F, 0, 0}, box), 0.0995F);
    close(edit_distance({0.2F, 0.2F, 0.2F}, box), -0.0005F);
    box.center = {0.1F, -0.1F, 0.1F};
    box.half_extent = {0.1F, 0.2F, 0.15F};
    box.rotation = cubey::math::angle_axis_quat(0.5F, {0, 1, 0});
    close(edit_distance(box.center + box.rotation * Vec3{0.2F, 0, 0}, box), 0.0995F);
    const auto packed = gpu_edit(box);
    close(packed.center_operation.x, 0.1F);
    close(packed.inverse_rotation.y, -std::sin(0.25F));
    close(packed.inverse_rotation.w, std::cos(0.25F));
    const auto cut = make_recipe("cut", 1, 1);
    const Vec3 removed = cut.edits[1].center;
    require(recipe_distance(removed, cut.edits) > 0, "subtraction must remove the cutter interior");
    auto reversed = cut.edits;
    std::swap(reversed[0], reversed[1]);
    require(recipe_distance(removed, reversed) < 0, "CSG ordering must remain significant");
    const auto first = make_recipe("sticks", 42, 257);
    const auto second = make_recipe("sticks", 42, 257);
    require(first.edits.size() == 257, "recipe must retain every edit beyond batch boundaries");
    require(recipe_hash(first) == recipe_hash(second),
            "same seed must produce the same explicit edits");
    require(recipe_hash(first) != recipe_hash(make_recipe("sticks", 43, 257)),
            "seed must change scene");
    for (const auto& recipe : {first, cut, Recipe{}}) {
        const auto restored = parse_recipe(serialize_recipe(recipe));
        require(
            restored.edits.size() == recipe.edits.size() &&
                recipe_hash(restored) == recipe_hash(recipe),
            "recipe roundtrip must retain all edits and operations, including the partial tail");
    }
    bool corrupt_rejected = false;
    try {
        auto corrupt = serialize_recipe(first);
        const auto hash = corrupt.find("\"hash\": \"") + 9;
        corrupt[hash] = corrupt[hash] == '0' ? '1' : '0';
        static_cast<void>(parse_recipe(corrupt));
    } catch (const std::exception&) {
        corrupt_rejected = true;
    }
    require(corrupt_rejected, "corrupt recipe content/hash must not load as a complete sculpture");
    for (const auto& edit : first.edits) {
        validate_edit(edit);
        const float radius = glm::length(edit.center);
        require(radius >= 0.28999F && radius <= 0.30001F,
                "stick shell must match reference dimensions");
    }
    const auto negative = make_recipe("negative-sticks", 42, 72);
    require(recipe_hash(negative) == 0xc92ff9ee08a5af98ULL,
            "the published rounded-cube recipe must remain unchanged");
    require(negative.edits.size() == 73 && negative.edits.front().operation == Operation::Fill,
            "negative sticks must retain one solid plus every cutter");
    require(recipe_hash(negative) == recipe_hash(make_recipe("negative-sticks", 42, 72)),
            "negative sticks must reproduce the explicit recipe");
    require(recipe_hash(negative) != recipe_hash(make_recipe("negative-sticks", 7, 72)),
            "negative sticks seed must change geometry");
    auto prefix = negative;
    prefix.edits.resize(25);
    require(recipe_hash(prefix) == recipe_hash(make_recipe("negative-sticks", 42, 24)),
            "increasing cutter count must retain the earlier cuts unchanged");
    for (std::size_t i = 1; i < negative.edits.size(); ++i) {
        const auto& cutter = negative.edits[i];
        validate_edit(cutter);
        require(cutter.operation == Operation::Remove &&
                    edit_distance(cutter.center, negative.edits.front()) < 0,
                "every cutter must intersect the initial solid");
        require(recipe_distance(cutter.center, std::span(negative.edits).first(i + 1)) > 0,
                "applying a cutter must leave its center empty");
    }
    const auto intact = make_recipe("negative-sticks", 42, 0);
    require(intact.edits.size() == 1 && recipe_distance({0, 0, 0}, intact.edits) < 0,
            "zero cuts must produce the intact solid");
    const auto negative_loaded = parse_recipe(serialize_recipe(negative));
    require(recipe_hash(negative_loaded) == recipe_hash(negative) &&
                negative_loaded.generator == negative.generator,
            "subtractive recipes must roundtrip all operations and metadata");
    bool too_many_cutters = false;
    try {
        static_cast<void>(make_recipe("negative-sticks", 42, 65536));
    } catch (const std::invalid_argument&) {
        too_many_cutters = true;
    }
    require(too_many_cutters, "generated cutters plus base must fit the recipe loader limit");
    validate_carve_recipe(negative_loaded);
    validate_carve_recipe(intact);
    const auto spherical = make_recipe("negative-sphere", 42, 72);
    validate_carve_recipe(spherical);
    const auto& sphere = spherical.edits.front();
    close(edit_distance({0, 0, 0}, sphere), -0.36F);
    for (const auto direction :
         {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, -1}, glm::normalize(Vec3{1, 2, 3})}) {
        close(edit_distance(direction * 0.36F, sphere), 0);
        close(edit_distance(direction * 0.40F, sphere), 0.04F);
    }
    auto moved_sphere = sphere;
    moved_sphere.center = {0.05F, -0.06F, 0.04F};
    moved_sphere.rotation = cubey::math::euler_xyz_quat({0.3F, 0.7F, -0.4F});
    close(edit_distance(moved_sphere.center + glm::normalize(Vec3{1, 2, 3}) * 0.36F, moved_sphere),
          0);
    auto spherical_tail = spherical;
    auto box_tail = negative;
    spherical_tail.edits.erase(spherical_tail.edits.begin());
    box_tail.edits.erase(box_tail.edits.begin());
    require(recipe_hash(spherical_tail) == recipe_hash(box_tail),
            "sphere comparison must keep every cutter identical to the rounded cube");
    const auto sphere_loaded = parse_recipe(serialize_recipe(spherical));
    require(recipe_hash(sphere_loaded) == recipe_hash(spherical),
            "zero-core spheres must survive explicit recipe serialization");
    require(make_recipe("negative-sphere", 42, 0).edits.size() == 1,
            "zero cutters must retain the spherical base");
    const auto thin_negative = make_recipe("negative-stick-ball", 42, 2048);
    validate_carve_recipe(thin_negative);
    require(thin_negative.edits.size() == 2049,
            "negative stick ball must retain every slender cutter plus its base");
    close(edit_distance({0.30F, 0, 0}, thin_negative.edits.front()), 0);
    auto thin_positive = thin_negative;
    thin_positive.edits.erase(thin_positive.edits.begin());
    for (auto& edit : thin_positive.edits) {
        require(edit.operation == Operation::Remove,
                "negative stick ball must contain only removals after the base");
        edit.operation = Operation::Fill;
    }
    require(recipe_hash(thin_positive) == recipe_hash(make_recipe("sticks", 42, 2048)),
            "slender subtraction must exactly reuse the additive sticks' geometry");
    auto thin_prefix = thin_negative;
    thin_prefix.edits.resize(257);
    require(recipe_hash(thin_prefix) == recipe_hash(make_recipe("negative-stick-ball", 42, 256)),
            "thin cutter counts must preserve exact earlier edits");
    require(recipe_hash(thin_negative) ==
                recipe_hash(parse_recipe(serialize_recipe(thin_negative))),
            "thin subtraction must roundtrip its explicit recipe");
    require(recipe_hash(thin_prefix) != recipe_hash(make_recipe("negative-stick-ball", 43, 256)),
            "thin cutter seed must change geometry");
    const auto thin_intact = make_recipe("negative-stick-ball", 42, 0);
    require(thin_intact.edits.size() == 1, "zero slender cuts must retain the base");
    validate_carve_recipe(thin_intact);
    bool too_many_thin_cutters = false;
    try {
        static_cast<void>(make_recipe("negative-stick-ball", 42, 65536));
    } catch (const std::invalid_argument&) {
        too_many_thin_cutters = true;
    }
    require(too_many_thin_cutters, "thin cutters plus base must fit the recipe loader limit");
    for (const auto extent : {Vec3{0, 0.1F, 0.1F}, Vec3{-0.1F}, Vec3{0}}) {
        bool rejected_core = false;
        try {
            auto invalid = sphere;
            invalid.half_extent = extent;
            invalid.rounding = glm::all(glm::equal(extent, Vec3{0})) ? 0.0F : 0.02F;
            validate_edit(invalid);
        } catch (const std::invalid_argument&) {
            rejected_core = true;
        }
        require(rejected_core, "mixed-zero, negative, and unrounded point cores remain invalid");
    }
    for (const auto& invalid : {first, Recipe{}}) {
        bool rejected_carve = false;
        try {
            validate_carve_recipe(invalid);
        } catch (const std::invalid_argument&) {
            rejected_carve = true;
        }
        require(rejected_carve, "carve must reject empty and additive multi-fill recipes");
    }
    for (const auto count : {128U, 129U, 256U, 257U}) {
        require(construction_prefix(count, 8.0, 8.0) == count,
                "last frame must contain every edit");
        require(construction_prefix(count, -1.0, 8.0) == 0, "negative time must clamp to empty");
    }
    require(construction_prefix(2048, 4.0, 8.0) == 1024,
            "construction depends on time rather than FPS");
    const TimelineConfig timeline{.motion = "presentation"};
    require(sample_timeline(timeline, 2048, 0).prefix == 0, "presentation must begin empty");
    require(sample_timeline(timeline, 2048, 4).prefix == 1024,
            "construction half-time must be half the edits");
    require(sample_timeline(timeline, 2048, 8).prefix == 2048,
            "construction endpoint must include the tail");
    require(sample_timeline(timeline, 2048, 21).debug == 3 &&
                sample_timeline(timeline, 2048, 24).debug == 4,
            "presentation must show traversal then brick diagnostics");
    for (unsigned tick = 0; tick <= 24; ++tick) {
        const auto at30 = sample_timeline(timeline, 257, static_cast<double>(tick * 30) / 30);
        const auto at60 = sample_timeline(timeline, 257, static_cast<double>(tick * 60) / 60);
        require(at30.prefix == at60.prefix && at30.yaw == at60.yaw && at30.debug == at60.debug,
                "logical state must be independent of capture FPS");
    }
    const TimelineConfig carve{
        .motion = "carve", .duration = 24, .construction_seconds = 16, .orbit_turns = 0.5F};
    require(sample_timeline(carve, 73, 0).prefix == 1 &&
                sample_timeline(carve, 73, 1.99).prefix == 1,
            "carving must hold the intact base at the start");
    for (unsigned slot = 0; slot < 4; ++slot) {
        const double start = 2.0 + static_cast<double>(slot) * 1.5;
        const auto preview = sample_timeline(carve, 73, start + 0.3);
        require(preview.prefix == 1 + slot && preview.cutter_index == 1 + slot &&
                    preview.cutter_opacity > 0.99F,
                "slow cut cue must identify the next stored edit before committing it");
        require(sample_timeline(carve, 73, start + 0.6 - 1e-6).prefix == 1 + slot &&
                    sample_timeline(carve, 73, start + 0.6 + 1e-6).prefix == 2 + slot,
                "each slow cut must commit exactly once at its logical boundary");
    }
    require(sample_timeline(carve, 73, 8).prefix == 5 &&
                sample_timeline(carve, 73, 12).prefix == 39 &&
                sample_timeline(carve, 73, 16).prefix == 73 &&
                sample_timeline(carve, 73, 24).prefix == 73,
            "carving must retain the intro cuts and publish every final edit");
    close(sample_timeline(carve, 73, 16).yaw, 0);
    close(sample_timeline(carve, 73, 24).yaw, 3.141592654F);
    for (std::size_t cuts : {0U, 1U, 3U, 4U, 72U}) {
        std::size_t previous = 1;
        for (unsigned tick = 0; tick <= 1440; ++tick) {
            const auto sample = sample_timeline(carve, cuts + 1, static_cast<double>(tick) / 60);
            require(sample.prefix >= previous && sample.prefix <= cuts + 1,
                    "carving must never lose the base or invent cuts, including small recipes");
            require(sample.cutter_index == 0 || sample.cutter_index == sample.prefix,
                    "a preview must refer to the next edit outside the built prefix");
            previous = sample.prefix;
            if (tick % 2 == 0) {
                const auto at30 =
                    sample_timeline(carve, cuts + 1, static_cast<double>(tick / 2) / 30);
                require(sample.prefix == at30.prefix && sample.cutter_index == at30.cutter_index &&
                            sample.cutter_opacity == at30.cutter_opacity && sample.yaw == at30.yaw,
                        "carving and cue states must agree at shared 30/60 FPS logical times");
            }
        }
        require(previous == cuts + 1 && sample_timeline(carve, cuts + 1, 0).prefix == 1,
                "all small recipes must finish and permit backward seeking to the base");
    }
    bool invalid_carve_time = false;
    try {
        auto invalid = carve;
        invalid.construction_seconds = invalid.duration;
        validate_timeline(invalid);
    } catch (const std::invalid_argument&) {
        invalid_carve_time = true;
    }
    require(invalid_carve_time, "carve must reserve nonzero time for the final orbit");
    bool rejected = false;
    try {
        box.half_extent.x = std::numeric_limits<float>::quiet_NaN();
        static_cast<void>(gpu_edit(box));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "nonfinite geometry must be rejected before GPU upload");
}
