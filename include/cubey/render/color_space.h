#pragma once

#include <cubey/core/math.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace cubey::render {

[[nodiscard]] inline float srgb_channel_to_linear(float value) {
    const float clamped = std::clamp(value, 0.0F, 1.0F);
    if (clamped <= 0.04045F) {
        return clamped / 12.92F;
    }
    return std::pow((clamped + 0.055F) / 1.055F, 2.4F);
}

[[nodiscard]] inline float linear_channel_to_srgb(float value) {
    const float clamped = std::clamp(value, 0.0F, 1.0F);
    if (clamped <= 0.0031308F) {
        return clamped * 12.92F;
    }
    return (1.055F * std::pow(clamped, 1.0F / 2.4F)) - 0.055F;
}

[[nodiscard]] inline std::array<float, 3> srgb_to_linear_rgb(std::array<float, 3> color) {
    return {
        srgb_channel_to_linear(color[0]),
        srgb_channel_to_linear(color[1]),
        srgb_channel_to_linear(color[2]),
    };
}

[[nodiscard]] inline std::array<float, 3> linear_to_srgb_rgb(std::array<float, 3> color) {
    return {
        linear_channel_to_srgb(color[0]),
        linear_channel_to_srgb(color[1]),
        linear_channel_to_srgb(color[2]),
    };
}

[[nodiscard]] inline math::Vec4 srgb_to_linear_rgba(math::Vec4 color) {
    return {
        srgb_channel_to_linear(color.r),
        srgb_channel_to_linear(color.g),
        srgb_channel_to_linear(color.b),
        color.a,
    };
}

[[nodiscard]] inline math::Vec4 linear_to_srgb_rgba(math::Vec4 color) {
    return {
        linear_channel_to_srgb(color.r),
        linear_channel_to_srgb(color.g),
        linear_channel_to_srgb(color.b),
        color.a,
    };
}

} // namespace cubey::render
