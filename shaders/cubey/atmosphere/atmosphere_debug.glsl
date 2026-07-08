#ifndef CUBEY_ATMOSPHERE_DEBUG_GLSL
#define CUBEY_ATMOSPHERE_DEBUG_GLSL

vec3 render_aerial_perspective_debug(vec3 ray_origin, vec3 ray_direction, vec3 planet_center) {
    vec2 atmosphere_hit = ray_sphere_intersection(ray_origin, ray_direction, planet_center,
                                                  atmosphere.radii_ground.y);
    float max_t = max(atmosphere_hit.y, 0.0);
    float ramp = smoothstep(-1.0, 1.0, frag_ndc.x);
    float scene_t = min(max_t, mix(8.0, 320.0, ramp));
    CubeyAtmosphereSample atmosphere_sample = integrate_atmosphere(ray_origin, ray_direction, 0.0,
                                                                   scene_t, planet_center);
    vec3 scene_color = cubey_srgb_to_linear(mix(vec3(0.16, 0.18, 0.14),
                                                vec3(0.55, 0.55, 0.50), ramp));
    return scene_color * atmosphere_sample.transmittance + atmosphere_sample.color;
}

vec3 debug_optical_depth(CubeyAtmosphereOpticalDepth depth) {
    return vec3(depth.rayleigh * 0.020, depth.mie * 0.080, depth.ozone * 0.035);
}

#endif // CUBEY_ATMOSPHERE_DEBUG_GLSL
