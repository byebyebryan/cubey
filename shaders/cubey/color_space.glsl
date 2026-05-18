vec3 cubey_srgb_to_linear(vec3 color) {
    vec3 clamped = clamp(color, vec3(0.0), vec3(1.0));
    vec3 lo = clamped / 12.92;
    vec3 hi = pow((clamped + vec3(0.055)) / 1.055, vec3(2.4));
    return mix(hi, lo, lessThanEqual(clamped, vec3(0.04045)));
}

vec3 cubey_linear_to_srgb(vec3 color) {
    vec3 clamped = clamp(color, vec3(0.0), vec3(1.0));
    vec3 lo = clamped * 12.92;
    vec3 hi = (1.055 * pow(clamped, vec3(1.0 / 2.4))) - vec3(0.055);
    return mix(hi, lo, lessThanEqual(clamped, vec3(0.0031308)));
}

float cubey_wrap_unit(float value) {
    float wrapped = mod(value, 1.0);
    if (wrapped < 0.0) {
        wrapped += 1.0;
    }
    return wrapped;
}

vec3 cubey_hue_chroma_to_rgb(float hue, float chroma) {
    float h = cubey_wrap_unit(hue) * 6.0;
    float x = chroma * (1.0 - abs(mod(h, 2.0) - 1.0));
    if (h < 1.0) {
        return vec3(chroma, x, 0.0);
    }
    if (h < 2.0) {
        return vec3(x, chroma, 0.0);
    }
    if (h < 3.0) {
        return vec3(0.0, chroma, x);
    }
    if (h < 4.0) {
        return vec3(0.0, x, chroma);
    }
    if (h < 5.0) {
        return vec3(x, 0.0, chroma);
    }
    return vec3(chroma, 0.0, x);
}

vec3 cubey_hsv_to_srgb(vec3 hsv) {
    float saturation = clamp(hsv.y, 0.0, 1.0);
    float value = clamp(hsv.z, 0.0, 1.0);
    float chroma = value * saturation;
    vec3 rgb = cubey_hue_chroma_to_rgb(hsv.x, chroma);
    return rgb + vec3(value - chroma);
}

vec3 cubey_hsl_to_srgb(vec3 hsl) {
    float saturation = clamp(hsl.y, 0.0, 1.0);
    float lightness = clamp(hsl.z, 0.0, 1.0);
    float chroma = (1.0 - abs((2.0 * lightness) - 1.0)) * saturation;
    vec3 rgb = cubey_hue_chroma_to_rgb(hsl.x, chroma);
    return rgb + vec3(lightness - (chroma * 0.5));
}

vec3 cubey_hsv_to_linear(vec3 hsv) {
    return cubey_srgb_to_linear(cubey_hsv_to_srgb(hsv));
}

vec3 cubey_hsl_to_linear(vec3 hsl) {
    return cubey_srgb_to_linear(cubey_hsl_to_srgb(hsl));
}
