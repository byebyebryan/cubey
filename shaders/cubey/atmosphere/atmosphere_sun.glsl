#ifndef CUBEY_ATMOSPHERE_SUN_GLSL
#define CUBEY_ATMOSPHERE_SUN_GLSL

float sun_chord_from_angle(float angle) {
    return 2.0 * sin(max(angle, 0.0) * 0.5);
}

float sun_halo_weight(float sun_chord, float sun_radius_chord) {
    float outside_disk = max(sun_chord - sun_radius_chord, 0.0);
    float inner_halo = exp(-outside_disk / max(sun_radius_chord * 2.2, 0.0001)) *
                       (1.0 - smoothstep(sun_radius_chord * 1.2,
                                          sun_radius_chord * 8.0, sun_chord));
    float outer_halo = exp(-outside_disk / max(sun_radius_chord * 8.5, 0.0001)) *
                       (1.0 - smoothstep(sun_radius_chord * 4.0,
                                          sun_radius_chord * 32.0, sun_chord));
    return inner_halo * 0.060 + outer_halo * 0.014;
}

vec3 sun_disk_luminance(vec3 ray_origin, vec3 ray_direction, vec3 planet_center) {
    vec3 sun_direction = normalize(atmosphere.sun_direction_radius.xyz);
    float sun_radius = max(atmosphere.sun_direction_radius.w, 0.0001);
    float sun_radius_chord = max(sun_chord_from_angle(sun_radius), 0.0001);
    float sun_chord = length(ray_direction - sun_direction);
    float disk_edge = max(fwidth(sun_chord) * 1.5, max(sun_radius_chord * 0.055, 0.00020));
    float disk = 1.0 - smoothstep(sun_radius_chord, sun_radius_chord + disk_edge, sun_chord);
    float halo = sun_halo_weight(sun_chord, sun_radius_chord);
    if (disk + halo <= 0.00001) {
        return vec3(0.0);
    }
    vec2 atmosphere_hit = ray_sphere_intersection(ray_origin, ray_direction, planet_center,
                                                  atmosphere.radii_ground.y);
    float ray_end = max(atmosphere_hit.y, 0.0);
    CubeyAtmosphereOpticalDepth depth = integrate_optical_depth(
        ray_origin, ray_direction, ray_end, planet_center, CUBEY_ATMOSPHERE_LIGHT_SAMPLE_COUNT);
    return transmittance_from_depth(depth, planet_center) * (disk + halo) *
           ATMOSPHERE_SUN_INTENSITY;
}

#endif // CUBEY_ATMOSPHERE_SUN_GLSL
