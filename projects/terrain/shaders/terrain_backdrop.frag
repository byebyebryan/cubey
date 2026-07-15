#version 450
#extension GL_GOOGLE_include_directive : require

#include "terrain_environment.glsl"
#include "terrain_lighting.glsl"

layout(push_constant) uniform TerrainBackdropPushConstants {
    mat4 view_projection;
    vec4 camera_position;
    vec4 render_options;
} pc;

layout(location = 0) in vec3 frag_world_position;
layout(location = 1) in vec3 frag_material_channels;
layout(location = 2) in vec3 frag_normal;

layout(location = 0) out vec4 out_color;

vec3 srgb_to_linear(vec3 value) {
    bvec3 cutoff = lessThanEqual(value, vec3(0.04045));
    vec3 lower = value / 12.92;
    vec3 higher = pow((value + 0.055) / 1.055, vec3(2.4));
    return mix(higher, lower, cutoff);
}

vec3 debug_color(int debug_view, vec3 normal, float rock, float snow,
                 float ambient_visibility) {
    if (debug_view == 1) {
        float normalized_height = clamp(
            (frag_world_position.y - pc.render_options.y) /
                max(pc.render_options.z - pc.render_options.y, 1.0),
            0.0, 1.0);
        return mix(vec3(0.08, 0.18, 0.26), vec3(0.92, 0.91, 0.86),
                   normalized_height);
    }
    if (debug_view == 3) {
        float slope = 1.0 - clamp(normal.y, 0.0, 1.0);
        return mix(vec3(0.10, 0.25, 0.58), vec3(0.92, 0.36, 0.12),
                   smoothstep(0.02, 0.78, slope));
    }
    if (debug_view == 6) {
        return srgb_to_linear(vec3(0.38, 0.39, 0.40));
    }
    if (debug_view == 10 || debug_view == 21) {
        return normal * 0.5 + 0.5;
    }
    if (debug_view == 11) {
        return vec3(rock, snow, max(0.0, 1.0 - rock - snow));
    }
    if (debug_view == 12) {
        return vec3(ambient_visibility);
    }
    if (debug_view == 14) {
        float projected_span = length(fwidth(frag_world_position));
        return mix(vec3(0.10, 0.28, 0.72), vec3(0.95, 0.32, 0.08),
                   smoothstep(8.0, 80.0, projected_span));
    }
    if (debug_view == 27) {
        float boundary = 1.0 - smoothstep(
            0.0, 20.0, abs(length(frag_world_position.xz) - pc.render_options.w));
        return mix(vec3(0.10, 0.65, 0.78), vec3(1.0, 0.84, 0.18), boundary);
    }
    return vec3(-1.0);
}

void main() {
    vec3 normal = normalize(frag_normal);
    float rock = clamp(frag_material_channels.x, 0.0, 1.0);
    float snow = clamp(frag_material_channels.y, 0.0, 1.0);
    float ambient_visibility = clamp(frag_material_channels.z, 0.65, 1.0);
    int debug_view = int(round(pc.render_options.x));
    vec3 diagnostic = debug_color(debug_view, normal, rock, snow, ambient_visibility);
    if (diagnostic.x >= 0.0) {
        out_color = vec4(diagnostic, 1.0);
        return;
    }

    float ground = max(0.0, 1.0 - rock - snow);
    float weight_sum = max(ground + rock + snow, 0.0001);
    ground /= weight_sum;
    rock /= weight_sum;
    snow /= weight_sum;
    vec3 base_color = srgb_to_linear(vec3(0.27, 0.255, 0.205)) * ground +
                      srgb_to_linear(vec3(0.39, 0.385, 0.37)) * rock +
                      srgb_to_linear(vec3(0.82, 0.845, 0.86)) * snow;
    float roughness = 0.94 * ground + 0.77 * rock + 0.84 * snow;
    vec3 view_direction = normalize(pc.camera_position.xyz - frag_world_position);
    vec3 light_direction = normalize(atmosphere.primary_light_direction_intensity.xyz);
    vec3 light_radiance = atmosphere.primary_light_color_angular_radius.xyz *
        atmosphere.primary_light_direction_intensity.w;
    vec3 color = terrain_lighting_ambient(
        base_color, terrain_diffuse_irradiance(normal), ambient_visibility);
    color += terrain_lighting_direct(base_color, roughness, normal, view_direction,
                                     light_direction, light_radiance, 1.0);
    CubeyAtmosphereSample aerial = terrain_aerial_perspective(
        pc.camera_position.xyz, frag_world_position);
    out_color = vec4(color * aerial.transmittance + aerial.color, 1.0);
}
