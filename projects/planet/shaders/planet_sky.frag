#version 450

layout(location = 0) in vec2 frag_ndc;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform PlanetSkyFrame {
    vec4 camera_right_aspect;
    vec4 camera_up_tan_half_fovy;
    vec4 camera_forward_enabled;
    vec4 sun_direction_radius;
    vec4 sun_color_intensity;
    vec4 sun_disk_glow;
    vec4 camera_position_radius;
    vec4 background_space_limb;
} celestial;

float sphere_occlusion(vec3 ray_direction, float radius) {
    vec3 camera_position = celestial.camera_position_radius.xyz;
    float closest_t = -dot(camera_position, ray_direction);
    if (closest_t <= 0.0) {
        return 0.0;
    }
    vec3 closest = camera_position + ray_direction * closest_t;
    float distance_to_center = length(closest);
    float edge_width = max(fwidth(distance_to_center) * 2.0, radius * 0.0008);
    return 1.0 - smoothstep(radius - edge_width, radius + edge_width,
                            distance_to_center);
}

float planet_occlusion(vec3 ray_direction) {
    return sphere_occlusion(ray_direction, celestial.camera_position_radius.w);
}

float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float star_field(vec3 ray_direction) {
    vec3 cell = floor(ray_direction * 900.0);
    float star = smoothstep(0.9978, 1.0, hash13(cell));
    float sparkle = 0.45 + 0.55 * hash13(cell + vec3(17.0, 37.0, 71.0));
    return star * sparkle;
}

vec3 atmosphere_scatter_color(float sun_elevation, float toward_sun, float horizon) {
    vec3 day_haze = mix(vec3(0.055, 0.105, 0.205), vec3(0.20, 0.36, 0.68),
                        clamp(sun_elevation * 1.2 + 0.35, 0.0, 1.0));
    vec3 twilight = vec3(1.00, 0.42, 0.15);
    float twilight_window =
        (1.0 - smoothstep(0.10, 0.52, abs(sun_elevation))) *
        smoothstep(-0.28, 0.06, sun_elevation);
    float warm = twilight_window * horizon * (0.32 + 0.68 * toward_sun);
    return mix(day_haze, twilight, clamp(warm, 0.0, 1.0));
}

vec3 atmosphere_view_haze(vec3 ray_direction, vec3 sun_direction) {
    vec3 camera_position = celestial.camera_position_radius.xyz;
    float planet_radius = celestial.camera_position_radius.w;
    float atmosphere_radius = max(celestial.background_space_limb.w, planet_radius * 1.001);
    float atmosphere_height = max(atmosphere_radius - planet_radius, planet_radius * 0.001);
    float camera_radius = length(camera_position);
    float camera_altitude = max(camera_radius - planet_radius, 0.0);
    float camera_altitude01 = camera_altitude / atmosphere_height;
    float inside_atmosphere = 1.0 - smoothstep(0.85, 1.25, camera_altitude01);
    if (inside_atmosphere <= 0.001) {
        return vec3(0.0);
    }

    vec3 camera_up = normalize(camera_position);
    float ray_up = dot(ray_direction, camera_up);
    float above_ground = smoothstep(0.0, 0.06, ray_up);
    float horizon = exp(-max(ray_up, 0.0) / 0.18) * above_ground;
    float upper_sky = smoothstep(0.02, 0.65, ray_up) * above_ground;
    float optical_depth = clamp(horizon * 0.82 + upper_sky * 0.30, 0.0, 1.0);

    float sun_elevation = dot(sun_direction, camera_up);
    vec3 sun_tangent = sun_direction - camera_up * sun_elevation;
    float sun_tangent_len = length(sun_tangent);
    float toward_sun = 0.0;
    if (sun_tangent_len > 0.0001) {
        toward_sun = pow(max(dot(ray_direction, sun_tangent / sun_tangent_len), 0.0), 2.0);
    }
    vec3 scatter = atmosphere_scatter_color(sun_elevation, toward_sun, horizon);
    return scatter * optical_depth * inside_atmosphere * 0.72;
}

vec3 local_atmosphere_background(vec3 ray_direction, vec3 sun_direction) {
    vec3 camera_position = celestial.camera_position_radius.xyz;
    vec3 camera_up = normalize(camera_position);
    float ray_up = dot(ray_direction, camera_up);
    float sun_elevation = dot(sun_direction, camera_up);
    vec3 sun_tangent = sun_direction - camera_up * sun_elevation;
    float toward_sun = 0.0;
    if (length(sun_tangent) > 0.0001) {
        vec3 view_tangent = ray_direction - camera_up * ray_up;
        if (length(view_tangent) > 0.0001) {
            toward_sun = pow(max(dot(normalize(view_tangent), normalize(sun_tangent)), 0.0), 2.0);
        }
    }

    float above_horizon = smoothstep(-0.035, 0.075, ray_up);
    float upper_sky = smoothstep(0.02, 0.68, ray_up);
    float horizon = exp(-abs(ray_up) / 0.13) * above_horizon;
    vec3 upper_color = mix(vec3(0.016, 0.023, 0.060), vec3(0.17, 0.25, 0.48), upper_sky);
    vec3 scatter = atmosphere_scatter_color(sun_elevation, toward_sun, horizon);
    float scatter_weight = clamp(horizon * 0.62 + upper_sky * 0.22, 0.0, 0.78);
    vec3 color = mix(upper_color, scatter, scatter_weight);

    float horizon_extinction = smoothstep(0.0, 0.36, horizon);
    vec3 stars =
        vec3(0.75, 0.82, 1.0) * star_field(ray_direction) * 0.30 * above_horizon *
        (1.0 - horizon_extinction);
    vec3 below_horizon = vec3(0.006, 0.008, 0.018);
    color = mix(below_horizon, color + stars, above_horizon);
    return color;
}

vec3 space_background(vec3 ray_direction, vec3 sun_direction) {
    vec3 base = vec3(0.0015, 0.0022, 0.0060);
    vec3 camera_position = celestial.camera_position_radius.xyz;
    float planet_radius = celestial.camera_position_radius.w;
    float atmosphere_radius = max(celestial.background_space_limb.w, planet_radius * 1.001);
    float atmosphere_height = max(atmosphere_radius - planet_radius, planet_radius * 0.001);
    float camera_altitude = max(length(camera_position) - planet_radius, 0.0);
    float local_sky = 1.0 - smoothstep(atmosphere_height * 0.70,
                                       atmosphere_height * 1.12, camera_altitude);
    if (local_sky >= 0.999) {
        return local_atmosphere_background(ray_direction, sun_direction);
    }
    float solid_planet = sphere_occlusion(ray_direction, planet_radius * 0.998);
    float sky_visibility = 1.0 - solid_planet;
    float closest_t = -dot(camera_position, ray_direction);
    float limb = 0.0;
    float lit_limb = 0.0;
    if (closest_t > 0.0) {
        vec3 closest = camera_position + ray_direction * closest_t;
        float distance_to_center = length(closest);
        float shell = 1.0 - smoothstep(planet_radius, atmosphere_radius, distance_to_center);
        shell *= 1.0 - sphere_occlusion(ray_direction, planet_radius * 0.985);
        vec3 limb_normal = normalize(closest);
        float limb_sun_dot = dot(limb_normal, sun_direction);
        lit_limb = smoothstep(-0.18, 0.58, limb_sun_dot);
        float terminator = exp(-abs(limb_sun_dot) / 0.18);
        limb = shell * (0.12 + 0.82 * lit_limb + 0.22 * terminator);
    }
    vec3 stars = vec3(0.75, 0.82, 1.0) * star_field(ray_direction) * 0.32 * sky_visibility;
    vec3 limb_color = mix(vec3(0.012, 0.036, 0.095), vec3(0.13, 0.30, 0.58), lit_limb);
    vec3 sky = base * sky_visibility + stars +
               atmosphere_view_haze(ray_direction, sun_direction) * sky_visibility;
    vec3 space_sky = sky + limb_color * limb * 0.55;
    if (local_sky <= 0.001) {
        return space_sky;
    }
    return mix(space_sky, local_atmosphere_background(ray_direction, sun_direction), local_sky);
}

void main() {
    float tan_half_fovy = celestial.camera_up_tan_half_fovy.w;
    vec3 ray_direction = normalize(
        celestial.camera_forward_enabled.xyz +
        celestial.camera_right_aspect.xyz * frag_ndc.x * celestial.camera_right_aspect.w *
            tan_half_fovy -
        celestial.camera_up_tan_half_fovy.xyz * frag_ndc.y * tan_half_fovy);
    vec3 sun_direction = normalize(celestial.sun_direction_radius.xyz);
    vec3 color = space_background(ray_direction, sun_direction);

    if (celestial.camera_forward_enabled.w < 0.5 || celestial.sun_color_intensity.w <= 0.0) {
        out_color = vec4(color, 1.0);
        return;
    }

    float sun_angle = acos(clamp(dot(ray_direction, sun_direction), -1.0, 1.0));
    float sun_radius = max(celestial.sun_direction_radius.w, 0.0001);
    float edge_width = max(fwidth(sun_angle) * 1.5, sun_radius * 0.10);
    float disk = 1.0 - smoothstep(sun_radius - edge_width, sun_radius + edge_width, sun_angle);
    float near_halo = exp(-sun_angle / max(sun_radius * 7.0, 0.0001));
    float far_halo = exp(-sun_angle / max(sun_radius * 28.0, 0.0001));
    float radiance = disk * celestial.sun_disk_glow.x +
                     near_halo * celestial.sun_disk_glow.y * celestial.sun_disk_glow.z +
                     far_halo * celestial.sun_disk_glow.y * celestial.sun_disk_glow.w;
    radiance *= 1.0 - planet_occlusion(ray_direction);
    color += celestial.sun_color_intensity.rgb * celestial.sun_color_intensity.w * radiance;

    out_color = vec4(color, 1.0);
}
