#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"
#include "cubey/environment_lighting.glsl"
#include "fluid_ray_box.glsl"

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

#define PYRO_DOMAIN_HEIGHT params.render_options.z
#include "pyro_3d_domain.glsl"

layout(set = 0, binding = 0) uniform sampler3D density_volume;
layout(set = 0, binding = 1) uniform sampler3D velocity_volume;
layout(set = 0, binding = 2) uniform sampler3D shadow_volume;
layout(set = 0, binding = 3) uniform sampler3D occupancy_volume;

layout(set = 1, binding = 0) uniform EnvironmentLightingBlock {
    CubeyEnvironmentLighting environment_lighting;
};
layout(set = 2, binding = 0) uniform sampler2D scene_color_texture;
layout(set = 2, binding = 1) uniform sampler2D scene_depth_texture;

layout(location = 0) in vec2 frag_position;
layout(location = 1) in vec2 frag_uv;
layout(location = 0) out vec4 out_color;

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

bool fire_render_mode() {
    return int(params.obstacle_options.z + 0.5) == 0;
}

bool explosion_render_mode() {
    return int(params.obstacle_options.z + 0.5) == 1;
}

bool external_background_enabled() {
    return (int(params.color_options.w) & 1) != 0 && int(params.ray_forward_debug.w + 0.5) == 0;
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

float volume_edge_distance(vec3 position) {
    float edge_distance = min(min(position.x, 1.0 - position.x),
                              min(min(position.y, pyro_domain_size().y - position.y),
                                  min(position.z, 1.0 - position.z)));
    return edge_distance;
}

float volume_edge_fade(vec3 position) {
    float edge_distance = volume_edge_distance(position);
    float edge_width = explosion_render_mode() ? 0.14 : 0.045;
    return smoothstep(0.0, edge_width, edge_distance);
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
    return smoke_density(density_at(uv / pyro_domain_size()), uv);
}

vec3 background_color(vec2 uv) {
    vec3 low = cubey_srgb_to_linear(vec3(0.050, 0.060, 0.075));
    vec3 high = cubey_srgb_to_linear(vec3(0.150, 0.185, 0.230));
    vec3 horizon = cubey_srgb_to_linear(vec3(0.095, 0.105, 0.110));
    vec3 color = mix(low, high, uv.y);
    float floor_band = 1.0 - smoothstep(0.00, 0.34, uv.y);
    color = mix(color, horizon, floor_band * 0.50);

    vec3 sky_tint = cubey_env_sky_color(environment_lighting);
    float sky_luma = max(max(sky_tint.r, sky_tint.g), sky_tint.b);
    float tint_response = clamp(environment_lighting.sky_color_options.w, 0.0, 1.0);
    float dynamic_scale = mix(0.35, 1.35, clamp(sky_luma * 2.2, 0.0, 1.0));
    vec3 tinted = mix(color, max(sky_tint, color), tint_response * 0.55);
    return tinted * mix(0.70, 1.35, clamp(params.style_options.y, 0.0, 1.0)) *
           mix(1.0, dynamic_scale, tint_response);
}

float base_volume_extinction(vec3 position) {
    float edge_shell = 1.0 - smoothstep(0.035, 0.18, volume_edge_distance(position));
    float floor_haze = (1.0 - smoothstep(0.00, 0.22, position.y)) * 0.45;
    return params.render_options.w * (0.030 + edge_shell * 0.085 + floor_haze * 0.045);
}

vec3 velocity_debug_color(vec3 velocity) {
    float speed = clamp(length(velocity) * 2.2, 0.0, 1.0);
    vec3 direction = normalize(velocity + vec3(0.00001)) * 0.5 + 0.5;
    return mix(vec3(speed), direction, 0.75);
}

vec3 smoke_albedo(vec4 density) {
    float heat = clamp(density.g * 0.06, 0.0, 1.0);
    float soot = max(density.r, 0.0);
    float soot_signal = smoothstep(0.20, 1.15, soot);
    float warm_signal =
        clamp(params.color_options.x * (1.0 - soot_signal * 0.55) +
                  heat * 0.18 * (1.0 - soot_signal * 0.70),
              0.0, 1.0);
    vec3 deep_soot = cubey_srgb_to_linear(vec3(0.15, 0.16, 0.17));
    vec3 cool_soot = cubey_srgb_to_linear(vec3(0.22, 0.23, 0.24));
    vec3 warm_soot = cubey_srgb_to_linear(vec3(0.40, 0.31, 0.24));
    vec3 hot_smoke = mix(cool_soot, warm_soot, warm_signal);
    return mix(hot_smoke, deep_soot, soot_signal * 0.55);
}

float flame_transfer(float flame, float heat, float soot, vec3 position) {
    float flame_mask = smoothstep(0.055, 0.34, flame);
    float heat_mask = smoothstep(0.035, 0.70, heat);
    float soot_cutoff = 1.0 - smoothstep(0.28, 1.10, soot);
    float height_fade =
        fire_render_mode() ? mix(0.04, 1.0, 1.0 - smoothstep(0.24, 0.82, position.y)) : 1.0;
    return pow(flame_mask * heat_mask * soot_cutoff, 1.75) * height_fade;
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

vec3 amber_flame_emission(float flame, float heat, float soot, float fuel, float height) {
    float flame_layer = smoothstep(0.040, 0.59, flame) * smoothstep(0.045, 0.88, heat);
    float open_flame = 1.0 - smoothstep(0.36, 1.25, soot);
    float height_weight = mix(0.085, 1.0, 1.0 - smoothstep(0.28, 0.86, height));
    float shoulder = smoothstep(0.018, 0.32, flame) * smoothstep(0.095, 0.85, heat);
    float core = smoothstep(0.30, 1.10, flame) * smoothstep(0.42, 1.85, heat);
    float amber = smoothstep(0.12, 0.82, flame) * smoothstep(0.22, 1.40, heat);
    vec3 sheet_color = mix(vec3(1.0, 0.048, 0.004), vec3(1.0, 0.16, 0.006), amber);
    float core_weight = clamp(core * params.color_options.z, 0.0, 1.0);
    sheet_color = mix(sheet_color, vec3(1.0, 0.65, 0.23), core_weight);
    float ridge = smoothstep(0.035, 0.19, flame) * (1.0 - smoothstep(0.19, 0.61, flame));
    float rich = smoothstep(0.15, 0.48, fuel);
    vec3 sheets = mix(vec3(1.0, 0.06, 0.001), sheet_color, 1.0 - 0.82 * rich) *
                  pow(flame_layer, 1.23) * open_flame * height_weight;
    vec3 edge = vec3(1.0, 0.26, 0.018) * ridge * smoothstep(0.08, 0.70, heat) *
                height_weight * open_flame;
    vec3 outer = vec3(1.0, 0.065, 0.006) * shoulder *
                 (1.0 - smoothstep(0.36, 1.15, soot)) * height_weight;
    return (sheets * (0.79 - 0.55 * rich) + edge * 0.22 + outer * 0.075) *
           params.render_options.y * params.color_options.y * 14.0;
}

vec3 flame_emission(vec4 density, vec3 position) {
    if (inside_obstacle(position)) {
        return vec3(0.0);
    }
    float flame = max(density.b, 0.0);
    float heat = max(density.g, 0.0);
    float soot = max(density.r, 0.0);
    if ((int(params.color_options.w) & 4) != 0) {
        return amber_flame_emission(flame, heat, soot, max(density.a, 0.0), position.y);
    }
    float flame_value = flame_transfer(flame, heat, soot, position);
    float flame_height =
        fire_render_mode() ? 1.0 - smoothstep(0.28, 0.70, position.y) : 1.0;
    float core = pow(smoothstep(0.20, 0.95, flame) * smoothstep(0.20, 1.10, heat), 2.2) *
                 params.color_options.z;
    if (explosion_render_mode()) {
        core *= 1.0 + smoothstep(0.08, 0.95, heat) * 0.65;
    }
    float halo = smoothstep(0.08, 0.90, heat) * smoothstep(0.015, 0.40, flame) *
                 (1.0 - smoothstep(0.48, 1.40, soot)) * mix(0.10, 1.0, flame_height);
    float heat_glow = smoothstep(0.040, 0.72, heat) * (1.0 - smoothstep(0.42, 1.35, soot)) *
                      mix(1.0, 0.05, smoothstep(0.18, 0.70, position.y));
    vec3 halo_color = cubey_srgb_to_linear(vec3(1.0, 0.42, 0.10)) * halo * 0.40;
    vec3 heat_color = cubey_srgb_to_linear(vec3(1.0, 0.30, 0.055)) * heat_glow * 0.30;
    return (flame_color(clamp(heat * 0.34, 0.0, 1.4), core) * flame_value + halo_color +
            heat_color) *
           params.render_options.y * params.color_options.y * 14.0;
}

// Gather incident flame radiance from the lower hemisphere. The short rays
// follow the simulated flame tongues and lose light as they cross soot.
vec3 incident_firelight(vec3 position) {
    const vec3 directions[] = vec3[](
        vec3(0.0, -1.0, 0.0), vec3(0.6, -0.8, 0.0), vec3(-0.6, -0.8, 0.0),
        vec3(0.0, -0.8, 0.6), vec3(0.0, -0.8, -0.6));
    const float spacing = 0.15;
    const float scatter_gain = 6.0;
    vec3 incident = vec3(0.0);
    for (int ray = 0; ray < directions.length(); ++ray) {
        vec3 direction = directions[ray];
        float hit;
        float limit = ray_sphere_intersection(position, direction, hit) ? hit : 0.45;
        float transmission = 1.0;
        for (int tap = 0; tap < 3; ++tap) {
            float travel = (float(tap) + 0.5) * spacing;
            vec3 source = position + direction * travel;
            vec3 uv = source / pyro_domain_size();
            if (travel >= limit || any(lessThan(uv, vec3(0.0))) ||
                any(greaterThan(uv, vec3(1.0)))) {
                break;
            }
            vec4 cell = max(textureLod(density_volume, uv, 0.0), vec4(0.0));
            float half_step = exp(-0.5 * spacing * params.render_options.x * cell.r);
            transmission *= half_step;
            // The amber transfer is zero outside these flame, heat and soot thresholds.
            if (cell.b > 0.018 && cell.g > 0.045 && cell.r < 1.25) {
                incident += transmission *
                            amber_flame_emission(cell.b, cell.g, cell.r, cell.a, source.y);
            }
            transmission *= half_step;
        }
    }
    return incident * (scatter_gain * spacing / float(directions.length()));
}

vec3 display_transform(vec3 color) {
    float exposure = exp2(cubey_env_exposure(environment_lighting));
    return vec3(1.0) - exp(-max(color, vec3(0.0)) * exposure);
}


#include "pyro_3d_brick_traversal.glsl"

float scene_ray_distance(vec3 direction) {
    float depth = texture(scene_depth_texture, frag_uv).r;
    if (depth >= 0.999999) {
        return 1.0e20;
    }
    float near_plane = max(params.style_options.x, 0.0001);
    float far_plane = max(params.obstacle_options.w, near_plane + 0.0001);
    float view_distance =
        (near_plane * far_plane) / max(far_plane - depth * (far_plane - near_plane), 0.0001);
    return view_distance / max(dot(direction, params.ray_forward_debug.xyz), 0.0001);
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
    vec3 light_direction = cubey_env_primary_light_direction(environment_lighting);
    vec3 light_color = cubey_env_primary_light(environment_lighting);
    vec3 sky_color = cubey_env_sky_color(environment_lighting);
    float obstacle_t = 0.0;
    bool obstacle_hit = ray_sphere_intersection(origin, direction, obstacle_t);

    float near_t = 0.0;
    float far_t = 0.0;
    bool external_background = external_background_enabled();
    vec3 background =
        external_background ? texture(scene_color_texture, frag_uv).rgb : background_color(screen_uv);
    if (!ray_box_intersection(origin / pyro_domain_size(), direction / pyro_domain_size(), near_t, far_t)) {
        out_color = vec4(clamp(display_transform(background), vec3(0.0), vec3(1.0)), 1.0);
        return;
    }

    near_t = max(near_t, 0.0);
    if (external_background) {
        far_t = min(far_t, scene_ray_distance(direction));
    }
    if (far_t <= near_t) {
        out_color = vec4(clamp(display_transform(background), vec3(0.0), vec3(1.0)), 1.0);
        return;
    }
    int steps = max(int(params.camera_position_steps.w + 0.5), 1);
    float path_length = far_t - near_t;
    float step_length = path_length / float(steps);
    vec3 accumulated = vec3(0.0);
    float transmittance = 1.0;
    bool occupancy_skip_enabled = (int(params.color_options.w) & 2) != 0 && external_background;
    for (int i = 0; i < steps;) {
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
        if (occupancy_skip_enabled) {
            ivec3 cell;
            ivec3 cell_count;
            if (occupancy_is_empty(position, cell, cell_count)) {
                i += samples_to_next_occupancy_cell(
                    origin, direction, near_t, step_length, i, steps,
                    position, cell, cell_count);
                continue;
            }
        }
        vec4 density = density_at(position / pyro_domain_size());
        float smoke_density_value = smoke_density(density, position);
        float smoke_extinction = smoke_density_value * params.render_options.x;
        float base_extinction =
            external_background ? 0.0 : base_volume_extinction(position);
        float smoke_alpha = 1.0 - exp(-smoke_extinction * step_length);
        float base_alpha = 1.0 - exp(-base_extinction * step_length);
        float shadow = texture(shadow_volume, clamp(position / pyro_domain_size(), vec3(0.0), vec3(1.0))).r;
        float shadowed_light = pow(clamp(shadow, 0.0, 1.0), 1.35);
        float ambient_shadow = mix(0.24, 1.0, shadowed_light);
        float view_light = clamp(dot(-direction, light_direction), 0.0, 1.0);
        float forward_scatter = (0.35 + 0.65 * pow(view_light, 3.0)) * params.style_options.w;
        float rim = pow(1.0 - clamp(dot(direction, light_direction) * 0.5 + 0.5, 0.0, 1.0), 3.0);
        vec3 lighting = sky_color * params.render_options.w * ambient_shadow * 1.25 +
                        light_color * shadowed_light * params.render_options.y *
                            forward_scatter +
                        light_color * shadowed_light * rim * 0.38 * params.style_options.z;
        if ((int(params.color_options.w) & 8) != 0 && smoke_density_value > 0.0) {
            lighting += incident_firelight(position);
        }
        vec3 base_color = (sky_color * 0.58 + light_color * 0.16) * ambient_shadow;
        accumulated += transmittance * base_color * base_alpha;
        accumulated += transmittance * smoke_albedo(density) * lighting * smoke_alpha;
        accumulated += transmittance * flame_emission(density, position) * step_length;
        transmittance *= exp(-(base_extinction + smoke_extinction + max(density.b, 0.0) * 0.12) *
                              step_length);
        if (transmittance < 0.01) {
            break;
        }
        ++i;
    }

    vec3 color = accumulated + background * transmittance;
    out_color = vec4(clamp(display_transform(color), vec3(0.0), vec3(1.0)), 1.0);
}
