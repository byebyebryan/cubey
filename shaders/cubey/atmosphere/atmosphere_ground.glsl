#ifndef CUBEY_ATMOSPHERE_GROUND_GLSL
#define CUBEY_ATMOSPHERE_GROUND_GLSL

float reference_line(vec2 position, float spacing) {
    vec2 uv = position / max(spacing, 0.0001);
    vec2 derivative = max(fwidth(uv), vec2(0.0001));
    vec2 grid = abs(fract(uv - 0.5) - 0.5) / derivative;
    return 1.0 - clamp(min(grid.x, grid.y), 0.0, 1.0);
}

float reference_axis(float coordinate) {
    float width = max(fwidth(coordinate) * 1.75, 0.012);
    return 1.0 - smoothstep(width * 0.35, width, abs(coordinate));
}

float reference_ring(vec2 position, float radius) {
    float distance_to_ring = abs(length(position) - radius);
    float width = max(fwidth(length(position)) * 1.5, 0.020);
    return 1.0 - smoothstep(width * 0.25, width, distance_to_ring);
}

vec4 ground_reference_geometry(vec3 ground_position, vec3 ray_origin, vec3 planet_center) {
    if (atmosphere.atmosphere_options.y < 0.5) {
        return vec4(0.0);
    }

    vec2 ground_xz = ground_position.xz;
    float grid_scale = max(atmosphere.atmosphere_options.z, 0.001);
    float intensity = atmosphere.atmosphere_options.w;
    float ground_distance = length(ground_xz);
    float altitude = atmosphere.radii_ground.z;
    float range = mix(80.0, 650.0, smoothstep(0.5, 25.0, altitude));
    float distance_fade = 1.0 - smoothstep(range * 0.62, range, ground_distance);
    float horizon_fade = smoothstep(
        0.0, 0.08, dot(normalize(ground_position - planet_center),
                       atmosphere_camera_up(ray_origin, planet_center)));

    float minor_grid = reference_line(ground_xz, grid_scale);
    float major_grid = reference_line(ground_xz, grid_scale * 10.0);
    float x_axis = reference_axis(ground_xz.y);
    float z_axis = reference_axis(ground_xz.x);
    float origin = 1.0 - smoothstep(grid_scale * 0.16, grid_scale * 0.24, ground_distance);
    float five_ring = reference_ring(ground_xz, grid_scale * 5.0);
    float ten_ring = reference_ring(ground_xz, grid_scale * 10.0);

    vec3 minor_color = cubey_srgb_to_linear(vec3(0.35, 0.40, 0.42));
    vec3 major_color = cubey_srgb_to_linear(vec3(0.62, 0.67, 0.66));
    vec3 ring_color = cubey_srgb_to_linear(vec3(0.82, 0.78, 0.54));
    vec3 x_axis_color = cubey_srgb_to_linear(vec3(0.95, 0.20, 0.14));
    vec3 z_axis_color = cubey_srgb_to_linear(vec3(0.10, 0.78, 0.92));
    vec3 origin_color = cubey_srgb_to_linear(vec3(1.0, 0.90, 0.20));

    vec3 color = minor_color;
    float alpha = minor_grid * 0.18;
    color = mix(color, major_color, major_grid);
    alpha = max(alpha, major_grid * 0.36);
    color = mix(color, ring_color, max(five_ring * 0.45, ten_ring));
    alpha = max(alpha, max(five_ring * 0.20, ten_ring * 0.32));
    color = mix(color, x_axis_color, x_axis);
    alpha = max(alpha, x_axis * 0.68);
    color = mix(color, z_axis_color, z_axis);
    alpha = max(alpha, z_axis * 0.68);
    color = mix(color, origin_color, origin);
    alpha = max(alpha, origin * 0.85);

    alpha *= distance_fade * horizon_fade * intensity;
    return vec4(color, clamp(alpha, 0.0, 1.0));
}

vec3 ground_radiance(vec3 ray_origin, vec3 ray_direction, vec3 planet_center, float ground_t) {
    vec3 sun_direction = normalize(atmosphere.sun_direction_radius.xyz);
    vec3 ground_position = ray_origin + ray_direction * ground_t;
    vec3 normal = normalize(ground_position - planet_center);
    float ndotl = dot(normal, sun_direction);
    float sun_visibility = ground_sun_visibility(normal, sun_direction);
    float direct_light = max(ndotl, 0.0) * sun_visibility;
    float twilight_fill = (1.0 - sun_visibility) * smoothstep(-0.16, 0.02, ndotl) * 0.16;
    float sun_elevation = sun_elevation_degrees(sun_direction);
    float twilight_ground = twilight_visibility(sun_elevation) * atmosphere.night_options.x;
    float night_ground = star_visibility(sun_elevation) * atmosphere.night_options.z;
    float night_ambient = twilight_ground * 0.060 + night_ground * 0.024;
    vec3 moon_direction = normalize(atmosphere.moon_direction_radius.xyz);
    float moon_visibility = ground_sun_visibility(normal, moon_direction);
    float moon_direct = max(dot(normal, moon_direction), 0.0) * moon_visibility *
                        atmosphere.moon_options.x * atmosphere.moon_options.z *
                        atmosphere.moon_options.w;
    vec3 moonlight = cubey_srgb_to_linear(vec3(0.46, 0.54, 0.78)) * moon_direct * 0.34;
    vec3 base = cubey_srgb_to_linear(vec3(0.18, 0.20, 0.16)) * atmosphere.radii_ground.w;
    vec3 lit_ground = base * (0.12 + direct_light * 1.35 + twilight_fill + night_ambient) +
                      moonlight * atmosphere.radii_ground.w;
    vec4 reference = ground_reference_geometry(ground_position, ray_origin, planet_center);
    return mix(lit_ground, reference.rgb, reference.a);
}

vec3 moon_ground_debug_radiance(vec3 ray_origin, vec3 ray_direction, vec3 planet_center,
                                float ground_t) {
    vec3 moon_direction = normalize(atmosphere.moon_direction_radius.xyz);
    vec3 ground_position = ray_origin + ray_direction * ground_t;
    vec3 normal = normalize(ground_position - planet_center);
    float moon_visibility = ground_sun_visibility(normal, moon_direction);
    float moon_direct = max(dot(normal, moon_direction), 0.0) * moon_visibility *
                        atmosphere.moon_options.x * atmosphere.moon_options.z *
                        atmosphere.moon_options.w;
    return cubey_srgb_to_linear(vec3(0.46, 0.54, 0.78)) * moon_direct * 0.34 *
           atmosphere.radii_ground.w;
}

#endif // CUBEY_ATMOSPHERE_GROUND_GLSL
