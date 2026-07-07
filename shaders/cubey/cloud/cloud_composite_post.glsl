#ifndef CUBEY_CLOUD_COMPOSITE_POST_GLSL
#define CUBEY_CLOUD_COMPOSITE_POST_GLSL

vec3 cloud_composite_final_post(vec3 color, vec3 direction, float cloud_alpha, float edge_mask,
                                float surface_lit_dim, float surface_day_saturation,
                                float surface_day_contrast) {
    vec3 sun_dir = normalize(params.sun_direction_intensity.xyz);
    vec3 up = cloud_planet_up();
    float sun_elevation = dot(sun_dir, up);
    float day = smoothstep(-0.05, 0.16, sun_elevation);
    float twilight =
        smoothstep(-0.24, 0.02, sun_elevation) *
        (1.0 - smoothstep(0.04, 0.30, sun_elevation));
    float lit_post = max(day, twilight * 0.42);
    float twilight_saturation =
        max(lit_post, twilight * clamp(params.twilight_options.z, 0.0, 2.0));
    float sun_alignment = max(dot(direction, sun_dir), 0.0);
    float horizon = pow(max(1.0 - abs(direction.y), 0.0), 3.0);
    float sun_post_intensity = params.sun_direction_intensity.w * lit_post;
    float halo = pow(sun_alignment, 38.0) * sun_post_intensity;
    float tight_glare = pow(sun_alignment, 420.0) * sun_post_intensity;
    float glare_strength = clamp(params.lighting_strengths.w, 0.0, 3.0);
    float horizon_strength = clamp(params.composite_options.w, 0.0, 3.0);
    float contrast = max(params.composite_options.y, 0.0);
    float saturation = max(params.composite_options.z, 0.0);
    float surface_view = cloud_surface_view_factor();
    float surface_haze = mix(1.0, cloud_weather_haze_factor(), surface_view);
    float low_sun = 1.0 - smoothstep(0.05, 0.36, max(sun_elevation, 0.0));
    float cloud_edge = clamp(edge_mask, 0.0, 1.0);
    float warm_body_tint =
        cloud_alpha * low_sun * twilight * clamp(params.twilight_options.x, 0.0, 2.0) * 0.46;
    color *= mix(1.0, 0.68, clamp(warm_body_tint, 0.0, 1.0));
    color = cloud_chroma_tint(color, vec3(1.0, 0.46, 0.16), warm_body_tint);

    color += vec3(1.0, 0.58, 0.22) * halo * (0.10 + 0.16 * cloud_alpha) *
             glare_strength * mix(1.0, 0.42, surface_view);
    color += vec3(1.0, 0.58, 0.24) * halo * cloud_alpha * cloud_edge *
             low_sun * clamp(params.twilight_options.y, 0.0, 2.0) *
             glare_strength * mix(0.18, 0.52, surface_view);
    color += vec3(1.0, 0.82, 0.50) * tight_glare * 1.25 * glare_strength *
             mix(1.0, 0.34, surface_view);
    color += vec3(0.10, 0.12, 0.13) * horizon * (1.0 - cloud_alpha) * 0.22 *
             horizon_strength * mix(0.18, 1.0, lit_post) *
             mix(1.0, 0.54, surface_view) * surface_haze;
    color *= mix(1.0, mix(0.92, surface_lit_dim, surface_view), lit_post);

    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float regime_saturation =
        saturation * mix(0.34, mix(surface_day_saturation, 1.06, surface_view),
                         twilight_saturation);
    color = mix(vec3(luma), color, regime_saturation);
    float black_point = mix(0.0035, mix(0.018, 0.035, surface_view), lit_post);
    float regime_contrast =
        contrast * mix(0.58, mix(surface_day_contrast, 1.06, surface_view), lit_post);
    color = max((color - vec3(black_point)) * regime_contrast, vec3(0.0));
    color = pow(max(color, vec3(0.0)), vec3(mix(0.92, 1.02, lit_post)));
    return color;
}

vec3 cloud_composite_standalone_final_post(vec3 color, vec3 direction, float cloud_alpha,
                                           float edge_mask) {
    return cloud_composite_final_post(color, direction, cloud_alpha, edge_mask, 0.74, 0.72, 0.82);
}

vec3 cloud_composite_external_final_post(vec3 color, vec3 direction, float cloud_alpha,
                                         float edge_mask) {
    return cloud_composite_final_post(color, direction, cloud_alpha, edge_mask, 0.82, 1.0, 1.0);
}

#endif
