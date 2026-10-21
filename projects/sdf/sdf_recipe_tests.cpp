#include "sdf_recipe.h"

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
    for (const auto count : {128U, 129U, 256U, 257U}) {
        require(construction_prefix(count, 8.0, 8.0) == count,
                "last frame must contain every edit");
        require(construction_prefix(count, -1.0, 8.0) == 0, "negative time must clamp to empty");
    }
    require(construction_prefix(2048, 4.0, 8.0) == 1024,
            "construction depends on time rather than FPS");
    bool rejected = false;
    try {
        box.half_extent.x = std::numeric_limits<float>::quiet_NaN();
        static_cast<void>(gpu_edit(box));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "nonfinite geometry must be rejected before GPU upload");
}
