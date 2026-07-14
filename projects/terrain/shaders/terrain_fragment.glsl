#include "terrain_environment.glsl"
#include "terrain_lighting.glsl"
#include "terrain_material.glsl"
#include "terrain_source.glsl"

#ifndef CUBEY_TERRAIN_QUALITY_MATERIAL
#define CUBEY_TERRAIN_QUALITY_MATERIAL 0
#endif

#ifndef CUBEY_TERRAIN_LAYERED_MATERIAL
#define CUBEY_TERRAIN_LAYERED_MATERIAL 0
#endif

#if CUBEY_TERRAIN_LAYERED_MATERIAL
layout(set = 2, binding = 0) uniform sampler2D terrain_ground_albedo_height;
layout(set = 2, binding = 1) uniform sampler2D terrain_scree_albedo_height;
layout(set = 2, binding = 2) uniform sampler2D terrain_rock_albedo_height;
layout(set = 2, binding = 3) uniform sampler2D terrain_snow_albedo_height;
layout(set = 2, binding = 4) uniform sampler2D terrain_ground_normal_roughness;
layout(set = 2, binding = 5) uniform sampler2D terrain_scree_normal_roughness;
layout(set = 2, binding = 6) uniform sampler2D terrain_rock_normal_roughness;
layout(set = 2, binding = 7) uniform sampler2D terrain_snow_normal_roughness;
#include "terrain_layered_material.glsl"
#elif CUBEY_TERRAIN_QUALITY_MATERIAL
layout(set = 2, binding = 0) uniform sampler2D terrain_ground_tile;
layout(set = 2, binding = 1) uniform sampler2D terrain_scree_tile;
layout(set = 2, binding = 2) uniform sampler2D terrain_rock_tile;
layout(set = 2, binding = 3) uniform sampler2D terrain_snow_tile;
#include "terrain_quality_material.glsl"
#endif

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
layout(location = 13) flat in float frag_tess_factor;
layout(location = 14) flat in float frag_projected_edge_px;

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

vec3 terrain_source_delta_color(float delta_m, float extent_m) {
    float normalized = clamp(delta_m / max(extent_m, 0.001), -1.0, 1.0);
    vec3 neutral = vec3(0.13, 0.14, 0.15);
    return normalized < 0.0
        ? mix(neutral, vec3(0.16, 0.48, 0.86), -normalized)
        : mix(neutral, vec3(0.92, 0.36, 0.12), normalized);
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

#if CUBEY_TERRAIN_LAYERED_MATERIAL
vec2 terrain_fragment_source_gradient_xz(float pixel_footprint_m) {
    float sample_step_m = clamp(pixel_footprint_m * 2.0, 2.0, 12.0);
    float height_x0 = terrain_source_base_height(
        terrain_uniforms.source, frag_world_position.xz - vec2(sample_step_m, 0.0),
        pixel_footprint_m);
    float height_x1 = terrain_source_base_height(
        terrain_uniforms.source, frag_world_position.xz + vec2(sample_step_m, 0.0),
        pixel_footprint_m);
    float height_z0 = terrain_source_base_height(
        terrain_uniforms.source, frag_world_position.xz - vec2(0.0, sample_step_m),
        pixel_footprint_m);
    float height_z1 = terrain_source_base_height(
        terrain_uniforms.source, frag_world_position.xz + vec2(0.0, sample_step_m),
        pixel_footprint_m);
    return vec2(height_x1 - height_x0, height_z1 - height_z0) / (2.0 * sample_step_m);
}
#endif

void main() {
    float material_footprint_m = max(
        0.35, length(pc.camera_position_vertical_scale.xyz - frag_world_position) *
                  pc.render_options.z);
    vec2 weathering_gradient_xz = terrain_weathering_gradient_xz();
    vec2 classification_gradient_xz = frag_base_gradient_xz + weathering_gradient_xz;
    vec2 shading_base_gradient_xz = frag_base_gradient_xz;
#if CUBEY_TERRAIN_LAYERED_MATERIAL
    float fragment_normal_blend = 0.60 *
        (1.0 - smoothstep(3.0, 8.0, material_footprint_m));
    if (fragment_normal_blend > 0.0) {
        shading_base_gradient_xz = mix(
            shading_base_gradient_xz,
            terrain_fragment_source_gradient_xz(material_footprint_m),
            fragment_normal_blend);
    }
#endif
    vec2 shading_gradient_xz = shading_base_gradient_xz + weathering_gradient_xz;
    vec3 classification_normal = normalize(vec3(
        -classification_gradient_xz.x * pc.camera_position_vertical_scale.w, 1.0,
        -classification_gradient_xz.y * pc.camera_position_vertical_scale.w));
    vec3 shading_normal = normalize(vec3(
        -shading_gradient_xz.x * pc.camera_position_vertical_scale.w, 1.0,
        -shading_gradient_xz.y * pc.camera_position_vertical_scale.w));
    if (terrain_covered_by_finer_lod(frag_world_position.xz)) {
        discard;
    }
    float normalized_height = clamp(
        (frag_height_m - terrain_uniforms.source.elevation.x) /
            max(terrain_uniforms.source.elevation.y, 1.0),
        0.0, 1.0);
    float slope = 1.0 - clamp(classification_normal.y, 0.0, 1.0);
    float ambient_visibility = terrain_lighting_ambient_visibility(
        classification_normal, frag_landform_concavity_m);
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
    bool backdrop_presentation = int(round(pc.render_options.w)) == 1;
    TerrainMaterialSample material = clay_view
        ? terrain_clay_material(shading_normal)
        : terrain_material_sample(classification_normal,
                                  vec3(frag_world_position.x, frag_height_m,
                                       frag_world_position.z),
                                  frag_height_m, frag_landform_concavity_m,
                                  material_footprint_m, backdrop_presentation,
                                  terrain_uniforms.source.elevation,
                                  uint(terrain_uniforms.source.macro.control.x));
#if CUBEY_TERRAIN_LAYERED_MATERIAL
    if (!clay_view) {
        material = terrain_layered_material_sample(
            material, shading_normal, frag_world_position, material_footprint_m);
    }
#elif CUBEY_TERRAIN_QUALITY_MATERIAL
    if (!clay_view) {
        material = terrain_quality_material_sample(
            material, shading_normal, frag_world_position, material_footprint_m);
    }
#endif
    if (debug_view == 9) {
        out_color = vec4(material.vegetation.ground, material.vegetation.woody, 0.0, 1.0);
        return;
    }
    if (debug_view == 10) {
        out_color = vec4(shading_normal * 0.5 + 0.5, 1.0);
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
    if (debug_view == 13) {
        float factor = clamp(log2(max(frag_tess_factor, 1.0)) / 6.0, 0.0, 1.0);
        out_color = vec4(mix(vec3(0.08, 0.20, 0.62), vec3(0.96, 0.32, 0.10), factor), 1.0);
        return;
    }
    if (debug_view == 14) {
        float edge = clamp(frag_projected_edge_px / 8.0, 0.0, 1.0);
        out_color = vec4(mix(vec3(0.10, 0.55, 0.24), vec3(0.96, 0.18, 0.08), edge), 1.0);
        return;
    }
    if (debug_view == 15) {
        out_color = vec4(material.base_color, 1.0);
        return;
    }
    if (debug_view == 16) {
        out_color = vec4(material.detail_normal * 0.5 + 0.5, 1.0);
        return;
    }
    if (debug_view == 17) {
        vec3 bands;
#if CUBEY_TERRAIN_SOURCE_VARIANT == 1
        TerrainSourceComponents components = terrain_source_v3_components(
            terrain_uniforms.source, frag_world_position.xz, frag_footprint_m);
        bands = vec3(components.range_support,
            clamp(components.massif_height_m / max(terrain_uniforms.source.elevation.y, 1.0),
                0.0, 1.0),
            clamp(components.ridge_delta_m /
                max(terrain_uniforms.source.v3_composition_1.x, 1.0), 0.0, 1.0));
#elif CUBEY_TERRAIN_SOURCE_VARIANT == 0 || CUBEY_TERRAIN_SOURCE_VARIANT == 3
        bands = vec3(
            terrain_source_band(terrain_uniforms.source.macro, frag_world_position.xz,
                                frag_footprint_m),
            terrain_source_band(terrain_uniforms.source.structure, frag_world_position.xz,
                                frag_footprint_m),
            terrain_source_band(terrain_uniforms.source.detail, frag_world_position.xz,
                                frag_footprint_m));
#else
        if (terrain_uniforms.source.source_control.x == 2) {
            TerrainSourceComponents components = terrain_source_v3_components(
                terrain_uniforms.source, frag_world_position.xz, frag_footprint_m);
            bands = vec3(components.range_support,
                clamp(components.massif_height_m / max(terrain_uniforms.source.elevation.y, 1.0),
                    0.0, 1.0),
                clamp(components.ridge_delta_m /
                    max(terrain_uniforms.source.v3_composition_1.x, 1.0), 0.0, 1.0));
        } else {
            bands = vec3(
                terrain_source_band(terrain_uniforms.source.macro, frag_world_position.xz,
                                    frag_footprint_m),
                terrain_source_band(terrain_uniforms.source.structure, frag_world_position.xz,
                                    frag_footprint_m),
                terrain_source_band(terrain_uniforms.source.detail, frag_world_position.xz,
                                    frag_footprint_m));
        }
#endif
        out_color = vec4(bands, 1.0);
        return;
    }
    if (debug_view == 18) {
        out_color = vec4(vec3(material.roughness), 1.0);
        return;
    }
    if (debug_view == 19) {
        out_color = vec4(vec3(material.blend_height), 1.0);
        return;
    }
    if (debug_view == 20) {
        out_color = vec4(vec3(material.cavity), 1.0);
        return;
    }
    if (debug_view == 21) {
        out_color = vec4(classification_normal * 0.5 + 0.5, 1.0);
        return;
    }
#if CUBEY_TERRAIN_SOURCE_VARIANT == 1 || CUBEY_TERRAIN_SOURCE_VARIANT == 2
    if (debug_view >= 22 && debug_view <= 26) {
        TerrainSourceComponents components = terrain_source_v3_components(
            terrain_uniforms.source, frag_world_position.xz, frag_footprint_m);
        if (debug_view == 22) {
            out_color = vec4(vec3(components.range_support), 1.0);
        } else if (debug_view == 23) {
            float massif = clamp(
                components.massif_height_m / max(terrain_uniforms.source.elevation.y, 1.0),
                0.0, 1.0);
            out_color = vec4(vec3(massif), 1.0);
        } else if (debug_view == 24) {
            out_color = vec4(terrain_source_delta_color(components.valley_delta_m,
                terrain_uniforms.source.v3_composition_0.z), 1.0);
        } else if (debug_view == 25) {
            out_color = vec4(terrain_source_delta_color(components.ridge_delta_m,
                terrain_uniforms.source.v3_composition_1.x), 1.0);
        } else {
            out_color = vec4(terrain_source_delta_color(components.meso_delta_m,
                terrain_uniforms.source.v3_composition_1.z), 1.0);
        }
        return;
    }
#endif
    // Relief stays subordinate to the resolved terrain shape at scene scale.
    float material_detail_blend = clamp(
        0.20 + material.material_weights.y * 0.28 +
        material.material_weights.z * 0.52, 0.0, 0.78);
    vec3 normal = clay_view
        ? shading_normal
        : normalize(mix(shading_normal, material.detail_normal, material_detail_blend));
    vec3 light_direction = normalize(atmosphere.primary_light_direction_intensity.xyz);
    vec3 view_direction = normalize(
        pc.camera_position_vertical_scale.xyz - frag_world_position);
    vec3 light_radiance = atmosphere.primary_light_color_angular_radius.xyz *
        atmosphere.primary_light_direction_intensity.w;
    vec3 color = terrain_lighting_ambient(
        material.base_color, terrain_diffuse_irradiance(normal),
        ambient_visibility * material.cavity);
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
