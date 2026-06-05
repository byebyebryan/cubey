#version 450

layout(location = 0) in vec2 frag_ndc;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform PlanetCelestialFrame {
    vec4 camera_right_aspect;
    vec4 camera_up_tan_half_fovy;
    vec4 camera_forward_enabled;
    vec4 sun_direction_radius;
    vec4 sun_color_intensity;
    vec4 sun_disk_glow;
    vec4 moon_direction_radius;
    vec4 moon_color_phase;
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

vec3 space_background(vec3 ray_direction, vec3 sun_direction) {
    vec3 base = vec3(0.0015, 0.0022, 0.0060);
    vec3 camera_position = celestial.camera_position_radius.xyz;
    float planet_radius = celestial.camera_position_radius.w;
    float atmosphere_radius = max(celestial.background_space_limb.w, planet_radius * 1.001);
    float closest_t = -dot(camera_position, ray_direction);
    float limb = 0.0;
    float lit_limb = 0.0;
    if (closest_t > 0.0) {
        vec3 closest = camera_position + ray_direction * closest_t;
        float distance_to_center = length(closest);
        float shell = 1.0 - smoothstep(planet_radius, atmosphere_radius, distance_to_center);
        shell *= 1.0 - sphere_occlusion(ray_direction, planet_radius * 0.985);
        vec3 limb_normal = normalize(closest);
        lit_limb = clamp(dot(limb_normal, sun_direction) * 0.5 + 0.5, 0.0, 1.0);
        limb = shell * (0.18 + 0.82 * lit_limb);
    }
    vec3 stars = vec3(0.75, 0.82, 1.0) * star_field(ray_direction) * 0.32;
    vec3 limb_color = mix(vec3(0.018, 0.045, 0.115), vec3(0.16, 0.33, 0.62), lit_limb);
    return base + stars + limb_color * limb * 0.55;
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

    vec3 moon_direction = normalize(celestial.moon_direction_radius.xyz);
    float moon_radius = celestial.moon_direction_radius.w;
    if (moon_radius > 0.0) {
        float moon_angle = acos(clamp(dot(ray_direction, moon_direction), -1.0, 1.0));
        float moon_edge = max(fwidth(moon_angle) * 1.5, moon_radius * 0.08);
        float moon_disk = 1.0 - smoothstep(moon_radius - moon_edge, moon_radius + moon_edge,
                                           moon_angle);
        float phase_light = clamp(1.0 - abs(celestial.moon_color_phase.w - 0.5) * 1.75,
                                  0.16, 1.0);
        float moon_lit = clamp(dot(moon_direction, sun_direction) * 0.5 + 0.5, 0.10, 1.0);
        float moon_occlusion = planet_occlusion(ray_direction);
        color += celestial.moon_color_phase.rgb * moon_disk * phase_light * moon_lit *
                 (1.0 - moon_occlusion) * 0.38;
    }

    out_color = vec4(color, 1.0);
}
