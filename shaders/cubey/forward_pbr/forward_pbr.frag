#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/pbr.glsl"

layout(set = 0, binding = 0) uniform PbrSceneUniforms {
    mat4 view_projection;
    mat4 light_view_projection;
    vec4 camera_position;
    vec4 light_direction;
    vec4 light_color_intensity;
    vec4 ambient_color_intensity;
    vec4 environment_intensity_mip_count;
    vec4 display_transform;
    vec4 debug_options;
} scene;

layout(set = 0, binding = 1) uniform sampler2D shadow_map;
layout(set = 0, binding = 2) uniform samplerCube irradiance_cube;
layout(set = 0, binding = 3) uniform samplerCube prefiltered_cube;
layout(set = 0, binding = 4) uniform sampler2D brdf_lut;
layout(set = 1, binding = 0) uniform sampler2D base_color_texture;
layout(set = 1, binding = 1) uniform sampler2D metallic_roughness_texture;
layout(set = 1, binding = 2) uniform sampler2D normal_texture;
layout(set = 1, binding = 3) uniform sampler2D occlusion_texture;
layout(set = 1, binding = 4) uniform sampler2D emissive_texture;
layout(set = 1, binding = 5) uniform sampler2D specular_texture;
layout(set = 1, binding = 6) uniform sampler2D specular_color_texture;
layout(set = 1, binding = 8) uniform sampler2D clearcoat_texture;
layout(set = 1, binding = 9) uniform sampler2D clearcoat_roughness_texture;
layout(set = 1, binding = 10) uniform sampler2D clearcoat_normal_texture;
layout(set = 1, binding = 11) uniform sampler2D sheen_color_texture;
layout(set = 1, binding = 12) uniform sampler2D sheen_roughness_texture;
layout(set = 1, binding = 13) uniform sampler2D anisotropy_texture;
layout(set = 1, binding = 14) uniform sampler2D iridescence_texture;
layout(set = 1, binding = 15) uniform sampler2D iridescence_thickness_texture;

struct PbrTextureTransform {
    vec4 offset_scale;
    vec4 rotation_texcoord;
};

layout(set = 1, binding = 7) uniform PbrMaterialUniforms {
    vec4 base_color_factor;
    vec4 emissive_alpha_cutoff;
    vec4 metallic_roughness_normal_occlusion;
    vec4 specular_color_factor;
    vec4 material_model;
    vec4 clearcoat_factor_roughness_normal;
    vec4 sheen_color_roughness;
    vec4 anisotropy_iridescence;
    vec4 iridescence_ior_thickness;
    PbrTextureTransform base_color_transform;
    PbrTextureTransform metallic_roughness_transform;
    PbrTextureTransform normal_transform;
    PbrTextureTransform occlusion_transform;
    PbrTextureTransform emissive_transform;
    PbrTextureTransform specular_transform;
    PbrTextureTransform specular_color_transform;
    PbrTextureTransform clearcoat_transform;
    PbrTextureTransform clearcoat_roughness_transform;
    PbrTextureTransform clearcoat_normal_transform;
    PbrTextureTransform sheen_color_transform;
    PbrTextureTransform sheen_roughness_transform;
    PbrTextureTransform anisotropy_transform;
    PbrTextureTransform iridescence_transform;
    PbrTextureTransform iridescence_thickness_transform;
} material;

const uint CUBEY_PBR_TEXTURE_SPECULAR = 1u;
const uint CUBEY_PBR_TEXTURE_SPECULAR_COLOR = 2u;
const uint CUBEY_PBR_TEXTURE_CLEARCOAT = 4u;
const uint CUBEY_PBR_TEXTURE_CLEARCOAT_ROUGHNESS = 8u;
const uint CUBEY_PBR_TEXTURE_CLEARCOAT_NORMAL = 16u;
const uint CUBEY_PBR_TEXTURE_SHEEN_COLOR = 32u;
const uint CUBEY_PBR_TEXTURE_SHEEN_ROUGHNESS = 64u;
const uint CUBEY_PBR_TEXTURE_ANISOTROPY = 128u;
const uint CUBEY_PBR_TEXTURE_IRIDESCENCE = 256u;
const uint CUBEY_PBR_TEXTURE_IRIDESCENCE_THICKNESS = 512u;

const uint CUBEY_PBR_DEBUG_FINAL = 0u;
const uint CUBEY_PBR_DEBUG_BASE_COLOR = 1u;
const uint CUBEY_PBR_DEBUG_NORMAL = 2u;
const uint CUBEY_PBR_DEBUG_GEOMETRIC_NORMAL = 3u;
const uint CUBEY_PBR_DEBUG_ROUGHNESS = 4u;
const uint CUBEY_PBR_DEBUG_METALLIC = 5u;
const uint CUBEY_PBR_DEBUG_OCCLUSION = 6u;
const uint CUBEY_PBR_DEBUG_EMISSIVE = 7u;
const uint CUBEY_PBR_DEBUG_SHADOW = 8u;
const uint CUBEY_PBR_DEBUG_ALPHA = 9u;
const uint CUBEY_PBR_DEBUG_UV0 = 10u;

bool cubey_pbr_has_material_texture(uint flag) {
    return (uint(material.material_model.w + 0.5) & flag) != 0u;
}

layout(location = 0) in vec3 frag_world_position;
layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec3 frag_tangent;
layout(location = 3) in vec3 frag_bitangent;
layout(location = 4) in vec2 frag_uv0;
layout(location = 5) in vec2 frag_uv1;
layout(location = 6) in vec4 frag_color0;
layout(location = 7) in vec4 frag_shadow_position;

layout(location = 0) out vec4 out_color;

vec2 cubey_pbr_transformed_uv(PbrTextureTransform transform) {
    vec2 uv = transform.rotation_texcoord.z > 0.5 ? frag_uv1 : frag_uv0;
    vec2 scaled = uv * transform.offset_scale.zw;
    float c = transform.rotation_texcoord.x;
    float s = transform.rotation_texcoord.y;
    vec2 rotated = vec2((c * scaled.x) - (s * scaled.y),
                        (s * scaled.x) + (c * scaled.y));
    return rotated + transform.offset_scale.xy;
}

vec3 rotate_environment_direction(vec3 direction) {
    float c = scene.environment_intensity_mip_count.z;
    float s = scene.environment_intensity_mip_count.w;
    return vec3(
        (c * direction.x) + (s * direction.z),
        direction.y,
        (-s * direction.x) + (c * direction.z)
    );
}

vec4 cubey_pbr_debug_output(uint debug_view, vec4 base_color, float metallic,
                            float roughness, vec3 geometric_normal, vec3 normal,
                            float occlusion, vec3 emissive, float shadow) {
    if (debug_view == CUBEY_PBR_DEBUG_BASE_COLOR) {
        return vec4(base_color.rgb, 1.0);
    }
    if (debug_view == CUBEY_PBR_DEBUG_NORMAL) {
        return vec4((normal * 0.5) + 0.5, 1.0);
    }
    if (debug_view == CUBEY_PBR_DEBUG_GEOMETRIC_NORMAL) {
        return vec4((geometric_normal * 0.5) + 0.5, 1.0);
    }
    if (debug_view == CUBEY_PBR_DEBUG_ROUGHNESS) {
        return vec4(vec3(roughness), 1.0);
    }
    if (debug_view == CUBEY_PBR_DEBUG_METALLIC) {
        return vec4(vec3(metallic), 1.0);
    }
    if (debug_view == CUBEY_PBR_DEBUG_OCCLUSION) {
        return vec4(vec3(occlusion), 1.0);
    }
    if (debug_view == CUBEY_PBR_DEBUG_EMISSIVE) {
        return vec4(emissive, 1.0);
    }
    if (debug_view == CUBEY_PBR_DEBUG_SHADOW) {
        return vec4(vec3(shadow), 1.0);
    }
    if (debug_view == CUBEY_PBR_DEBUG_ALPHA) {
        return vec4(vec3(base_color.a), 1.0);
    }
    if (debug_view == CUBEY_PBR_DEBUG_UV0) {
        return vec4(fract(frag_uv0), 0.0, 1.0);
    }
    return vec4(base_color.rgb, 1.0);
}

float shadow_visibility(vec4 shadow_position, vec3 normal, vec3 light_direction) {
    vec3 shadow_ndc = shadow_position.xyz / shadow_position.w;
    vec2 uv = (shadow_ndc.xy * 0.5) + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || shadow_ndc.z < 0.0 ||
        shadow_ndc.z > 1.0) {
        return 1.0;
    }

    float bias = max(0.002 * (1.0 - max(dot(normal, light_direction), 0.0)), 0.0007);
    vec2 texel_size = 1.0 / vec2(textureSize(shadow_map, 0));
    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float closest_depth = texture(shadow_map, uv + vec2(x, y) * texel_size).r;
            visibility += shadow_ndc.z - bias > closest_depth ? 0.34 : 1.0;
        }
    }
    return visibility / 9.0;
}

void main() {
    uint debug_view = uint(scene.debug_options.x + 0.5);
    vec4 base_color =
        texture(base_color_texture, cubey_pbr_transformed_uv(material.base_color_transform)) *
        material.base_color_factor;
    base_color *= frag_color0;
    float alpha_cutoff = material.emissive_alpha_cutoff.w;
    if (alpha_cutoff > 0.0 && base_color.a < alpha_cutoff) {
        discard;
    }
    float output_alpha = material.material_model.y > 1.5 ? base_color.a : 1.0;
    if (debug_view == CUBEY_PBR_DEBUG_FINAL && material.material_model.z > 0.5) {
        out_color = vec4(base_color.rgb * output_alpha, output_alpha);
        return;
    }

    vec4 metallic_roughness_sample = texture(
        metallic_roughness_texture,
        cubey_pbr_transformed_uv(material.metallic_roughness_transform));
    float metallic = clamp(material.metallic_roughness_normal_occlusion.x *
                               metallic_roughness_sample.b,
                           0.0, 1.0);
    float roughness = clamp(material.metallic_roughness_normal_occlusion.y *
                                metallic_roughness_sample.g,
                            0.04, 1.0);
    float normal_scale = material.metallic_roughness_normal_occlusion.z;
    float occlusion_strength = material.metallic_roughness_normal_occlusion.w;

    vec3 geometric_normal = normalize(frag_normal);
    vec3 tangent = normalize(frag_tangent);
    vec3 bitangent = normalize(frag_bitangent);
    mat3 tbn = mat3(tangent, bitangent, geometric_normal);
    vec3 sampled_normal =
        texture(normal_texture, cubey_pbr_transformed_uv(material.normal_transform)).xyz *
            2.0 -
        1.0;
    sampled_normal.xy *= normal_scale;
    vec3 normal = normalize(tbn * sampled_normal);
    float occlusion = mix(
        1.0, texture(occlusion_texture, cubey_pbr_transformed_uv(material.occlusion_transform)).r,
        occlusion_strength);

    float clearcoat_factor = clamp(material.clearcoat_factor_roughness_normal.x, 0.0, 1.0);
    if (cubey_pbr_has_material_texture(CUBEY_PBR_TEXTURE_CLEARCOAT)) {
        clearcoat_factor *=
            texture(clearcoat_texture, cubey_pbr_transformed_uv(material.clearcoat_transform)).r;
    }
    float clearcoat_roughness = clamp(material.clearcoat_factor_roughness_normal.y, 0.04, 1.0);
    if (cubey_pbr_has_material_texture(CUBEY_PBR_TEXTURE_CLEARCOAT_ROUGHNESS)) {
        clearcoat_roughness =
            clamp(clearcoat_roughness *
                      texture(clearcoat_roughness_texture,
                              cubey_pbr_transformed_uv(material.clearcoat_roughness_transform)).g,
                  0.04, 1.0);
    }
    vec3 clearcoat_normal = geometric_normal;
    if (cubey_pbr_has_material_texture(CUBEY_PBR_TEXTURE_CLEARCOAT_NORMAL)) {
        vec3 sampled_clearcoat_normal =
            texture(clearcoat_normal_texture,
                    cubey_pbr_transformed_uv(material.clearcoat_normal_transform)).xyz *
                2.0 -
            1.0;
        sampled_clearcoat_normal.xy *= material.clearcoat_factor_roughness_normal.z;
        clearcoat_normal = normalize(tbn * sampled_clearcoat_normal);
    }

    vec3 view_direction = normalize(scene.camera_position.xyz - frag_world_position);
    vec3 light_direction = normalize(scene.light_direction.xyz);
    vec3 half_vector = normalize(view_direction + light_direction);
    float ndotl = max(dot(normal, light_direction), 0.0);
    float ndotv = max(dot(normal, view_direction), 0.0);
    float ndoth = max(dot(normal, half_vector), 0.0);
    float vdoth = max(dot(half_vector, view_direction), 0.0);

    vec3 albedo = base_color.rgb;
    vec3 diffuse_color = cubey_pbr_diffuse_color(albedo, metallic);
    float specular_strength = material.specular_color_factor.a;
    if (cubey_pbr_has_material_texture(CUBEY_PBR_TEXTURE_SPECULAR)) {
        specular_strength *=
            texture(specular_texture, cubey_pbr_transformed_uv(material.specular_transform)).a;
    }
    vec3 specular_color_factor = material.specular_color_factor.rgb;
    if (cubey_pbr_has_material_texture(CUBEY_PBR_TEXTURE_SPECULAR_COLOR)) {
        vec2 specular_color_uv =
            cubey_pbr_transformed_uv(material.specular_color_transform);
        specular_color_factor *= texture(specular_color_texture, specular_color_uv).rgb;
    }
    vec3 dielectric_f0 = cubey_pbr_dielectric_f0(
        specular_color_factor, specular_strength, material.material_model.x);
    vec3 f0 = cubey_pbr_f0(albedo, metallic, dielectric_f0);
    float iridescence_factor = clamp(material.anisotropy_iridescence.w, 0.0, 1.0);
    if (cubey_pbr_has_material_texture(CUBEY_PBR_TEXTURE_IRIDESCENCE)) {
        iridescence_factor *= texture(iridescence_texture,
                                      cubey_pbr_transformed_uv(material.iridescence_transform)).r;
    }
    float iridescence_thickness = material.iridescence_ior_thickness.z;
    if (cubey_pbr_has_material_texture(CUBEY_PBR_TEXTURE_IRIDESCENCE_THICKNESS)) {
        float thickness_sample =
            texture(iridescence_thickness_texture,
                    cubey_pbr_transformed_uv(material.iridescence_thickness_transform)).g;
        iridescence_thickness = mix(material.iridescence_ior_thickness.y,
                                   material.iridescence_ior_thickness.z, thickness_sample);
    }
    f0 = cubey_pbr_iridescence_f0(f0, iridescence_factor,
                                  material.iridescence_ior_thickness.x, iridescence_thickness);

    vec3 anisotropy_tangent = tangent;
    vec3 anisotropy_bitangent = bitangent;
    float anisotropy_strength = clamp(material.anisotropy_iridescence.x, 0.0, 1.0);
    vec2 anisotropy_direction = normalize(material.anisotropy_iridescence.yz);
    if (cubey_pbr_has_material_texture(CUBEY_PBR_TEXTURE_ANISOTROPY)) {
        vec3 anisotropy_sample =
            texture(anisotropy_texture, cubey_pbr_transformed_uv(material.anisotropy_transform))
                .rgb;
        vec2 texture_direction = (anisotropy_sample.rg * 2.0) - 1.0;
        if (dot(texture_direction, texture_direction) > 0.0001) {
            vec2 rotated_direction =
                vec2((anisotropy_direction.x * texture_direction.x) -
                         (anisotropy_direction.y * texture_direction.y),
                     (anisotropy_direction.y * texture_direction.x) +
                         (anisotropy_direction.x * texture_direction.y));
            anisotropy_direction = normalize(rotated_direction);
        }
        anisotropy_strength *= anisotropy_sample.b;
    }
    anisotropy_tangent =
        normalize((tangent * anisotropy_direction.x) + (bitangent * anisotropy_direction.y));
    anisotropy_tangent = normalize(anisotropy_tangent -
                                   (normal * dot(normal, anisotropy_tangent)));
    anisotropy_bitangent = normalize(cross(normal, anisotropy_tangent));

    vec3 sheen_color = material.sheen_color_roughness.rgb;
    if (cubey_pbr_has_material_texture(CUBEY_PBR_TEXTURE_SHEEN_COLOR)) {
        sheen_color *= texture(sheen_color_texture,
                               cubey_pbr_transformed_uv(material.sheen_color_transform)).rgb;
    }
    float sheen_roughness = clamp(material.sheen_color_roughness.w, 0.01, 1.0);
    if (cubey_pbr_has_material_texture(CUBEY_PBR_TEXTURE_SHEEN_ROUGHNESS)) {
        sheen_roughness =
            clamp(sheen_roughness *
                      texture(sheen_roughness_texture,
                              cubey_pbr_transformed_uv(material.sheen_roughness_transform)).a,
                  0.01, 1.0);
    }

    vec3 dfg = texture(brdf_lut, vec2(ndotv, roughness)).rgb;
    vec3 energy_compensation = cubey_pbr_energy_compensation(f0, dfg.b);
    float alpha_roughness = roughness * roughness;
    float anisotropy_alpha_roughness =
        mix(alpha_roughness, 1.0, anisotropy_strength * anisotropy_strength);
    float d = anisotropy_strength > 0.0
                  ? cubey_pbr_distribution_ggx_anisotropic(
                        dot(anisotropy_tangent, half_vector),
                        dot(anisotropy_bitangent, half_vector), ndoth,
                        anisotropy_alpha_roughness, alpha_roughness)
                  : cubey_pbr_distribution_ggx(ndoth, roughness);
    float v = cubey_pbr_visibility_smith_ggx_correlated(ndotv, ndotl, roughness);
    vec3 f = cubey_pbr_fresnel_schlick(vdoth, f0);
    vec3 specular = d * v * f * energy_compensation;

    vec3 radiance = scene.light_color_intensity.rgb * scene.light_color_intensity.a;
    float visibility = shadow_visibility(frag_shadow_position, normal, light_direction);
    vec3 emissive =
        texture(emissive_texture, cubey_pbr_transformed_uv(material.emissive_transform)).rgb *
        material.emissive_alpha_cutoff.rgb;
    if (debug_view != CUBEY_PBR_DEBUG_FINAL) {
        out_color = cubey_pbr_debug_output(debug_view, base_color, metallic, roughness,
                                           geometric_normal, normal, occlusion, emissive,
                                           visibility);
        return;
    }
    float clearcoat_ndotv = max(dot(clearcoat_normal, view_direction), 0.0);
    float clearcoat_ndotl = max(dot(clearcoat_normal, light_direction), 0.0);
    float clearcoat_ndoth = max(dot(clearcoat_normal, half_vector), 0.0);
    float clearcoat_fresnel =
        cubey_pbr_fresnel_schlick(clearcoat_ndotv, vec3(0.04)).r * clearcoat_factor;
    float clearcoat_attenuation = 1.0 - clearcoat_fresnel;
    vec3 sheen_direct =
        cubey_pbr_sheen_direct(sheen_color, sheen_roughness, ndotv, ndotl, ndoth);
    vec3 base_direct =
        (cubey_pbr_lambert_diffuse(diffuse_color) + specular + sheen_direct) * clearcoat_attenuation;
    vec3 clearcoat_direct =
        vec3(clearcoat_factor *
             cubey_pbr_clearcoat_direct(clearcoat_ndotv, clearcoat_ndotl, clearcoat_ndoth, vdoth,
                                        clearcoat_roughness));
    vec3 direct = ((base_direct * ndotl) + (clearcoat_direct * clearcoat_ndotl)) * radiance *
                  visibility;

    vec3 irradiance = texture(irradiance_cube, rotate_environment_direction(normal)).rgb;
    vec3 diffuse_ibl = irradiance * diffuse_color;
    vec3 reflection = reflect(-view_direction, normal);
    float max_prefiltered_lod = max(scene.environment_intensity_mip_count.y - 1.0, 0.0);
    vec3 prefiltered = textureLod(prefiltered_cube, rotate_environment_direction(reflection),
                                  roughness * max_prefiltered_lod)
                           .rgb;
    float specular_occlusion =
        cubey_pbr_specular_ao(ndotv, occlusion, roughness) *
        cubey_pbr_horizon_specular_occlusion(reflection, geometric_normal);
    vec3 specular_ibl =
        prefiltered * cubey_pbr_indirect_specular(f0, dfg) * specular_occlusion;
    vec3 sheen_ibl = irradiance * sheen_color * occlusion * 0.25;
    vec3 clearcoat_reflection = reflect(-view_direction, clearcoat_normal);
    vec3 clearcoat_prefiltered =
        textureLod(prefiltered_cube, rotate_environment_direction(clearcoat_reflection),
                   clearcoat_roughness * max_prefiltered_lod)
            .rgb;
    vec3 clearcoat_dfg =
        texture(brdf_lut, vec2(clearcoat_ndotv, clearcoat_roughness)).rgb;
    float clearcoat_specular_occlusion =
        cubey_pbr_specular_ao(clearcoat_ndotv, occlusion, clearcoat_roughness) *
        cubey_pbr_horizon_specular_occlusion(clearcoat_reflection, geometric_normal);
    vec3 clearcoat_ibl =
        clearcoat_factor * clearcoat_prefiltered *
        cubey_pbr_indirect_specular(vec3(0.04), clearcoat_dfg) * clearcoat_specular_occlusion;
    vec3 ambient = (((diffuse_ibl * occlusion) + specular_ibl + sheen_ibl) *
                        clearcoat_attenuation +
                    clearcoat_ibl) *
                   scene.environment_intensity_mip_count.x;
    ambient += scene.ambient_color_intensity.rgb * scene.ambient_color_intensity.a *
               albedo * occlusion;
    vec3 color = ambient + direct + emissive;
    out_color = vec4(color * output_alpha, output_alpha);
}
