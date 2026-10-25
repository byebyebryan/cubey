#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cubey/core/math.h>
#include <cubey/engine/environment_transition.h>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace cubey::projects::pbr_materials {
struct PresentationSpecimen {
    std::string_view material, shape;
    math::Vec3 position, rotation;
};
inline std::vector<PresentationSpecimen> presentation_layout(std::string_view name) {
    if (name == "library") {
        constexpr std::array names{
            "plain",        "limestone",       "marble",      "clay",
            "ceramic",      "bronze",          "glass",       "wood",
            "rusted-steel", "kraft-cardboard", "woven-cloth", "frosted-glass"};
        std::vector<PresentationSpecimen> result;
        for (std::size_t i = 0; i < names.size(); ++i)
            result.push_back({names[i],
                              "sphere",
                              {(static_cast<float>(i % 4) - 1.5F) * .252F,
                               (1 - static_cast<float>(i / 4)) * .195F, 0},
                              {0, 0, 0}});
        return result;
    }
    if (name != "surfaces" && name != "contrast")
        throw std::invalid_argument("presentation must be surfaces, contrast or library");
    return {
        {"wood", "cut-block", {-.34F, .27F, 0}, {.10F, .38F, 0}},
        {"rusted-steel", "sphere", {.34F, .27F, .015F}, {0, 0, 0}},
        {"woven-cloth", "swatch", {-.34F, -.27F, .02F}, {-.08F, -.18F, -.08F}},
        {name == "surfaces" ? "frosted-glass" : "bronze", "sphere", {.34F, -.27F, 0}, {0, 0, 0}}};
}
inline EnvironmentTransitionSample presentation_lighting(std::string_view cycle, double seconds) {
    if (cycle != "dusk" && cycle != "dawn")
        throw std::invalid_argument("lighting cycle must be dusk or dawn");
    return sample_environment_transition({.duration_seconds = 8,
                                          .sun_elevation_begin = cycle == "dawn" ? -4.0F : 14.0F,
                                          .sun_elevation_end = cycle == "dawn" ? 14.0F : -4.0F,
                                          .sun_azimuth = -155,
                                          .cloud_seconds_begin = 300,
                                          .cloud_time_scale = 30},
                                         seconds);
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
