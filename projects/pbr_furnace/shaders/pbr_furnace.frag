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
    float normal_scale = material.metallic_roughness_normal_occlusion.z;
    float occlusion_strength = material.metallic_roughness_normal_occlusion.w;

    vec3 geometric_normal = normalize(frag_normal);
    mat3 tbn = mat3(normalize(frag_tangent), normalize(frag_bitangent), geometric_normal);
    vec3 sampled_normal = texture(normal_texture, frag_uv0).xyz * 2.0 - 1.0;
    sampled_normal.xy *= normal_scale;
    vec3 normal = normalize(tbn * sampled_normal);

    vec3 view_direction = normalize(scene.camera_position.xyz - frag_world_position);
    float ndotv = max(dot(normal, view_direction), 0.0);
    vec3 albedo = base_color.rgb;
    vec3 diffuse_color = cubey_pbr_diffuse_color(albedo, metallic);
    vec3 f0 = cubey_pbr_f0(albedo, metallic);

    vec3 irradiance = texture(irradiance_cube, normal).rgb;
    vec3 diffuse_ibl = irradiance * diffuse_color;
    vec3 reflection = reflect(-view_direction, normal);
    float max_prefiltered_lod = max(scene.environment_intensity_mip_count.y - 1.0, 0.0);
    vec3 prefiltered = textureLod(prefiltered_cube, reflection,
                                  roughness * max_prefiltered_lod)
                           .rgb;
    vec3 dfg = texture(brdf_lut, vec2(ndotv, roughness)).rgb;
    float occlusion = mix(1.0, texture(occlusion_texture, frag_uv0).r, occlusion_strength);
    float specular_occlusion =
        cubey_pbr_specular_ao(ndotv, occlusion, roughness) *
        cubey_pbr_horizon_specular_occlusion(reflection, geometric_normal);
    vec3 specular_ibl =
        prefiltered * cubey_pbr_indirect_specular(f0, dfg) * specular_occlusion;
    vec3 emissive = texture(emissive_texture, frag_uv0).rgb *
                    material.emissive_alpha_cutoff.rgb;
    vec3 color = (((diffuse_ibl * occlusion) + specular_ibl) *
                  scene.environment_intensity_mip_count.x) +
                 emissive;
    out_color = vec4(color, base_color.a);
}
