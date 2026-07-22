#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/terrain/terrain_environment.glsl"
#include "cubey/terrain/terrain_lighting.glsl"

layout(push_constant) uniform TerrainBackdropPushConstants {
    mat4 view_projection;
    vec4 camera_position;
    vec4 render_options;
    vec4 material_options;
    vec4 world_translation;
} pc;

layout(set = 1, binding = 0) uniform sampler2D backdrop_detail_texture;
layout(set = 1, binding = 1) uniform sampler2DShadow terrain_shadow_map;

layout(location = 0) in vec3 frag_world_position;
layout(location = 1) in vec3 frag_material_channels;
layout(location = 2) in vec3 frag_normal;
layout(location = 3) in vec2 frag_surface_channels;

layout(location = 0) out vec4 out_color;

const float backdrop_macro_period_m = 32768.0;
const float backdrop_local_period_m = 2048.0;

vec3 srgb_to_linear(vec3 value) {
    bvec3 cutoff = lessThanEqual(value, vec3(0.04045));
    vec3 lower = value / 12.92;
    vec3 higher = pow((value + 0.055) / 1.055, vec3(2.4));
    return mix(higher, lower, cutoff);
}

vec3 debug_color(int debug_view, vec3 normal, float rock, float snow,
                 float vegetation, float moisture, float ambient_visibility,
                 float sun_visibility) {
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
    if (debug_view == 22) {
        return vec3(vegetation);
    }
    if (debug_view == 23) {
        return mix(vec3(0.36, 0.22, 0.10), vec3(0.10, 0.36, 0.72), moisture);
    }
    if (debug_view == 27) {
        float boundary = 1.0 - smoothstep(
            0.0, 20.0, abs(length(frag_world_position.xz) - pc.render_options.w));
        return mix(vec3(0.10, 0.65, 0.78), vec3(1.0, 0.84, 0.18), boundary);
    }
    return vec3(-1.0);
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
        0.75, texel_world_m * (0.25 + 0.35 * min(slope_tangent, 2.0)));
    float receiver_depth = light_ndc.z -
        bias_m / max(atmosphere.shadow_options.z, 1.0);
    float texel = atmosphere.shadow_options.y;
    const vec2 tap_offsets[4] = vec2[4](
        vec2(-0.5, -0.5), vec2(0.5, -0.5),
        vec2(-0.5, 0.5), vec2(0.5, 0.5));
    float visibility = 0.0;
    for (int tap = 0; tap < 4; ++tap) {
        visibility += texture(
            terrain_shadow_map,
            vec3(shadow_uv + tap_offsets[tap] * texel, receiver_depth));
    }
    return visibility * 0.25;
}

void main() {
    vec3 classification_normal = normalize(frag_normal);
    float rock = clamp(frag_material_channels.x, 0.0, 1.0);
    float snow = clamp(frag_material_channels.y, 0.0, 1.0);
    float ambient_visibility = clamp(frag_material_channels.z, 0.65, 1.0);
    float vegetation = clamp(frag_surface_channels.x, 0.0, 1.0);
    float moisture = clamp(frag_surface_channels.y, 0.0, 1.0);
    int debug_view = int(round(pc.render_options.x));
    vec3 light_direction = normalize(atmosphere.primary_light_direction_intensity.xyz);
    float sun_visibility = terrain_sun_visibility(
        frag_world_position, classification_normal, light_direction);
    vec3 diagnostic = debug_color(debug_view, classification_normal, rock, snow, vegetation,
                                  moisture, ambient_visibility, sun_visibility);
    if (diagnostic.x >= 0.0) {
        out_color = vec4(diagnostic, 1.0);
        return;
    }

    float ground = max(0.0, 1.0 - rock - snow);
    float weight_sum = max(ground + rock + snow, 0.0001);
    ground /= weight_sum;
    rock /= weight_sum;
    snow /= weight_sum;
    vegetation = min(vegetation / weight_sum, ground);
    float soil = ground - vegetation;

    vec4 macro_detail = vec4(0.5);
    vec4 local_planar_detail = vec4(0.5);
    vec3 planar_perturbation = vec3(0.0);
    if (pc.material_options.x > 0.5) {
        macro_detail = texture(backdrop_detail_texture,
                               frag_world_position.xz / backdrop_macro_period_m);
        vec2 planar_uv = frag_world_position.xz / backdrop_local_period_m;
        local_planar_detail = texture(backdrop_detail_texture, planar_uv);
        vec2 planar_tangent = local_planar_detail.rg * 2.0 - 1.0;
        planar_perturbation = vec3(planar_tangent.x, 0.0, planar_tangent.y);
    }
    vec3 perturbation = planar_perturbation;
    vec3 material_normal = normalize(classification_normal + 0.55 * perturbation);
    float normal_strength = 0.14 * soil + 0.10 * vegetation +
                            0.38 * rock + 0.05 * snow;
    vec3 normal = normalize(classification_normal + normal_strength * perturbation);

    vec3 flat_base_color = srgb_to_linear(vec3(0.27, 0.255, 0.205)) * ground +
                           srgb_to_linear(vec3(0.39, 0.385, 0.37)) * rock +
                           srgb_to_linear(vec3(0.82, 0.845, 0.86)) * snow;
    float filtered_detail = step(0.5, pc.material_options.x);
    float macro_mineral = smoothstep(0.18, 0.82, macro_detail.b);
    float rock_mineral = smoothstep(
        0.16, 0.84, 0.72 * macro_detail.b + 0.28 * macro_detail.a);
    vec3 refined_ground = mix(
        srgb_to_linear(vec3(0.260, 0.275, 0.280)),
        srgb_to_linear(vec3(0.335, 0.300, 0.255)), macro_mineral);
    vec3 refined_rock = mix(
        srgb_to_linear(vec3(0.340, 0.355, 0.370)),
        srgb_to_linear(vec3(0.430, 0.385, 0.335)), rock_mineral);
    vec3 refined_snow = mix(
        srgb_to_linear(vec3(0.68, 0.72, 0.75)),
        srgb_to_linear(vec3(0.77, 0.80, 0.82)),
        smoothstep(0.20, 0.80, macro_detail.b));
    vec3 refined_base_color = refined_ground * ground + refined_rock * rock +
                              refined_snow * snow;
    if (vegetation > 0.0001) {
        vec3 vegetation_color = mix(
            srgb_to_linear(vec3(0.300, 0.305, 0.220)),
            srgb_to_linear(vec3(0.160, 0.240, 0.160)), moisture);
        flat_base_color = srgb_to_linear(vec3(0.27, 0.255, 0.205)) * soil +
                          vegetation_color * vegetation +
                          srgb_to_linear(vec3(0.39, 0.385, 0.37)) * rock +
                          srgb_to_linear(vec3(0.82, 0.845, 0.86)) * snow;
        refined_base_color = refined_ground * soil + vegetation_color * vegetation +
                             refined_rock * rock + refined_snow * snow;
    }
    vec3 base_color = mix(flat_base_color, refined_base_color, filtered_detail);
    float macro_albedo = macro_detail.b * 2.0 - 1.0;
    float planar_albedo = local_planar_detail.b * 2.0 - 1.0;
    float albedo_variation =
        soil * (0.040 * macro_albedo + 0.018 * planar_albedo) +
        vegetation * (0.022 * macro_albedo + 0.008 * planar_albedo) +
        rock * (0.055 * macro_albedo + 0.045 * planar_albedo) +
        snow * (0.035 * macro_albedo + 0.012 * planar_albedo);
    base_color *= max(0.0, 1.0 + albedo_variation);
    float flat_roughness =
        0.94 * soil + 0.93 * vegetation + 0.77 * rock + 0.84 * snow;
    float refined_roughness =
        0.91 * soil + 0.90 * vegetation + 0.70 * rock + 0.84 * snow;
    float roughness = mix(flat_roughness, refined_roughness, filtered_detail);
    float macro_roughness = macro_detail.a * 2.0 - 1.0;
    float local_roughness = local_planar_detail.a * 2.0 - 1.0;
    roughness = clamp(roughness + filtered_detail *
                      (0.07 * macro_roughness + 0.02 * local_roughness),
                      0.0, 1.0);

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
    float occlusion_strength =
        0.90 * soil + 0.95 * vegetation + 1.20 * rock + 0.75 * snow;
    float refined_ambient_visibility = clamp(
        1.0 - (1.0 - ambient_visibility) * occlusion_strength, 0.55, 1.0);
    float material_ambient_visibility = mix(
        ambient_visibility, refined_ambient_visibility, filtered_detail);
    vec3 ambient_light = terrain_lighting_ambient(
        base_color, terrain_diffuse_irradiance(normal), material_ambient_visibility);
    vec3 direct_light = terrain_lighting_direct(
        base_color, roughness, normal, view_direction, light_direction,
        light_radiance, sun_visibility);
    if (debug_view == 24) {
        out_color = vec4(ambient_light, 1.0);
        return;
    }
    if (debug_view == 25) {
        out_color = vec4(direct_light, 1.0);
        return;
    }
    vec3 color = ambient_light + direct_light;
    CubeyAtmosphereSample aerial = terrain_aerial_perspective(
        pc.camera_position.xyz, frag_world_position);
    out_color = vec4(color * aerial.transmittance + aerial.color, 1.0);
}
