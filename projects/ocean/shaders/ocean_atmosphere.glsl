#ifndef CUBEY_OCEAN_ATMOSPHERE_GLSL
#define CUBEY_OCEAN_ATMOSPHERE_GLSL

vec3 ocean_sun_direction() {
    return normalize(vec3(-0.32, 0.34, -0.88));
}

vec3 ocean_sky_color(vec3 direction) {
    direction = normalize(direction);
    float horizon = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 horizon_color = cubey_srgb_to_linear(vec3(0.70, 0.80, 0.88));
    vec3 zenith_color = cubey_srgb_to_linear(vec3(0.055, 0.17, 0.34));
    vec3 color = mix(horizon_color, zenith_color, pow(horizon, 1.65));

    vec3 sun_direction = ocean_sun_direction();
    float sun_disk = pow(max(dot(direction, sun_direction), 0.0), 780.0);
    float sun_glow = pow(max(dot(direction, sun_direction), 0.0), 9.0);
    color += cubey_srgb_to_linear(vec3(1.0, 0.78, 0.43)) * sun_disk * 16.0;
    color += cubey_srgb_to_linear(vec3(0.95, 0.58, 0.28)) * sun_glow * 0.42;
    return color;
}

#endif
