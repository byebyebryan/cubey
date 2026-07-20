#version 450
#extension GL_GOOGLE_include_directive : require

#include "terrain_environment.glsl"
#include "terrain_lighting.glsl"

layout(push_constant) uniform TerrainBackdropPushConstants {
    mat4 view_projection;
    vec4 camera_position;
    vec4 render_options;
    vec4 material_options;
} pc;

layout(set = 1, binding = 0) uniform sampler2D backdrop_detail_texture;
layout(set = 1, binding = 1) uniform sampler2D terrain_shadow_map;

layout(location = 0) in vec3 frag_world_position;
layout(location = 1) in vec3 frag_material_channels;
layout(location = 2) in vec3 frag_normal;

layout(location = 0) out vec4 out_color;

const float backdrop_detail_period_m = 2048.0;

vec3 srgb_to_linear(vec3 value) {
    bvec3 cutoff = lessThanEqual(value, vec3(0.04045));
    vec3 lower = value / 12.92;
    vec3 higher = pow((value + 0.055) / 1.055, vec3(2.4));
    return mix(higher, lower, cutoff);
}

vec3 debug_color(int debug_view, vec3 normal, float rock, float snow,
                 float ambient_visibility, float sun_visibility) {
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
    if (debug_view == 19) {
        return vec3(sun_visibility);
    }
    if (debug_view == 27) {
        float boundary = 1.0 - smoothstep(
            0.0, 20.0, abs(length(frag_world_position.xz) - pc.render_options.w));
        return mix(vec3(0.10, 0.65, 0.78), vec3(1.0, 0.84, 0.18), boundary);
    }
    return vec3(-1.0);
}

float axis_sign(float value) {
    return value < 0.0 ? -1.0 : 1.0;
}

float terrain_sun_visibility(vec3 world_position, vec3 normal,
                             vec3 light_direction) {
    if (atmosphere.shadow_options.x < 0.5) {
        return 1.0;
    }
    vec4 light_clip = atmosphere.light_view_projection * vec4(world_position, 1.0);
    if (light_clip.w <= 0.0) {
        return 1.0;
    }
    vec3 light_ndc = light_clip.xyz / light_clip.w;
    vec2 shadow_uv = light_ndc.xy * 0.5 + 0.5;
    if (light_ndc.z <= 0.0 || light_ndc.z >= 1.0 ||
        any(lessThanEqual(shadow_uv, vec2(0.0))) ||
        any(greaterThanEqual(shadow_uv, vec2(1.0)))) {
        return 1.0;
    }

    float ndotl = clamp(dot(normal, light_direction), 0.0, 1.0);
    float slope_tangent = sqrt(max(1.0 - ndotl * ndotl, 0.0)) /
        max(ndotl, 0.2);
    float texel_world_m = max(atmosphere.shadow_options.w, 0.0);
    float bias_m = max(
        1.25, texel_world_m * (0.75 + 0.90 * min(slope_tangent, 2.0)));
    float receiver_depth = light_ndc.z -
        bias_m / max(atmosphere.shadow_options.z, 1.0);
    float texel = atmosphere.shadow_options.y;
    float visibility = 0.0;
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            vec2 offset = (vec2(float(x), float(y)) - vec2(0.5)) * texel;
            float blocker_depth = texture(terrain_shadow_map, shadow_uv + offset).r;
            visibility += receiver_depth <= blocker_depth ? 0.25 : 0.0;
        }
    }
    return visibility;
}

void main() {
    vec3 classification_normal = normalize(frag_normal);
    float rock = clamp(frag_material_channels.x, 0.0, 1.0);
    float snow = clamp(frag_material_channels.y, 0.0, 1.0);
    float ambient_visibility = clamp(frag_material_channels.z, 0.65, 1.0);
    int debug_view = int(round(pc.render_options.x));
    vec3 light_direction = normalize(atmosphere.primary_light_direction_intensity.xyz);
    float sun_visibility = terrain_sun_visibility(
        frag_world_position, classification_normal, light_direction);
    vec3 diagnostic = debug_color(debug_view, classification_normal, rock, snow,
                                  ambient_visibility, sun_visibility);
    if (diagnostic.x >= 0.0) {
        out_color = vec4(diagnostic, 1.0);
        return;
    }

    float ground = max(0.0, 1.0 - rock - snow);
    float weight_sum = max(ground + rock + snow, 0.0001);
    ground /= weight_sum;
    rock /= weight_sum;
    snow /= weight_sum;

    vec3 triplanar_weight = pow(abs(classification_normal), vec3(4.0));
    triplanar_weight /= max(dot(triplanar_weight, vec3(1.0)), 0.0001);
    vec4 detail_x = vec4(0.5);
    vec4 detail_y = vec4(0.5);
    vec4 detail_z = vec4(0.5);
    if (pc.material_options.x > 0.5) {
        detail_x = texture(backdrop_detail_texture,
                           frag_world_position.yz / backdrop_detail_period_m);
        detail_y = texture(backdrop_detail_texture,
                           frag_world_position.xz / backdrop_detail_period_m);
        detail_z = texture(backdrop_detail_texture,
                           frag_world_position.xy / backdrop_detail_period_m);
    }
    vec4 detail = detail_x * triplanar_weight.x + detail_y * triplanar_weight.y +
        detail_z * triplanar_weight.z;
    vec2 tangent_x = detail_x.rg * 2.0 - 1.0;
    vec2 tangent_y = detail_y.rg * 2.0 - 1.0;
    vec2 tangent_z = detail_z.rg * 2.0 - 1.0;
    vec3 perturbation =
        vec3(0.0, tangent_x.x, tangent_x.y) *
            (triplanar_weight.x * axis_sign(classification_normal.x)) +
        vec3(tangent_y.x, 0.0, tangent_y.y) *
            (triplanar_weight.y * axis_sign(classification_normal.y)) +
        vec3(tangent_z.x, tangent_z.y, 0.0) *
            (triplanar_weight.z * axis_sign(classification_normal.z));
    vec3 material_normal = normalize(classification_normal + perturbation);
    float normal_strength = 0.16 * ground + 0.42 * rock + 0.06 * snow;
    vec3 normal = normalize(classification_normal + normal_strength * perturbation);

    vec3 base_color = srgb_to_linear(vec3(0.27, 0.255, 0.205)) * ground +
                      srgb_to_linear(vec3(0.39, 0.385, 0.37)) * rock +
                      srgb_to_linear(vec3(0.82, 0.845, 0.86)) * snow;
    float albedo_strength = 0.10 * ground + 0.22 * rock + 0.035 * snow;
    float albedo_variation = detail.b * 2.0 - 1.0;
    base_color *= max(0.0, 1.0 + albedo_strength * albedo_variation);
    float roughness = 0.94 * ground + 0.77 * rock + 0.84 * snow;
    roughness = clamp(roughness + 0.08 * (detail.a * 2.0 - 1.0), 0.0, 1.0);

    if (debug_view == 10) {
        out_color = vec4(normal * 0.5 + 0.5, 1.0);
        return;
    }
    if (debug_view == 15) {
        out_color = vec4(base_color, 1.0);
        return;
    }
    if (debug_view == 16) {
        out_color = vec4(material_normal * 0.5 + 0.5, 1.0);
        return;
    }
    if (debug_view == 18) {
        out_color = vec4(vec3(roughness), 1.0);
        return;
    }
    if (debug_view == 21) {
        out_color = vec4(classification_normal * 0.5 + 0.5, 1.0);
        return;
    }

    vec3 view_direction = normalize(pc.camera_position.xyz - frag_world_position);
    vec3 light_radiance = atmosphere.primary_light_color_angular_radius.xyz *
        atmosphere.primary_light_direction_intensity.w;
    vec3 color = terrain_lighting_ambient(
        base_color, terrain_diffuse_irradiance(normal), ambient_visibility);
    color += terrain_lighting_direct(base_color, roughness, normal, view_direction,
                                     light_direction, light_radiance, sun_visibility);
    CubeyAtmosphereSample aerial = terrain_aerial_perspective(
        pc.camera_position.xyz, frag_world_position);
    out_color = vec4(color * aerial.transmittance + aerial.color, 1.0);
}
