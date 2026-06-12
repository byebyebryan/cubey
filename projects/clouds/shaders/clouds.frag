#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/atmosphere.glsl"

const int CLOUDS_MAX_VIEW_STEPS = 64;
const int CLOUDS_MAX_LIGHT_STEPS = 8;

const int CLOUDS_VIEW_FINAL = 0;
const int CLOUDS_VIEW_WEATHER = 1;
const int CLOUDS_VIEW_DENSITY = 2;
const int CLOUDS_VIEW_TRANSMITTANCE = 3;
const int CLOUDS_VIEW_LIGHTING = 4;
const int CLOUDS_VIEW_SHADOW = 5;
const int CLOUDS_VIEW_STEPS = 6;
const int CLOUDS_VIEW_BACKGROUND = 7;
const int CLOUDS_VIEW_ATMOSPHERE = 8;
const int CLOUDS_VIEW_GROUND = 9;
const int CLOUDS_VIEW_GROUND_HIT = 10;
const int CLOUDS_VIEW_CLOUD_ALPHA = 11;
const int CLOUDS_VIEW_SHELL = 12;
const int CLOUDS_VIEW_SURFACE_SHADOW = 13;

layout(push_constant) uniform CloudsParams {
    vec4 camera_right_aspect;
    vec4 camera_up_tan_half_fovy;
    vec4 camera_forward_mode;
    vec4 camera_position_radius;
    vec4 cloud_shell;
    vec4 weather;
    vec4 sun_direction_intensity;
    vec4 render_options;
} params;

layout(location = 0) in vec2 frag_position;
layout(location = 0) out vec4 out_color;

#include "clouds_model.glsl"

float cloud_solar_margin(vec3 position_km, vec3 sun_dir) {
    vec3 center = planet_center();
    float ground_radius = params.camera_position_radius.w;
    vec3 to_sample = position_km - center;
    float sample_radius = max(length(to_sample), ground_radius + 0.001);
    vec3 local_up = to_sample / sample_radius;
    float horizon_dip = sqrt(max(1.0 - (ground_radius * ground_radius) /
                                           (sample_radius * sample_radius),
                                 0.0));
    float horizon_cos = -horizon_dip;
    return dot(local_up, sun_dir) - horizon_cos;
}

float cloud_sun_visibility(vec3 position_km, vec3 sun_dir) {
    float margin = cloud_solar_margin(position_km, sun_dir);
    return smoothstep(-0.10, 0.14, margin);
}

float cloud_twilight_visibility(vec3 position_km, vec3 sun_dir) {
    float margin = cloud_solar_margin(position_km, sun_dir);
    return smoothstep(-0.38, 0.18, margin);
}

float light_transmittance(vec3 position_km, vec3 sun_dir, int light_steps) {
    vec3 center = planet_center();
    float top_radius = params.camera_position_radius.w + params.cloud_shell.y;
    vec2 hit = ray_sphere(position_km, sun_dir, center, top_radius);
    float ray_end = max(hit.y, 0.0);
    if (ray_end <= 0.0) {
        return 1.0;
    }
    float layer_thickness = max(params.cloud_shell.y - params.cloud_shell.x, 0.001);
    // Near the terminator, full shell-length light rays turn cheap self-shadowing
    // into a slab artifact. Keep this local until clouds get a proper shadow map.
    float max_light_distance = max(layer_thickness * 10.0, 24.0);
    ray_end = min(ray_end, max_light_distance);

    float step_len = ray_end / float(max(light_steps, 1));
    float optical_depth = 0.0;
    for (int i = 0; i < CLOUDS_MAX_LIGHT_STEPS; ++i) {
        if (i >= light_steps) {
            break;
        }
        vec3 p = position_km + sun_dir * ((float(i) + 0.5) * step_len);
        optical_depth += cloud_density(p) * step_len;
    }
    return exp(-optical_depth * 0.50);
}

struct CloudSample {
    vec3 color;
    float transmittance;
    float mean_density;
    float mean_weather;
    float mean_light;
    float mean_shadow;
    float step_fraction;
    float shell_hit;
    float hit_ground;
    float shell_span;
};

CloudSample march_clouds(vec3 origin, vec3 direction, int view_steps, int light_steps) {
    CloudSample result;
    result.color = vec3(0.0);
    result.transmittance = 1.0;
    result.mean_density = 0.0;
    result.mean_weather = 0.0;
    result.mean_light = 0.0;
    result.mean_shadow = 0.0;
    result.step_fraction = 0.0;
    result.shell_hit = 0.0;
    result.hit_ground = 0.0;
    result.shell_span = 0.0;

    vec3 center = planet_center();
    float top_radius = params.camera_position_radius.w + params.cloud_shell.y;
    vec2 top_hit = ray_sphere(origin, direction, center, top_radius);
    if (top_hit.y <= 0.0) {
        return result;
    }
    float ray_start = max(top_hit.x, 0.0);
    float ray_end = top_hit.y;
    float bottom_radius = params.camera_position_radius.w + params.cloud_shell.x;
    float camera_radius = length(origin - center);
    if (camera_radius < bottom_radius) {
        vec2 bottom_hit = ray_sphere(origin, direction, center, bottom_radius);
        if (bottom_hit.y > 0.0) {
            ray_start = max(ray_start, bottom_hit.y);
        }
    }
    vec2 ground_hit = ray_sphere(origin, direction, center, params.camera_position_radius.w);
    bool hit_ground = ground_hit.x > 0.0;
    if (hit_ground) {
        ray_end = min(ray_end, ground_hit.x);
        result.hit_ground = 1.0;
    }
    if (ray_end <= ray_start) {
        return result;
    }
    result.shell_hit = 1.0;
    float layer_thickness = max(params.cloud_shell.y - params.cloud_shell.x, 0.001);
    float shell_span_ratio = (ray_end - ray_start) / layer_thickness;
    result.shell_span =
        clamp(shell_span_ratio * 0.18, 0.0, 1.0);
    float high_view = smoothstep(0.5, 1.1, params.camera_forward_mode.w);
    float view_horizon = dot(direction, normalize(origin - center));
    float tangent_fade = 1.0 - smoothstep(-0.34, -0.035, view_horizon);
    float high_horizon_fade = mix(1.0, tangent_fade, high_view);
    float sky_limb_fade = 1.0;
    if (!hit_ground) {
        sky_limb_fade = mix(1.0, smoothstep(0.55, 2.70, shell_span_ratio), high_view);
    }

    vec3 sun_dir = normalize(params.sun_direction_intensity.xyz);
    float step_len = (ray_end - ray_start) / float(max(view_steps, 1));
    float edge_fade_distance = max(layer_thickness * 0.55, step_len * 2.0);
    int used_steps = 0;
    float density_sum = 0.0;
    float weather_sum = 0.0;
    float light_sum = 0.0;
    float shadow_sum = 0.0;
    for (int i = 0; i < CLOUDS_MAX_VIEW_STEPS; ++i) {
        if (i >= view_steps) {
            break;
        }
        float jitter = hash31(vec3(frag_position, float(i))) - 0.5;
        float sample_t = ray_start + (float(i) + 0.5 + jitter * 0.18) * step_len;
        vec3 p = origin + direction * sample_t;
        float density = cloud_density(p);
        density *= high_horizon_fade;
        density *= sky_limb_fade;
        float edge_distance = min(sample_t - ray_start, ray_end - sample_t);
        density *= smoothstep(0.0, edge_fade_distance, edge_distance);
        if (params.camera_forward_mode.w > 1.5 && !hit_ground) {
            density *= 0.22;
        }
        float weather = weather_coverage(p);
        ++used_steps;
        density_sum += density;
        weather_sum += weather;
        if (density <= 0.0001) {
            continue;
        }

        float sun_visibility = cloud_sun_visibility(p, sun_dir);
        float self_shadow = light_transmittance(p, sun_dir, light_steps);
        float shadow = self_shadow * sun_visibility;
        float powder = 1.0 - exp(-density * step_len * 0.75);
        float view_sun = max(dot(direction, sun_dir), 0.0);
        float silver_edge = pow(view_sun, cloud_style_value(5.0, 4.5, 3.6, 5.5, 7.5));
        float phase_base = cloud_style_value(0.42, 0.44, 0.50, 0.36, 0.60);
        float phase_peak = cloud_style_value(1.75, 1.85, 1.45, 2.10, 1.35);
        float phase = mix(phase_base, phase_peak, silver_edge);
        float interior =
            mix(cloud_style_value(0.62, 0.58, 0.72, 0.46, 0.80), 1.0, self_shadow);
        float sun_light =
            self_shadow * interior * phase *
            (cloud_style_value(0.62, 0.65, 0.52, 0.58, 0.46) +
             powder * cloud_style_value(1.18, 1.20, 0.82, 1.35, 0.55)) *
            params.sun_direction_intensity.w * sun_visibility;
        vec3 local_up = normalize(p - center);
        float altitude = length(p - center) - params.camera_position_radius.w;
        float height01 = clamp((altitude - params.cloud_shell.x) /
                                   max(params.cloud_shell.y - params.cloud_shell.x, 0.001),
                               0.0, 1.0);
        float ambient = cloud_style_value(0.23, 0.22, 0.30, 0.16, 0.34) +
                        cloud_style_value(0.30, 0.31, 0.24, 0.22, 0.26) *
                            smoothstep(-0.2, 0.8, dot(local_up, vec3(0.0, 1.0, 0.0)));
        float twilight_visibility = cloud_twilight_visibility(p, sun_dir);
        float ambient_energy = smoothstep(0.01, 0.55,
                                          params.sun_direction_intensity.w * twilight_visibility);
        float ambient_shadow = mix(1.0, self_shadow, ambient_energy * 0.35);
        ambient *= mix(cloud_style_value(0.82, 0.78, 0.92, 0.58, 0.96), 1.0,
                       ambient_shadow);
        ambient *= mix(cloud_style_value(0.018, 0.018, 0.024, 0.012, 0.030), 1.0,
                       ambient_energy);
        float top_lift = smoothstep(0.20, 0.85, height01);
        vec3 direct_tint = mix(vec3(1.12, 0.96, 0.78), vec3(1.06, 1.04, 0.98),
                               params.sun_direction_intensity.w);
        vec3 ambient_tint = cloud_style_value(0.54, 0.56, 0.58, 0.46, 0.62) *
                            mix(vec3(0.44, 0.50, 0.59), vec3(0.64, 0.69, 0.76), top_lift);
        vec3 cloud_light =
            direct_tint * sun_light * (1.08 + silver_edge * 0.34) + ambient_tint * ambient;
        float alpha =
            1.0 - exp(-density * step_len * cloud_style_value(0.88, 0.95, 0.72, 1.08, 0.54));
        result.color += result.transmittance * alpha * cloud_light;
        result.transmittance *= 1.0 - alpha;
        light_sum += sun_light + ambient;
        shadow_sum += shadow;
        if (result.transmittance < 0.015) {
            break;
        }
    }

    float denom = max(float(used_steps), 1.0);
    result.mean_density = density_sum / denom;
    result.mean_weather = weather_sum / denom;
    result.mean_light = light_sum / denom;
    result.mean_shadow = shadow_sum / denom;
    result.step_fraction = float(used_steps) / float(max(view_steps, 1));
    return result;
}

void main() {
    vec3 origin = params.camera_position_radius.xyz;
    vec3 direction = view_direction();
    int debug_view = int(params.render_options.x + 0.5);
    int view_steps = clamp(int(params.render_options.y + 0.5), 1, CLOUDS_MAX_VIEW_STEPS);
    int light_steps = clamp(int(params.render_options.z + 0.5), 1, CLOUDS_MAX_LIGHT_STEPS);

    CloudSample clouds = march_clouds(origin, direction, view_steps, light_steps);
    vec3 output_color = clouds.color;
    float output_alpha = clouds.transmittance;

    if (debug_view == CLOUDS_VIEW_WEATHER) {
        output_color = vec3(clouds.mean_weather);
        output_alpha = 1.0;
    } else if (debug_view == CLOUDS_VIEW_DENSITY) {
        output_color = vec3(clouds.mean_density * 1.6);
        output_alpha = 1.0;
    } else if (debug_view == CLOUDS_VIEW_TRANSMITTANCE) {
        output_color = vec3(clouds.transmittance);
        output_alpha = 1.0;
    } else if (debug_view == CLOUDS_VIEW_LIGHTING) {
        output_color = vec3(clouds.mean_light);
        output_alpha = 1.0;
    } else if (debug_view == CLOUDS_VIEW_SHADOW) {
        output_color = vec3(clouds.mean_shadow);
        output_alpha = 1.0;
    } else if (debug_view == CLOUDS_VIEW_STEPS) {
        output_color = vec3(clouds.step_fraction, 1.0 - clouds.step_fraction, 0.15);
        output_alpha = 1.0;
    } else if (debug_view == CLOUDS_VIEW_CLOUD_ALPHA) {
        output_color = vec3(1.0 - clouds.transmittance);
        output_alpha = 1.0;
    } else if (debug_view == CLOUDS_VIEW_SHELL) {
        output_color = vec3(clouds.shell_hit, clouds.hit_ground, clouds.shell_span);
        output_alpha = 1.0;
    }

    out_color = vec4(output_color, output_alpha);
}
