#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"

layout(push_constant) uniform RenderParams {
    vec4 camera_position_steps;
    vec4 ray_right_tan;
    vec4 ray_up_aspect;
    vec4 ray_forward_debug;
    vec4 render_options;
    vec4 obstacle_options;
    vec4 style_options;
    vec4 color_options;
} params;

layout(set = 0, binding = 0) uniform sampler3D density_volume;
layout(set = 0, binding = 1) uniform sampler3D velocity_volume;
layout(set = 0, binding = 2) uniform sampler3D shadow_volume;

layout(location = 0) in vec2 frag_position;
layout(location = 1) in vec2 frag_uv;
layout(location = 0) out vec4 out_color;

float safe_ray_component(float value) {
    if (abs(value) >= 0.00001) {
        return value;
    }
    return value < 0.0 ? -0.00001 : 0.00001;
}

bool ray_box_intersection(vec3 origin, vec3 direction, out float near_t, out float far_t) {
    vec3 safe_direction = vec3(safe_ray_component(direction.x), safe_ray_component(direction.y),
                               safe_ray_component(direction.z));
    vec3 inv_direction = 1.0 / safe_direction;
    vec3 t0 = (vec3(0.0) - origin) * inv_direction;
    vec3 t1 = (vec3(1.0) - origin) * inv_direction;
    vec3 t_min = min(t0, t1);
    vec3 t_max = max(t0, t1);
    near_t = max(max(t_min.x, t_min.y), t_min.z);
    far_t = min(min(t_max.x, t_max.y), t_max.z);
    return far_t > max(near_t, 0.0);
}

vec4 density_at(vec3 uv) {
    return texture(density_volume, clamp(uv, vec3(0.0), vec3(1.0)));
}

float obstacle_radius() {
    return max(params.obstacle_options.y, 0.0);
}

vec3 obstacle_center() {
    return vec3(0.5, params.obstacle_options.x, 0.5);
}

bool obstacle_enabled() {
    return obstacle_radius() > 0.0;
}

bool inside_obstacle(vec3 position) {
    return obstacle_enabled() && length(position - obstacle_center()) <= obstacle_radius();
}

bool ray_sphere_intersection(vec3 origin, vec3 direction, out float hit_t) {
    if (!obstacle_enabled()) {
        return false;
    }
    vec3 offset = origin - obstacle_center();
    float b = dot(offset, direction);
    float c = dot(offset, offset) - obstacle_radius() * obstacle_radius();
    float discriminant = b * b - c;
    if (discriminant < 0.0) {
        return false;
    }
    float root = sqrt(discriminant);
    float near_t = -b - root;
    float far_t = -b + root;
    hit_t = near_t > 0.0 ? near_t : far_t;
    return hit_t > 0.0;
}

float volume_edge_fade(vec3 position) {
    float edge_distance = min(min(position.x, 1.0 - position.x),
                              min(min(position.y, 1.0 - position.y),
                                  min(position.z, 1.0 - position.z)));
    return smoothstep(0.0, 0.045, edge_distance);
}

float raw_smoke_density(vec4 density) {
    return max(density.r, 0.0);
}

float smoke_density(vec4 density, vec3 position) {
    if (inside_obstacle(position)) {
        return 0.0;
    }
    float soot_density = raw_smoke_density(density);
    float low_density_mask = smoothstep(0.010, 0.050, soot_density);
    return soot_density * low_density_mask * volume_edge_fade(position);
}

float smoke_density_at(vec3 uv) {
    return smoke_density(density_at(uv), uv);
}

vec3 background_color(vec2 uv) {
    vec3 low = cubey_srgb_to_linear(vec3(0.050, 0.060, 0.075));
    vec3 high = cubey_srgb_to_linear(vec3(0.150, 0.185, 0.230));
    vec3 horizon = cubey_srgb_to_linear(vec3(0.095, 0.105, 0.110));
    vec3 color = mix(low, high, uv.y);
    float floor_band = 1.0 - smoothstep(0.00, 0.34, uv.y);
    color = mix(color, horizon, floor_band * 0.50);

    vec2 grid_uv = vec2(uv.x * 8.0, uv.y * 5.0);
    vec2 grid_cell = abs(fract(grid_uv) - 0.5);
    float grid = 1.0 - smoothstep(0.470, 0.500, max(grid_cell.x, grid_cell.y));
    color += cubey_srgb_to_linear(vec3(0.115, 0.135, 0.155)) * grid *
             params.color_options.w * (0.35 + floor_band * 0.65);

    return color * mix(0.70, 1.35, clamp(params.style_options.y, 0.0, 1.0));
}

float base_volume_extinction() {
    return params.render_options.w * 0.16;
}

vec3 velocity_debug_color(vec3 velocity) {
    float speed = clamp(length(velocity) * 2.2, 0.0, 1.0);
    vec3 direction = normalize(velocity + vec3(0.00001)) * 0.5 + 0.5;
    return mix(vec3(speed), direction, 0.75);
}

vec3 smoke_albedo(vec4 density) {
    float heat = clamp(density.g * 0.06, 0.0, 1.0);
    vec3 cool_soot = cubey_srgb_to_linear(vec3(0.50, 0.53, 0.56));
    vec3 warm_soot = cubey_srgb_to_linear(vec3(0.66, 0.57, 0.45));
    return mix(cool_soot, warm_soot, clamp(params.color_options.x + heat * 0.65, 0.0, 1.0));
}

float flame_transfer(float flame, float heat, float soot, vec3 position) {
    float flame_mask = smoothstep(0.055, 0.34, flame);
    float heat_mask = smoothstep(0.035, 0.70, heat);
    float soot_cutoff = 1.0 - smoothstep(0.36, 1.45, soot);
    float lower_volume = 1.0 - smoothstep(0.24, 0.82, position.y);
    return pow(flame_mask * heat_mask * soot_cutoff, 1.75) * mix(0.25, 1.0, lower_volume);
}

vec3 flame_color(float heat, float core) {
    vec3 ember = cubey_srgb_to_linear(vec3(1.0, 0.16, 0.025));
    vec3 orange = cubey_srgb_to_linear(vec3(1.0, 0.42, 0.08));
    vec3 yellow = cubey_srgb_to_linear(vec3(1.0, 0.78, 0.28));
    vec3 white_hot = cubey_srgb_to_linear(vec3(1.0, 0.93, 0.66));
    vec3 warm = mix(ember, orange, smoothstep(0.05, 0.34, heat));
    vec3 hot = mix(yellow, white_hot, core);
    return mix(warm, hot, smoothstep(0.28, 1.0, heat + core * 0.45));
}

vec3 flame_emission(vec4 density, vec3 position) {
    if (inside_obstacle(position)) {
        return vec3(0.0);
    }
    float flame = max(density.b, 0.0);
    float heat = max(density.g, 0.0);
    float soot = max(density.r, 0.0);
    float flame_value = flame_transfer(flame, heat, soot, position);
    float core = pow(smoothstep(0.20, 0.95, flame) * smoothstep(0.20, 1.10, heat), 2.2) *
                 params.color_options.z;
    float halo = smoothstep(0.08, 0.90, heat) * smoothstep(0.015, 0.40, flame) *
                 (1.0 - smoothstep(0.75, 1.80, soot));
    float heat_glow = smoothstep(0.040, 0.72, heat) * (1.0 - smoothstep(0.70, 2.10, soot)) *
                      mix(1.0, 0.34, smoothstep(0.18, 0.92, position.y));
    vec3 halo_color = cubey_srgb_to_linear(vec3(1.0, 0.42, 0.10)) * halo * 0.40;
    vec3 heat_color = cubey_srgb_to_linear(vec3(1.0, 0.30, 0.055)) * heat_glow * 0.30;
    return (flame_color(clamp(heat * 0.34, 0.0, 1.4), core) * flame_value + halo_color +
            heat_color) *
           params.render_options.y * params.color_options.y * 14.0;
}

vec3 display_transform(vec3 color) {
    float exposure = exp2(params.style_options.x);
    return vec3(1.0) - exp(-max(color, vec3(0.0)) * exposure);
}

void main() {
    vec2 screen_position = vec2(frag_position.x, -frag_position.y);
    vec2 screen_uv = vec2(frag_uv.x, 1.0 - frag_uv.y);
    int debug_view = int(params.ray_forward_debug.w + 0.5);
    if (debug_view == 1) {
        vec4 density = density_at(vec3(screen_uv, 0.5));
        out_color = vec4(clamp(vec3(density.r, density.g * 0.35, density.b) +
                                   vec3(density.a * 0.15),
                               vec3(0.0), vec3(1.0)),
                         1.0);
        return;
    }
    if (debug_view == 2) {
        vec3 velocity = texture(velocity_volume, vec3(screen_uv, 0.5)).xyz;
        out_color = vec4(velocity_debug_color(velocity), 1.0);
        return;
    }

    vec3 origin = params.camera_position_steps.xyz;
    vec3 right = params.ray_right_tan.xyz;
    vec3 up = params.ray_up_aspect.xyz;
    vec3 forward = params.ray_forward_debug.xyz;
    float tan_half_fovy = params.ray_right_tan.w;
    float aspect = params.ray_up_aspect.w;
    vec3 direction = normalize(forward + right * screen_position.x * aspect * tan_half_fovy +
                               up * screen_position.y * tan_half_fovy);
    vec3 light_direction = normalize(vec3(0.45, 0.82, 0.35));
    vec3 light_color = cubey_srgb_to_linear(vec3(1.0, 0.92, 0.80));
    vec3 sky_color = cubey_srgb_to_linear(vec3(0.42, 0.54, 0.76));
    float obstacle_t = 0.0;
    bool obstacle_hit = ray_sphere_intersection(origin, direction, obstacle_t);

    float near_t = 0.0;
    float far_t = 0.0;
    vec3 background = background_color(screen_uv);
    if (!ray_box_intersection(origin, direction, near_t, far_t)) {
        out_color = vec4(background, 1.0);
        return;
    }

    near_t = max(near_t, 0.0);
    int steps = max(int(params.camera_position_steps.w + 0.5), 1);
    float path_length = far_t - near_t;
    float step_length = path_length / float(steps);
    vec3 accumulated = vec3(0.0);
    float transmittance = 1.0;
    for (int i = 0; i < steps; ++i) {
        float t = near_t + (float(i) + 0.5) * step_length;
        if (obstacle_hit && obstacle_t >= near_t && obstacle_t <= far_t && t >= obstacle_t) {
            vec3 hit_position = origin + direction * obstacle_t;
            vec3 normal = normalize(hit_position - obstacle_center());
            float diffuse = clamp(dot(normal, light_direction), 0.0, 1.0);
            float rim = pow(1.0 - clamp(dot(-direction, normal), 0.0, 1.0), 2.5);
            vec3 obstacle_color =
                cubey_srgb_to_linear(vec3(0.080, 0.090, 0.100)) +
                light_color * diffuse * 0.24 +
                sky_color * rim * 0.14 * params.style_options.z;
            accumulated += transmittance * obstacle_color;
            transmittance = 0.0;
            break;
        }
        vec3 position = origin + direction * t;
        vec4 density = density_at(position);
        float smoke_density_value = smoke_density(density, position);
        float smoke_extinction = smoke_density_value * params.render_options.x;
        float base_extinction = base_volume_extinction();
        float smoke_alpha = 1.0 - exp(-smoke_extinction * step_length);
        float base_alpha = 1.0 - exp(-base_extinction * step_length);
        float shadow = texture(shadow_volume, clamp(position, vec3(0.0), vec3(1.0))).r;
        float shadowed_light = pow(clamp(shadow, 0.0, 1.0), 1.35);
        float ambient_shadow = mix(0.24, 1.0, shadowed_light);
        float view_light = clamp(dot(-direction, light_direction), 0.0, 1.0);
        float forward_scatter = (0.35 + 0.65 * pow(view_light, 3.0)) * params.style_options.w;
        float rim = pow(1.0 - clamp(dot(direction, light_direction) * 0.5 + 0.5, 0.0, 1.0), 3.0);
        vec3 lighting = sky_color * params.render_options.w * ambient_shadow * 1.25 +
                        light_color * shadowed_light * params.render_options.y *
                            forward_scatter +
                        light_color * shadowed_light * rim * 0.38 * params.style_options.z;
        vec3 base_color = (sky_color * 0.58 + light_color * 0.16) * ambient_shadow;
        accumulated += transmittance * base_color * base_alpha;
        accumulated += transmittance * smoke_albedo(density) * lighting * smoke_alpha;
        accumulated += transmittance * flame_emission(density, position) * step_length;
        transmittance *= exp(-(base_extinction + smoke_extinction + max(density.b, 0.0) * 0.12) *
                              step_length);
        if (transmittance < 0.01) {
            break;
        }
    }

    vec3 color = accumulated + background * transmittance;
    out_color = vec4(clamp(display_transform(color), vec3(0.0), vec3(1.0)), 1.0);
}
