#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cubey/core/math.h>
#include <stdexcept>
#include <string_view>

namespace cubey::projects::pbr_materials {
struct PresentationSpecimen {
    std::string_view material, shape;
    math::Vec3 position, rotation;
};
inline std::array<PresentationSpecimen, 4> presentation_layout(std::string_view name) {
    if (name != "surfaces" && name != "contrast")
        throw std::invalid_argument("presentation must be surfaces or contrast");
    return {
        {{"wood", "cut-block", {-.34F, .27F, 0}, {.10F, .38F, 0}},
         {"rusted-steel", "sphere", {.34F, .27F, .015F}, {0, 0, 0}},
         {"woven-cloth", "swatch", {-.34F, -.27F, .02F}, {-.08F, -.18F, -.08F}},
         {name == "surfaces" ? "frosted-glass" : "bronze", "sphere", {.34F, -.27F, 0}, {0, 0, 0}}}};
}
inline float presentation_yaw(double seconds) {
    const auto t = static_cast<float>(std::clamp((seconds - 1.5) / 5, 0.0, 1.0));
    return glm::radians(20.0F) * t * t * (3 - 2 * t);
}
inline float specimen_thickness(std::string_view specimen) {
    // Forward transport estimates the specimen depth; SDF measures ray segments.
    return specimen == "swatch" ? .036F : specimen == "cut-block" ? .32F : .5F;
}
} // namespace cubey::projects::pbr_materials
