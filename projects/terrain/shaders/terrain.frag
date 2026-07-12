#version 450
#extension GL_GOOGLE_include_directive : require

#include "terrain_environment.glsl"
#include "terrain_lighting.glsl"
#include "terrain_material.glsl"
#include "terrain_source.glsl"

layout(set = 0, binding = 0, std140) uniform TerrainSourceUniforms {
    TerrainSourceGpuParameters source;
} terrain_uniforms;

layout(push_constant) uniform TerrainPushConstants {
    mat4 view_projection;
    vec4 camera_position_vertical_scale;
    vec4 render_options;
} pc;

layout(location = 0) in vec3 frag_world_position;
layout(location = 1) in float frag_base_height_m;
layout(location = 2) in float frag_height_m;
layout(location = 3) in float frag_weathering_delta_m;
layout(location = 4) flat in float frag_lod;
layout(location = 5) in vec2 frag_base_gradient_xz;
layout(location = 6) flat in float frag_child_half_extent_m;
layout(location = 7) flat in float frag_origin_snap_m;
layout(location = 8) flat in float frag_cell_size_m;
layout(location = 9) in float frag_lod_morph;
layout(location = 10) in float frag_footprint_m;
layout(location = 11) in float frag_direct_visibility;
layout(location = 12) in float frag_landform_concavity_m;

layout(location = 0) out vec4 out_color;

vec2 terrain_child_origin() {
    return floor(pc.camera_position_vertical_scale.xz / frag_origin_snap_m) *
        frag_origin_snap_m;
}

bool terrain_covered_by_finer_lod(vec2 world_xz) {
    if (frag_child_half_extent_m <= 0.0) {
        return false;
    }
    vec2 child_origin = terrain_child_origin();
    float raster_guard_m = frag_cell_size_m;
    vec2 child_min = child_origin - vec2(frag_child_half_extent_m - raster_guard_m);
    vec2 child_max = child_origin + vec2(frag_child_half_extent_m - raster_guard_m);
    return all(greaterThan(world_xz, child_min)) && all(lessThan(world_xz, child_max));
}

vec3 terrain_lod_color(float value) {
    const vec3 colors[8] = vec3[8](
        vec3(0.12, 0.72, 0.34), vec3(0.22, 0.66, 0.82),
        vec3(0.32, 0.42, 0.88), vec3(0.60, 0.32, 0.84),
        vec3(0.88, 0.34, 0.62), vec3(0.94, 0.48, 0.24),
        vec3(0.88, 0.72, 0.20), vec3(0.78, 0.84, 0.35));
    int index = clamp(int(round(value * 7.0)), 0, 7);
    return colors[index];
}

vec2 terrain_weathering_gradient_xz() {
    vec2 position_dx = dFdx(frag_world_position.xz);
    vec2 position_dy = dFdy(frag_world_position.xz);
    float determinant = position_dx.x * position_dy.y - position_dx.y * position_dy.x;
    if (abs(determinant) <= 0.000001) {
        return vec2(0.0);
    }
    float delta_dx = dFdx(frag_weathering_delta_m);
    float delta_dy = dFdy(frag_weathering_delta_m);
    return vec2(
        position_dy.y * delta_dx - position_dx.y * delta_dy,
        position_dx.x * delta_dy - position_dy.x * delta_dx) / determinant;
}

void main() {
    vec2 final_gradient_xz = frag_base_gradient_xz + terrain_weathering_gradient_xz();
    vec3 source_normal = normalize(vec3(
        -final_gradient_xz.x * pc.camera_position_vertical_scale.w, 1.0,
        -final_gradient_xz.y * pc.camera_position_vertical_scale.w));
    if (terrain_covered_by_finer_lod(frag_world_position.xz)) {
        discard;
    }
    float normalized_height = clamp(
        (frag_height_m - terrain_uniforms.source.elevation.x) /
            max(terrain_uniforms.source.elevation.y, 1.0),
        0.0, 1.0);
    float slope = 1.0 - clamp(source_normal.y, 0.0, 1.0);
    float ambient_visibility = terrain_lighting_ambient_visibility(
        source_normal, frag_landform_concavity_m);
    int debug_view = int(round(pc.render_options.x));

    if (debug_view == 1) {
        vec3 low = vec3(0.08, 0.18, 0.26);
        vec3 mid = vec3(0.34, 0.48, 0.30);
        vec3 high = vec3(0.92, 0.91, 0.86);
        vec3 color = mix(low, mid, smoothstep(0.04, 0.52, normalized_height));
        color = mix(color, high, smoothstep(0.50, 0.96, normalized_height));
        out_color = vec4(color, 1.0);
        return;
    }
    if (debug_view == 2) {
        float base_normalized = clamp(
            (frag_base_height_m - terrain_uniforms.source.elevation.x) /
                max(terrain_uniforms.source.elevation.y, 1.0),
            0.0, 1.0);
        out_color = vec4(vec3(base_normalized), 1.0);
        return;
    }
    if (debug_view == 3) {
        out_color = vec4(mix(vec3(0.10, 0.25, 0.58), vec3(0.92, 0.36, 0.12),
            smoothstep(0.02, 0.78, slope)), 1.0);
        return;
    }
    if (debug_view == 4) {
        float extent = max(terrain_uniforms.source.weathering.y * 0.03, 0.001);
        float signed_delta = clamp(frag_weathering_delta_m / extent, -1.0, 1.0);
        vec3 neutral = vec3(0.13, 0.14, 0.15);
        vec3 color = signed_delta < 0.0
            ? mix(neutral, vec3(0.16, 0.48, 0.86), -signed_delta)
            : mix(neutral, vec3(0.92, 0.36, 0.12), signed_delta);
        out_color = vec4(color, 1.0);
        return;
    }
    if (debug_view == 5) {
        out_color = vec4(terrain_lod_color(frag_lod), 1.0);
        return;
    }

    if (debug_view == 7) {
        out_color = vec4(vec3(clamp(frag_direct_visibility, 0.0, 1.0)), 1.0);
        return;
    }

    bool clay_view = debug_view == 6;
    // Keep procedural relief filtering continuous across clipmap ownership changes.
    float material_footprint_m = max(
        0.35, length(pc.camera_position_vertical_scale.xyz - frag_world_position) *
                  pc.render_options.z);
    bool backdrop_presentation = int(round(pc.render_options.w)) == 1;
    TerrainMaterialSample material = clay_view
        ? terrain_clay_material(source_normal)
        : terrain_material_sample(source_normal,
                                  vec3(frag_world_position.x, frag_height_m,
                                       frag_world_position.z),
                                  frag_height_m, frag_landform_concavity_m,
                                  material_footprint_m, backdrop_presentation,
                                  terrain_uniforms.source.elevation,
                                  uint(terrain_uniforms.source.macro.control.x));
    if (debug_view == 9) {
        out_color = vec4(material.vegetation.ground, material.vegetation.woody, 0.0, 1.0);
        return;
    }
    if (debug_view == 10) {
        out_color = vec4(source_normal * 0.5 + 0.5, 1.0);
        return;
    }
    if (debug_view == 11) {
        out_color = vec4(
            material.material_weights.z + material.material_weights.y,
            material.material_weights.x + material.material_weights.y,
            material.material_weights.w, 1.0);
        return;
    }
    if (debug_view == 12) {
        out_color = vec4(vec3(ambient_visibility), 1.0);
        return;
    }
    // Relief stays subordinate to the resolved terrain shape at scene scale.
    float material_detail_blend = clamp(
        0.20 + material.material_weights.y * 0.28 +
        material.material_weights.z * 0.52, 0.0, 0.78);
    vec3 normal = clay_view
        ? source_normal
        : normalize(mix(source_normal, material.detail_normal, material_detail_blend));
    vec3 light_direction = normalize(atmosphere.primary_light_direction_intensity.xyz);
    vec3 view_direction = normalize(
        pc.camera_position_vertical_scale.xyz - frag_world_position);
    vec3 light_radiance = atmosphere.primary_light_color_angular_radius.xyz *
        atmosphere.primary_light_direction_intensity.w;
    vec3 color = terrain_lighting_ambient(
        material.base_color, terrain_diffuse_irradiance(normal), ambient_visibility);
    color += terrain_lighting_direct(
        material.base_color, material.roughness, normal, view_direction,
        light_direction, light_radiance, frag_direct_visibility);
    CubeyAtmosphereSample aerial = terrain_aerial_perspective(
        pc.camera_position_vertical_scale.xyz, frag_world_position);
    if (debug_view == 8) {
        out_color = vec4(aerial.transmittance, 1.0);
        return;
    }
    color = color * aerial.transmittance + aerial.color;
    out_color = vec4(color, 1.0);
}
