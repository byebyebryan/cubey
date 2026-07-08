#ifndef CUBEY_VIEW_GLSL
#define CUBEY_VIEW_GLSL

vec2 cubey_view_ndc_from_uv(vec2 uv) {
    return (uv * 2.0) - vec2(1.0);
}

vec2 cubey_view_uv_from_ndc(vec2 ndc) {
    return (ndc * 0.5) + vec2(0.5);
}

vec2 cubey_view_frag_coord_to_uv(vec2 frag_coord, vec2 extent) {
    return (frag_coord + vec2(0.5)) / max(extent, vec2(1.0));
}

float cubey_view_distance_fade(float distance, float fade_start, float fade_end) {
    float width = max(fade_end - fade_start, 0.0001);
    return clamp((distance - fade_start) / width, 0.0, 1.0);
}

vec3 cubey_view_world_ray(vec2 uv, mat4 inverse_view_projection, vec3 camera_position) {
    vec2 ndc = cubey_view_ndc_from_uv(uv);
    vec4 far_point = inverse_view_projection * vec4(ndc, 1.0, 1.0);
    vec3 world_far = far_point.xyz / max(far_point.w, 0.000001);
    return normalize(world_far - camera_position);
}

#endif
