#ifndef CUBEY_ATMOSPHERE_GLSL
#define CUBEY_ATMOSPHERE_GLSL

const float CUBEY_ATMOSPHERE_PI = 3.14159265359;

vec2 cubey_atmosphere_ray_sphere_intersection(vec3 ray_origin, vec3 ray_direction,
                                              vec3 sphere_center, float sphere_radius) {
    vec3 offset = ray_origin - sphere_center;
    float b = dot(offset, ray_direction);
    float c = dot(offset, offset) - sphere_radius * sphere_radius;
    float discriminant = b * b - c;
    if (discriminant < 0.0) {
        return vec2(1.0, -1.0);
    }
    float root = sqrt(discriminant);
    return vec2(-b - root, -b + root);
}

vec3 cubey_atmosphere_transmittance_from_depth(vec3 rayleigh_scattering, float mie_extinction,
                                               vec3 ozone_absorption, float rayleigh_depth,
                                               float mie_depth, float ozone_depth) {
    vec3 rayleigh_extinction = rayleigh_scattering * rayleigh_depth;
    vec3 mie_extinction_rgb = vec3(mie_extinction * mie_depth);
    vec3 ozone_extinction = ozone_absorption * ozone_depth;
    return exp(-(rayleigh_extinction + mie_extinction_rgb + ozone_extinction));
}

float cubey_atmosphere_rayleigh_phase(float cos_theta) {
    return (3.0 / (16.0 * CUBEY_ATMOSPHERE_PI)) * (1.0 + cos_theta * cos_theta);
}

float cubey_atmosphere_mie_phase(float cos_theta, float anisotropy) {
    float g = clamp(anisotropy, 0.0, 0.98);
    float g2 = g * g;
    float denom = pow(max(1.0 + g2 - 2.0 * g * cos_theta, 0.0001), 1.5);
    return (3.0 / (8.0 * CUBEY_ATMOSPHERE_PI)) *
           ((1.0 - g2) * (1.0 + cos_theta * cos_theta)) /
           max((2.0 + g2) * denom, 0.0001);
}

float cubey_atmosphere_limb_visibility(vec3 sample_position, vec3 light_direction,
                                       vec3 planet_center, float ground_radius,
                                       float angular_radius, float min_softness) {
    vec3 to_planet_center = planet_center - sample_position;
    float center_distance = length(to_planet_center);
    float planet_angular_radius =
        asin(clamp(ground_radius / max(center_distance, 0.0001), 0.0, 1.0));
    float light_center_angle =
        acos(clamp(dot(light_direction, normalize(to_planet_center)), -1.0, 1.0));
    float limb_clearance = light_center_angle - planet_angular_radius;
    float softness = max(angular_radius * 4.0, min_softness);
    return smoothstep(-softness, softness, limb_clearance);
}

#endif
