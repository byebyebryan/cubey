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

layout(set = 1, binding = 5) uniform PbrMaterialUniforms {
    vec4 base_color_factor;
    vec4 emissive_alpha_cutoff;
    vec4 metallic_roughness_normal_occlusion;
    vec4 specular_color_factor;
    vec4 material_model;
} material;

layout(location = 0) in vec3 frag_world_position;
layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec3 frag_tangent;
layout(location = 3) in vec3 frag_bitangent;
layout(location = 4) in vec2 frag_uv0;
layout(location = 5) in vec4 frag_shadow_position;

layout(location = 0) out vec4 out_color;

vec3 rotate_environment_direction(vec3 direction) {
    float c = scene.environment_intensity_mip_count.z;
    float s = scene.environment_intensity_mip_count.w;
    return vec3(
        (c * direction.x) + (s * direction.z),
        direction.y,
        (-s * direction.x) + (c * direction.z)
    );
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
    vec4 base_color = texture(base_color_texture, frag_uv0) * material.base_color_factor;
    float alpha_cutoff = material.emissive_alpha_cutoff.w;
    if (alpha_cutoff > 0.0 && base_color.a < alpha_cutoff) {
        discard;
    }

    vec4 metallic_roughness_sample = texture(metallic_roughness_texture, frag_uv0);
    float metallic = clamp(material.metallic_roughness_normal_occlusion.x *
                               metallic_roughness_sample.b,
                           0.0, 1.0);
    float roughness = clamp(material.metallic_roughness_normal_occlusion.y *
                                metallic_roughness_sample.g,
                            0.04, 1.0);
    float normal_scale = material.metallic_roughness_normal_occlusion.z;
    float occlusion_strength = material.metallic_roughness_normal_occlusion.w;

    vec3 geometric_normal = normalize(frag_normal);
    mat3 tbn = mat3(normalize(frag_tangent), normalize(frag_bitangent), geometric_normal);
    vec3 sampled_normal = texture(normal_texture, frag_uv0).xyz * 2.0 - 1.0;
    sampled_normal.xy *= normal_scale;
    vec3 normal = normalize(tbn * sampled_normal);

    vec3 view_direction = normalize(scene.camera_position.xyz - frag_world_position);
    vec3 light_direction = normalize(scene.light_direction.xyz);
    vec3 half_vector = normalize(view_direction + light_direction);
    float ndotl = max(dot(normal, light_direction), 0.0);
    float ndotv = max(dot(normal, view_direction), 0.0);

    vec3 albedo = base_color.rgb;
    vec3 diffuse_color = cubey_pbr_diffuse_color(albedo, metallic);
    vec3 dielectric_f0 = cubey_pbr_dielectric_f0(
        material.specular_color_factor.rgb, material.specular_color_factor.a,
        material.material_model.x);
    vec3 f0 = cubey_pbr_f0(albedo, metallic, dielectric_f0);
    vec3 dfg = texture(brdf_lut, vec2(ndotv, roughness)).rgb;
    vec3 energy_compensation = cubey_pbr_energy_compensation(f0, dfg.b);
    float d = cubey_pbr_distribution_ggx(max(dot(normal, half_vector), 0.0), roughness);
    float v = cubey_pbr_visibility_smith_ggx_correlated(ndotv, ndotl, roughness);
    vec3 f = cubey_pbr_fresnel_schlick(max(dot(half_vector, view_direction), 0.0), f0);
    vec3 specular = d * v * f * energy_compensation;

    float occlusion = mix(1.0, texture(occlusion_texture, frag_uv0).r, occlusion_strength);
    vec3 radiance = scene.light_color_intensity.rgb * scene.light_color_intensity.a;
    float visibility = shadow_visibility(frag_shadow_position, normal, light_direction);
    vec3 direct =
        (cubey_pbr_lambert_diffuse(diffuse_color) + specular) * radiance * ndotl * visibility;

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
    vec3 ambient = ((diffuse_ibl * occlusion) + specular_ibl) *
                   scene.environment_intensity_mip_count.x;
    ambient += scene.ambient_color_intensity.rgb * scene.ambient_color_intensity.a *
               albedo * occlusion;
    vec3 emissive = texture(emissive_texture, frag_uv0).rgb *
                    material.emissive_alpha_cutoff.rgb;
    vec3 color = ambient + direct + emissive;
    out_color = vec4(cubey_pbr_apply_display_transform(color, scene.display_transform),
                     base_color.a);
}
