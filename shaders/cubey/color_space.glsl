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
