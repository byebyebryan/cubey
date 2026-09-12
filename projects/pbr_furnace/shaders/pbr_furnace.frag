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
    vec4 diffuse_irradiance_sh[9];
    vec4 environment_options;
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
    vec4 transmission_factor;
    vec4 volume_thickness_attenuation_distance;
    vec4 volume_attenuation_color;
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
    PbrTextureTransform transmission_transform;
    PbrTextureTransform volume_thickness_transform;
} material;

const uint CUBEY_PBR_TEXTURE_SPECULAR = 1u;
const uint CUBEY_PBR_TEXTURE_SPECULAR_COLOR = 2u;

bool cubey_pbr_has_material_texture(uint flag) {
    return (uint(material.material_model.w + 0.5) & flag) != 0u;
}

layout(location = 0) in vec3 frag_world_position;
layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec3 frag_tangent;
layout(location = 3) in vec3 frag_bitangent;
layout(location = 4) in vec2 frag_uv0;

layout(location = 0) out vec4 out_color;

void main() {
    vec4 base_color = texture(base_color_texture, frag_uv0) * material.base_color_factor;
    vec4 metallic_roughness_sample = texture(metallic_roughness_texture, frag_uv0);
    float metallic = clamp(material.metallic_roughness_normal_occlusion.x *
                               metallic_roughness_sample.b,
                           0.0, 1.0);
    float roughness = clamp(material.metallic_roughness_normal_occlusion.y *
                                metallic_roughness_sample.g,
                            0.04, 1.0);
    float transmission = clamp(material.transmission_factor.x, 0.0, 1.0) * (1.0 - metallic);
    float volume_thickness = max(material.volume_thickness_attenuation_distance.x, 0.0);
    float normal_scale = material.metallic_roughness_normal_occlusion.z;
    float occlusion_strength = material.metallic_roughness_normal_occlusion.w;

    vec3 geometric_normal = normalize(frag_normal);
    vec3 tangent = normalize(frag_tangent);
    vec3 bitangent = normalize(frag_bitangent);
    mat3 tbn = mat3(tangent, bitangent, geometric_normal);
    vec3 sampled_normal = texture(normal_texture, frag_uv0).xyz * 2.0 - 1.0;
    sampled_normal.xy *= normal_scale;
    vec3 normal = normalize(tbn * sampled_normal);

    vec3 view_direction = normalize(scene.camera_position.xyz - frag_world_position);
    float ndotv = max(dot(normal, view_direction), 0.0);
    vec3 albedo = base_color.rgb;
    vec3 diffuse_color = cubey_pbr_diffuse_color(albedo, metallic);
    float specular_strength = material.specular_color_factor.a;
    if (cubey_pbr_has_material_texture(CUBEY_PBR_TEXTURE_SPECULAR)) {
        specular_strength *= texture(specular_texture, frag_uv0).a;
    }
    vec3 specular_color_factor = material.specular_color_factor.rgb;
    if (cubey_pbr_has_material_texture(CUBEY_PBR_TEXTURE_SPECULAR_COLOR)) {
        specular_color_factor *= texture(specular_color_texture, frag_uv0).rgb;
    }
    vec3 dielectric_f0 = cubey_pbr_dielectric_f0(
        specular_color_factor, specular_strength, material.material_model.x);
    vec3 f0 = cubey_pbr_f0(albedo, metallic, dielectric_f0);
    vec3 f90 = mix(vec3(cubey_pbr_saturate(specular_strength)), vec3(1.0), metallic);
    float iridescence_factor = clamp(material.anisotropy_iridescence.w, 0.0, 1.0);
    float iridescence_thickness = material.iridescence_ior_thickness.z;
    if (iridescence_thickness <= 0.0) {
        iridescence_factor = 0.0;
    }
    vec3 iridescence_fresnel_dielectric = cubey_pbr_iridescence_fresnel(
        1.0, material.iridescence_ior_thickness.x, ndotv, iridescence_thickness,
        dielectric_f0);
    vec3 iridescence_fresnel_metallic = cubey_pbr_iridescence_fresnel(
        1.0, material.iridescence_ior_thickness.x, ndotv, iridescence_thickness,
        albedo);
    vec3 view_facing_fresnel = cubey_pbr_fresnel_schlick(ndotv, f0, f90);
    vec3 thin_film_fresnel = mix(iridescence_fresnel_dielectric,
                                  iridescence_fresnel_metallic, metallic);
    vec3 view_interface_fresnel = mix(view_facing_fresnel, thin_film_fresnel,
                                      iridescence_factor);
    float clearcoat_factor = clamp(material.clearcoat_factor_roughness_normal.x, 0.0, 1.0);
    float clearcoat_roughness =
        clamp(material.clearcoat_factor_roughness_normal.y, 0.04, 1.0);
    float clearcoat_ndotv = max(dot(geometric_normal, view_direction), 0.0);
    float clearcoat_layer_weight =
        cubey_pbr_clearcoat_layer_weight(clearcoat_factor, clearcoat_ndotv);
    float clearcoat_attenuation = 1.0 - clearcoat_layer_weight;

    float anisotropy_strength = clamp(material.anisotropy_iridescence.x, 0.0, 1.0);
    vec2 anisotropy_direction = normalize(material.anisotropy_iridescence.yz);
    vec3 anisotropy_tangent =
        normalize((tangent * anisotropy_direction.x) + (bitangent * anisotropy_direction.y));
    anisotropy_tangent = normalize(anisotropy_tangent -
                                   (normal * dot(normal, anisotropy_tangent)));
    vec3 anisotropy_bitangent = normalize(cross(normal, anisotropy_tangent));

    vec3 sheen_color = material.sheen_color_roughness.rgb;
    float sheen_roughness = clamp(material.sheen_color_roughness.w, 0.0, 1.0);
    float sheen_color_max = max(max(sheen_color.r, sheen_color.g), sheen_color.b);
    float sheen_view_attenuation = 1.0;
    float sheen_view_energy = 0.0;
    if (sheen_color_max > 0.0) {
        sheen_view_energy = texture(brdf_lut, vec2(ndotv, sheen_roughness)).a;
        sheen_view_attenuation = clamp(1.0 - (sheen_color_max * sheen_view_energy), 0.0, 1.0);
    }

    vec3 irradiance = texture(irradiance_cube, normal).rgb;
    vec3 ibl_fresnel = cubey_pbr_fresnel_schlick(ndotv, f0, f90);
    float diffuse_ibl_attenuation = 1.0 - max(max(ibl_fresnel.r, ibl_fresnel.g), ibl_fresnel.b);
    vec3 diffuse_ibl = irradiance * diffuse_color * diffuse_ibl_attenuation;
    vec3 ibl_normal = anisotropy_strength > 0.0
                          ? cubey_pbr_anisotropic_bent_normal(
                                normal, view_direction, anisotropy_bitangent, roughness,
                                anisotropy_strength)
                          : normal;
    vec3 reflection = reflect(-view_direction, ibl_normal);
    float max_prefiltered_lod = max(scene.environment_intensity_mip_count.y - 1.0, 0.0);
    vec3 prefiltered = textureLod(prefiltered_cube, reflection,
                                  roughness * max_prefiltered_lod)
                           .rgb;
    vec3 dfg = texture(brdf_lut, vec2(ndotv, roughness)).rgb;
    vec3 energy_compensation = cubey_pbr_energy_compensation(f0, dfg.b);
    float occlusion = mix(1.0, texture(occlusion_texture, frag_uv0).r, occlusion_strength);
    float specular_occlusion =
        cubey_pbr_specular_ao(ndotv, occlusion, roughness) *
        cubey_pbr_horizon_specular_occlusion(reflection, geometric_normal);
    vec3 specular_ibl =
        prefiltered * cubey_pbr_indirect_specular(f0, f90, dfg) * specular_occlusion;
    vec3 base_ibl = (diffuse_ibl * occlusion) + specular_ibl;
    if (iridescence_factor > 0.0) {
        vec3 diffuse_ibl_base = irradiance * diffuse_color * occlusion;
        vec3 dielectric_thin_film_ibl = mix(
            diffuse_ibl_base, prefiltered * specular_occlusion,
            iridescence_fresnel_dielectric);
        vec3 metallic_thin_film_ibl =
            prefiltered * iridescence_fresnel_metallic * specular_occlusion;
        vec3 thin_film_base_ibl =
            mix(dielectric_thin_film_ibl, metallic_thin_film_ibl, metallic);
        base_ibl = mix(base_ibl, thin_film_base_ibl, iridescence_factor);
    }
    vec3 sheen_ibl = vec3(0.0);
    if (sheen_color_max > 0.0) {
        vec3 sheen_reflection = reflect(-view_direction, normal);
        vec3 sheen_prefiltered =
            textureLod(prefiltered_cube, sheen_reflection, sheen_roughness * max_prefiltered_lod)
                .rgb;
        float sheen_occlusion =
            cubey_pbr_specular_ao(ndotv, occlusion, sheen_roughness) *
            cubey_pbr_horizon_specular_occlusion(sheen_reflection, geometric_normal);
        sheen_ibl = sheen_prefiltered * sheen_color * sheen_view_energy * sheen_occlusion;
        base_ibl *= sheen_view_attenuation;
    }
    vec3 direct = vec3(0.0);
    vec3 diffuse_direct_contribution = vec3(0.0);
    if (scene.light_color_intensity.a > 0.0) {
        vec3 light_direction = normalize(scene.light_direction.xyz);
        vec3 half_vector = normalize(view_direction + light_direction);
        float ndotl = max(dot(normal, light_direction), 0.0);
        if (ndotl > 0.0) {
            float ndoth = max(dot(normal, half_vector), 0.0);
            float vdoth = max(dot(view_direction, half_vector), 0.0);
            float alpha_roughness = roughness * roughness;
            float anisotropy_alpha_roughness =
                mix(alpha_roughness, 1.0, anisotropy_strength * anisotropy_strength);
            float d = anisotropy_strength > 0.0
                          ? cubey_pbr_distribution_ggx_anisotropic(
                                dot(anisotropy_tangent, half_vector),
                                dot(anisotropy_bitangent, half_vector), ndoth,
                                anisotropy_alpha_roughness, alpha_roughness)
                          : cubey_pbr_distribution_ggx(ndoth, roughness);
            float v = anisotropy_strength > 0.0
                          ? cubey_pbr_visibility_smith_ggx_correlated_anisotropic(
                                ndotv, ndotl, dot(anisotropy_tangent, view_direction),
                                dot(anisotropy_bitangent, view_direction),
                                dot(anisotropy_tangent, light_direction),
                                dot(anisotropy_bitangent, light_direction),
                                anisotropy_alpha_roughness, alpha_roughness)
                          : cubey_pbr_visibility_smith_ggx_correlated(ndotv, ndotl, roughness);
            vec3 ordinary_fresnel = cubey_pbr_fresnel_schlick(vdoth, f0, f90);
            vec3 f = mix(ordinary_fresnel, thin_film_fresnel, iridescence_factor);
            vec3 diffuse_direct = cubey_pbr_lambert_diffuse(diffuse_color) *
                                  (1.0 - max(max(f.r, f.g), f.b));
            vec3 specular_direct = d * v * f * energy_compensation;
            float clearcoat_ndotl = max(dot(geometric_normal, light_direction), 0.0);
            float clearcoat_ndoth = max(dot(geometric_normal, half_vector), 0.0);
            vec3 base_direct = diffuse_direct + specular_direct;
            float sheen_direct_attenuation = 1.0;
            if (sheen_color_max > 0.0) {
                float sheen_light_energy = texture(brdf_lut, vec2(ndotl, sheen_roughness)).a;
                sheen_direct_attenuation =
                    min(sheen_view_attenuation,
                        clamp(1.0 - (sheen_color_max * sheen_light_energy), 0.0, 1.0));
                vec3 sheen_direct =
                    cubey_pbr_sheen_direct(sheen_color, sheen_roughness, ndotv, ndotl, ndoth);
                base_direct = sheen_direct + (base_direct * sheen_direct_attenuation);
            }
            base_direct *= clearcoat_attenuation;
            vec3 clearcoat_direct =
                vec3(clearcoat_layer_weight * cubey_pbr_clearcoat_direct(
                                               clearcoat_ndotv, clearcoat_ndotl,
                                               clearcoat_ndoth, clearcoat_roughness));
            direct = ((base_direct * ndotl) + (clearcoat_direct * clearcoat_ndotl)) *
                     scene.light_color_intensity.rgb * scene.light_color_intensity.a;
            diffuse_direct_contribution = diffuse_direct * sheen_direct_attenuation *
                                          clearcoat_attenuation * ndotl *
                                          scene.light_color_intensity.rgb *
                                          scene.light_color_intensity.a;
        }
    }
    if (transmission > 0.0) {
        vec3 direct_transmitted_radiance = cubey_pbr_direct_transmission_btdf(
            normal, view_direction, scene.light_direction.xyz, roughness, base_color.rgb,
            material.material_model.x);
        direct_transmitted_radiance *=
            scene.light_color_intensity.rgb * scene.light_color_intensity.a;
        // The deterministic furnace specimens are unit-scale spheres viewed
        // face-on, so authored thickness is their representative world path.
        direct_transmitted_radiance = cubey_pbr_apply_volume_attenuation(
            direct_transmitted_radiance, volume_thickness, material.volume_attenuation_color.rgb,
            material.volume_thickness_attenuation_distance.y);
        vec3 direct_transmission_layer = direct_transmitted_radiance * transmission *
                                         (vec3(1.0) - view_interface_fresnel) *
                                         sheen_view_attenuation * clearcoat_attenuation;
        // Match the production replacement contract: backlit BTDF replaces
        // only the transmission-weighted direct diffuse budget.
        direct += direct_transmission_layer - (transmission * diffuse_direct_contribution);
    }
    vec3 emissive = texture(emissive_texture, frag_uv0).rgb *
                    material.emissive_alpha_cutoff.rgb;
    vec3 clearcoat_reflection = reflect(-view_direction, geometric_normal);
    vec3 clearcoat_prefiltered = textureLod(prefiltered_cube, clearcoat_reflection,
                                            clearcoat_roughness * max_prefiltered_lod)
                                    .rgb;
    vec3 clearcoat_dfg = texture(brdf_lut, vec2(clearcoat_ndotv, clearcoat_roughness)).rgb;
    float clearcoat_specular_occlusion =
        cubey_pbr_specular_ao(clearcoat_ndotv, occlusion, clearcoat_roughness) *
        cubey_pbr_horizon_specular_occlusion(clearcoat_reflection, geometric_normal);
    vec3 clearcoat_ibl = clearcoat_layer_weight * clearcoat_prefiltered *
                         cubey_pbr_clearcoat_indirect(clearcoat_dfg) *
                         clearcoat_specular_occlusion;
    vec3 color = (((base_ibl + sheen_ibl) * clearcoat_attenuation) + clearcoat_ibl) *
                     scene.environment_intensity_mip_count.x +
                 direct + (emissive * clearcoat_attenuation);
    out_color = vec4(cubey_pbr_apply_display_transform(color, scene.display_transform),
                     base_color.a);
}
