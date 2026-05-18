#pragma once

#include <cubey/core/math.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace cubey::render {

struct HsvColor {
    float hue = 0.0F;
    float saturation = 0.0F;
    float value = 0.0F;
};

struct HslColor {
    float hue = 0.0F;
    float saturation = 0.0F;
    float lightness = 0.0F;
};

[[nodiscard]] inline float wrap_unit(float value) {
    float wrapped = std::fmod(value, 1.0F);
    if (wrapped < 0.0F) {
        wrapped += 1.0F;
    }
    return wrapped;
}

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

[[nodiscard]] inline std::array<float, 3> hue_chroma_to_rgb(float hue, float chroma) {
    const float h = wrap_unit(hue) * 6.0F;
    const float x = chroma * (1.0F - std::fabs(std::fmod(h, 2.0F) - 1.0F));
    if (h < 1.0F) {
        return {chroma, x, 0.0F};
    }
    if (h < 2.0F) {
        return {x, chroma, 0.0F};
    }
    if (h < 3.0F) {
        return {0.0F, chroma, x};
    }
    if (h < 4.0F) {
        return {0.0F, x, chroma};
    }
    if (h < 5.0F) {
        return {x, 0.0F, chroma};
    }
    return {chroma, 0.0F, x};
}

[[nodiscard]] inline std::array<float, 3> hsv_to_srgb(HsvColor color) {
    const float saturation = std::clamp(color.saturation, 0.0F, 1.0F);
    const float value = std::clamp(color.value, 0.0F, 1.0F);
    const float chroma = value * saturation;
    const std::array<float, 3> rgb = hue_chroma_to_rgb(color.hue, chroma);
    const float match = value - chroma;
    return {rgb[0] + match, rgb[1] + match, rgb[2] + match};
}

[[nodiscard]] inline std::array<float, 3> hsl_to_srgb(HslColor color) {
    const float saturation = std::clamp(color.saturation, 0.0F, 1.0F);
    const float lightness = std::clamp(color.lightness, 0.0F, 1.0F);
    const float chroma = (1.0F - std::fabs((2.0F * lightness) - 1.0F)) * saturation;
    const std::array<float, 3> rgb = hue_chroma_to_rgb(color.hue, chroma);
    const float match = lightness - (chroma * 0.5F);
    return {rgb[0] + match, rgb[1] + match, rgb[2] + match};
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

[[nodiscard]] inline std::array<float, 3> hsv_to_linear_rgb(HsvColor color) {
    return srgb_to_linear_rgb(hsv_to_srgb(color));
}

[[nodiscard]] inline std::array<float, 3> hsl_to_linear_rgb(HslColor color) {
    return srgb_to_linear_rgb(hsl_to_srgb(color));
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
