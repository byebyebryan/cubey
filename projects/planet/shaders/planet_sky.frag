#version 450
#extension GL_GOOGLE_include_directive : require

#include "planet_atmosphere.glsl"

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
    vec4 atmosphere_mode_options;
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

    PlanetAtmosphereTerms terms = planet_atmosphere_terms(ray_direction, camera_up, sun_direction);
    vec3 scatter = planet_atmosphere_scatter_color(terms.sun_elevation, terms.toward_sun, horizon);
    return scatter * optical_depth * inside_atmosphere * 0.72;
}

vec3 local_atmosphere_background(vec3 ray_direction, vec3 sun_direction) {
    vec3 camera_position = celestial.camera_position_radius.xyz;
    vec3 camera_up = normalize(camera_position);
    PlanetAtmosphereTerms terms = planet_atmosphere_terms(ray_direction, camera_up, sun_direction);

    vec3 night_upper = mix(vec3(0.004, 0.006, 0.018), vec3(0.010, 0.016, 0.036),
                           clamp(terms.horizon_shell * 0.55 + terms.upper_sky * 0.25, 0.0, 1.0));
    vec3 day_upper = mix(vec3(0.030, 0.060, 0.130), vec3(0.17, 0.25, 0.48), terms.upper_sky);
    vec3 upper_color = mix(night_upper, day_upper, terms.daylight);
    vec3 scatter = planet_atmosphere_scatter_color(terms);
    float scatter_weight =
        clamp(terms.horizon * 0.62 + terms.upper_sky * 0.22, 0.0, 0.78) *
        terms.atmosphere_visibility;
    vec3 color = mix(upper_color, scatter, scatter_weight);

    float horizon_extinction = smoothstep(0.0, 0.36, terms.horizon);
    vec3 stars =
        vec3(0.75, 0.82, 1.0) * star_field(ray_direction) * 0.30 * terms.above_horizon *
        (1.0 - horizon_extinction) * terms.star_visibility;
    float below_horizon_haze = smoothstep(-0.75, -0.04, terms.ray_up) * terms.daylight;
    vec3 night_below_horizon = vec3(0.006, 0.008, 0.018);
    vec3 night_horizon_fill = mix(night_below_horizon, vec3(0.010, 0.016, 0.036),
                                  smoothstep(-0.70, -0.02, terms.ray_up));
    vec3 day_horizon_fill =
        planet_atmosphere_scatter_color(
            terms.sun_elevation, terms.toward_sun, terms.horizon_shell) *
        (0.68 + 0.22 * terms.toward_sun);
    vec3 below_horizon =
        mix(night_horizon_fill, day_horizon_fill, clamp(below_horizon_haze, 0.0, 1.0));
    color = mix(below_horizon, color + stars, terms.above_horizon);
    return color;
}

vec3 physical_atmosphere_background(vec3 ray_direction, vec3 sun_direction) {
    vec3 camera_position = celestial.camera_position_radius.xyz;
    vec3 camera_up = normalize(camera_position);
    float planet_radius = celestial.camera_position_radius.w;
    float atmosphere_radius = max(celestial.background_space_limb.w, planet_radius * 1.001);
    float atmosphere_height = max(atmosphere_radius - planet_radius, planet_radius * 0.001);
    float camera_altitude = max(length(camera_position) - planet_radius, 0.0);
    float inside_atmosphere =
        1.0 - smoothstep(0.85, 1.25, camera_altitude / atmosphere_height);

    float ray_up = dot(ray_direction, camera_up);
    float sun_elevation = dot(sun_direction, camera_up);
    float above_horizon = smoothstep(-0.10, 0.035, ray_up);
    float daylight = smoothstep(-0.08, 0.24, sun_elevation);
    float twilight = exp(-abs(sun_elevation) / 0.16) * smoothstep(-0.22, 0.08, sun_elevation);
    float horizon_shell = exp(-abs(ray_up) / 0.14);

    PlanetAtmosphereScatterSample scatter = planet_atmosphere_integrate_ray(
        camera_position, ray_direction, -1.0, planet_radius, atmosphere_radius, sun_direction,
        celestial.sun_color_intensity.rgb, celestial.sun_color_intensity.w);
    vec3 night = vec3(0.003, 0.005, 0.016);
    vec3 twilight_warm = vec3(0.92, 0.36, 0.15) * horizon_shell * twilight * 0.08;
    vec3 stars = vec3(0.75, 0.82, 1.0) * star_field(ray_direction) * 0.28 *
                 (1.0 - max(daylight, twilight)) * above_horizon;
    vec3 sky = night + scatter.radiance + twilight_warm + stars;
    vec3 below = mix(vec3(0.006, 0.008, 0.018),
                     scatter.radiance * 0.35 + vec3(0.035, 0.050, 0.075) * daylight,
                     max(daylight, twilight * 0.65));
    sky = mix(below, sky, above_horizon);
    return mix(local_atmosphere_background(ray_direction, sun_direction), sky,
               clamp(inside_atmosphere, 0.0, 1.0));
}

bool physical_enabled() {
    return celestial.atmosphere_mode_options.x > 0.5;
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
        return physical_enabled()
                   ? physical_atmosphere_background(ray_direction, sun_direction)
                   : local_atmosphere_background(ray_direction, sun_direction);
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
    vec3 local_sky_color = physical_enabled()
                               ? physical_atmosphere_background(ray_direction, sun_direction)
                               : local_atmosphere_background(ray_direction, sun_direction);
    return mix(space_sky, local_sky_color, local_sky);
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
