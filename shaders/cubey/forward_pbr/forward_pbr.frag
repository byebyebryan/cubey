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
    vec4 backdrop_reflection_radiance_strength;
    vec4 backdrop_reflection_horizon;
} scene;

layout(set = 0, binding = 1) uniform sampler2D shadow_map;
layout(set = 0, binding = 2) uniform samplerCube irradiance_cube;
layout(set = 0, binding = 3) uniform samplerCube prefiltered_cube;
layout(set = 0, binding = 4) uniform sampler2D brdf_lut;
layout(set = 0, binding = 5) uniform samplerCube previous_prefiltered_cube;
layout(set = 0, binding = 6) uniform sampler2D refraction_radiance;
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
layout(set = 1, binding = 16) uniform sampler2D transmission_texture;
layout(set = 1, binding = 17) uniform sampler2D volume_thickness_texture;

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
const uint CUBEY_PBR_TEXTURE_CLEARCOAT = 4u;
const uint CUBEY_PBR_TEXTURE_CLEARCOAT_ROUGHNESS = 8u;
const uint CUBEY_PBR_TEXTURE_CLEARCOAT_NORMAL = 16u;
const uint CUBEY_PBR_TEXTURE_SHEEN_COLOR = 32u;
const uint CUBEY_PBR_TEXTURE_SHEEN_ROUGHNESS = 64u;
const uint CUBEY_PBR_TEXTURE_ANISOTROPY = 128u;
const uint CUBEY_PBR_TEXTURE_IRIDESCENCE = 256u;
const uint CUBEY_PBR_TEXTURE_IRIDESCENCE_THICKNESS = 512u;
const uint CUBEY_PBR_TEXTURE_TRANSMISSION = 1024u;
const uint CUBEY_PBR_TEXTURE_VOLUME_THICKNESS = 2048u;

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
layout(location = 8) in vec3 frag_model_scale;

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

vec3 cubey_pbr_prefiltered_environment(vec3 direction, float lod) {
    vec3 rotated = rotate_environment_direction(direction);
    vec3 previous = textureLod(previous_prefiltered_cube, rotated, lod).rgb;
    vec3 current = textureLod(prefiltered_cube, rotated, lod).rgb;
    vec3 environment = mix(previous, current, clamp(scene.environment_options.y, 0.0, 1.0));
    float horizon = scene.backdrop_reflection_horizon.x;
    float softness = max(scene.backdrop_reflection_horizon.y, 0.001);
    float backdrop_coverage =
        1.0 - smoothstep(horizon - softness, horizon + softness, direction.y);
    float backdrop_strength =
        clamp(scene.backdrop_reflection_radiance_strength.w * backdrop_coverage, 0.0, 1.0);
    return mix(environment, scene.backdrop_reflection_radiance_strength.rgb, backdrop_strength);
}

float cubey_pbr_apply_ior_to_transmission_roughness(float perceptual_roughness,
                                                     float dielectric_ior) {
    // KHR_materials_transmission: keep an authored smooth interface at zero
    // and apply the extension's IOR-dependent microfacet roughness scale.
    return perceptual_roughness * clamp((dielectric_ior * 2.0) - 2.0, 0.0, 1.0);
}

float cubey_pbr_transmission_pyramid_lod(float perceptual_roughness, float dielectric_ior) {
    float transmission_roughness =
        cubey_pbr_apply_ior_to_transmission_roughness(perceptual_roughness, dielectric_ior);
    float max_lod = float(max(textureQueryLevels(refraction_radiance) - 1, 0));
    return transmission_roughness * max_lod;
}

float cubey_pbr_transmission_environment_lod(float perceptual_roughness,
                                             float dielectric_ior) {
    float transmission_roughness =
        cubey_pbr_apply_ior_to_transmission_roughness(perceptual_roughness, dielectric_ior);
    float max_lod = max(scene.environment_intensity_mip_count.y - 1.0, 0.0);
    return transmission_roughness * max_lod;
}

vec3 cubey_pbr_transmission_radiance(vec2 screen_uv, vec3 fallback_direction,
                                     float perceptual_roughness, float dielectric_ior) {
    vec2 extent = vec2(textureSize(refraction_radiance, 0));
    float pyramid_lod = cubey_pbr_transmission_pyramid_lod(perceptual_roughness, dielectric_ior);
    vec3 screen_radiance =
        textureLod(refraction_radiance, clamp(screen_uv, 0.0, 1.0), pyramid_lod).rgb;

    // The pyramid is same-frame, linear HDR radiance. It is deliberately not
    // scaled by environment intensity or display exposure a second time.
    float environment_lod =
        cubey_pbr_transmission_environment_lod(perceptual_roughness, dielectric_ior);
    vec3 fallback_radiance =
        cubey_pbr_prefiltered_environment(fallback_direction, environment_lod) *
        scene.environment_intensity_mip_count.x;
    vec2 edge_width = max(vec2(2.0) / max(extent, vec2(1.0)), vec2(0.025));
    vec2 near_lower = smoothstep(vec2(0.0), edge_width, screen_uv);
    vec2 near_upper = 1.0 - smoothstep(vec2(1.0) - edge_width, vec2(1.0), screen_uv);
    float screen_weight = near_lower.x * near_lower.y * near_upper.x * near_upper.y;
    return mix(fallback_radiance, screen_radiance, screen_weight);
}

vec3 cubey_pbr_volume_transmission_ray(vec3 normal, vec3 view_direction, float thickness,
                                       float dielectric_ior, vec3 model_scale) {
    float interface_ior = max(dielectric_ior, 1.0);
    vec3 refraction_direction = refract(-view_direction, normalize(normal),
                                        1.0 / interface_ior);
    float direction_length_squared = dot(refraction_direction, refraction_direction);
    if (direction_length_squared <= 1.0e-10) {
        return vec3(0.0);
    }
    // glTF thickness is in mesh space. Convert its refracted offset using the
    // model's basis magnitudes before measuring attenuation in world space.
    return normalize(refraction_direction) * thickness * max(model_scale, vec3(0.0));
}

vec2 cubey_pbr_project_refraction_exit(vec3 world_position) {
    vec4 clip_position = scene.view_projection * vec4(world_position, 1.0);
    if (clip_position.w <= 1.0e-6) {
        return vec2(-1.0);
    }
    return (clip_position.xy / clip_position.w) * 0.5 + 0.5;
}

vec3 cubey_pbr_apply_volume_attenuation(vec3 radiance, float world_ray_length,
                                        vec3 attenuation_color, float attenuation_distance) {
    if (world_ray_length <= 0.0 || attenuation_distance <= 0.0) {
        return radiance;
    }
    // Beer-Lambert, expressed in the glTF extension's attenuation-color form.
    vec3 transmittance = pow(clamp(attenuation_color, 0.0, 1.0),
                             vec3(world_ray_length / attenuation_distance));
    return radiance * transmittance;
}

vec3 cubey_pbr_volume_dispersion_radiance(vec3 normal, vec3 view_direction, float thickness,
                                          float dielectric_ior, float perceptual_roughness,
                                          float dispersion, vec3 model_scale,
                                          vec3 world_position) {
    // KHR_materials_dispersion's stable approximation spreads the base IOR
    // symmetrically around green. Each channel gets its own exit projection
    // and same-frame/environment lookup; attenuation still uses green's ray
    // length in the caller so Beer absorption remains wavelength-independent.
    float base_ior = max(dielectric_ior, 1.0);
    float half_spread = max(base_ior - 1.0, 0.0) * 0.025 * dispersion;
    vec3 channel_iors = vec3(max(1.0, base_ior - half_spread), base_ior,
                             base_ior + half_spread);
    vec3 transmitted_radiance = vec3(0.0);
    for (int channel = 0; channel < 3; ++channel) {
        float channel_ior = channel_iors[channel];
        vec3 channel_ray = cubey_pbr_volume_transmission_ray(
            normal, view_direction, thickness, channel_ior, model_scale);
        float channel_ray_length = length(channel_ray);
        vec2 channel_screen_uv =
            cubey_pbr_project_refraction_exit(world_position + channel_ray);
        vec3 channel_fallback_direction = channel_ray_length > 0.0
                                              ? channel_ray / channel_ray_length
                                              : -view_direction;
        vec3 channel_radiance = cubey_pbr_transmission_radiance(
            channel_screen_uv, channel_fallback_direction, perceptual_roughness, channel_ior);
        transmitted_radiance[channel] = channel_radiance[channel];
    }
    return transmitted_radiance;
}

vec3 cubey_pbr_evaluate_diffuse_irradiance_sh(vec3 direction) {
    vec3 normal = normalize(direction);
    float x = normal.x;
    float y = normal.y;
    float z = normal.z;
    float basis[9] = float[](
        0.282095,
        0.488603 * y,
        0.488603 * z,
        0.488603 * x,
        1.092548 * x * y,
        1.092548 * y * z,
        0.315392 * ((3.0 * z * z) - 1.0),
        1.092548 * x * z,
        0.546274 * ((x * x) - (y * y))
    );
    vec3 irradiance = vec3(0.0);
    for (int index = 0; index < 9; ++index) {
        irradiance += scene.diffuse_irradiance_sh[index].rgb * basis[index];
    }
    return max(irradiance, vec3(0.0));
}

vec3 cubey_pbr_diffuse_irradiance(vec3 normal) {
    if (scene.environment_options.x > 0.5) {
        return cubey_pbr_evaluate_diffuse_irradiance_sh(normal);
    }
    return texture(irradiance_cube, rotate_environment_direction(normal)).rgb;
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
    float perceptual_roughness = clamp(material.metallic_roughness_normal_occlusion.y *
                                           metallic_roughness_sample.g,
                                       0.0, 1.0);
    float roughness = max(perceptual_roughness, 0.04);
    float transmission = clamp(material.transmission_factor.x, 0.0, 1.0);
    if (cubey_pbr_has_material_texture(CUBEY_PBR_TEXTURE_TRANSMISSION)) {
        transmission *= texture(transmission_texture,
                                cubey_pbr_transformed_uv(material.transmission_transform)).r;
    }
    transmission *= 1.0 - metallic;
    float volume_thickness = max(material.volume_thickness_attenuation_distance.x, 0.0);
    if (cubey_pbr_has_material_texture(CUBEY_PBR_TEXTURE_VOLUME_THICKNESS)) {
        volume_thickness *= texture(volume_thickness_texture,
                                    cubey_pbr_transformed_uv(material.volume_thickness_transform)).g;
    }
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
    vec3 clearcoat_normal = geometric_normal;
    float clearcoat_roughness = 0.04;
    if (clearcoat_factor > 0.0) {
        clearcoat_roughness = material.clearcoat_factor_roughness_normal.y;
        if (cubey_pbr_has_material_texture(CUBEY_PBR_TEXTURE_CLEARCOAT_ROUGHNESS)) {
            clearcoat_roughness *=
                texture(clearcoat_roughness_texture,
                        cubey_pbr_transformed_uv(material.clearcoat_roughness_transform)).g;
        }
        clearcoat_roughness = clamp(clearcoat_roughness, 0.04, 1.0);
        if (cubey_pbr_has_material_texture(CUBEY_PBR_TEXTURE_CLEARCOAT_NORMAL)) {
            vec3 sampled_clearcoat_normal =
                texture(clearcoat_normal_texture,
                        cubey_pbr_transformed_uv(material.clearcoat_normal_transform)).xyz *
                    2.0 -
                1.0;
            sampled_clearcoat_normal.xy *= material.clearcoat_factor_roughness_normal.z;
            clearcoat_normal = normalize(tbn * sampled_clearcoat_normal);
        }
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
    vec3 f90 = mix(vec3(cubey_pbr_saturate(specular_strength)), vec3(1.0), metallic);
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
    if (iridescence_thickness <= 0.0) {
        // The thin-film model becomes the ordinary interface at zero thickness.
        // Keep the full existing base path exactly neutral in that case.
        iridescence_factor = 0.0;
    }
    vec3 iridescence_fresnel_dielectric = cubey_pbr_iridescence_fresnel(
        1.0, material.iridescence_ior_thickness.x, ndotv, iridescence_thickness, dielectric_f0);
    vec3 iridescence_fresnel_metallic = cubey_pbr_iridescence_fresnel(
        1.0, material.iridescence_ior_thickness.x, ndotv, iridescence_thickness, albedo);

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
    float sheen_roughness = clamp(material.sheen_color_roughness.w, 0.0, 1.0);
    if (cubey_pbr_has_material_texture(CUBEY_PBR_TEXTURE_SHEEN_ROUGHNESS)) {
        sheen_roughness =
            clamp(sheen_roughness *
                      texture(sheen_roughness_texture,
                              cubey_pbr_transformed_uv(material.sheen_roughness_transform)).a,
                  0.0, 1.0);
    }
    float sheen_color_max = max(max(sheen_color.r, sheen_color.g), sheen_color.b);
    float sheen_view_attenuation = 1.0;
    float sheen_view_energy = 0.0;
    if (sheen_color_max > 0.0) {
        sheen_view_energy = texture(brdf_lut, vec2(ndotv, sheen_roughness)).a;
        sheen_view_attenuation = clamp(1.0 - (sheen_color_max * sheen_view_energy), 0.0, 1.0);
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
    float v = anisotropy_strength > 0.0
                  ? cubey_pbr_visibility_smith_ggx_correlated_anisotropic(
                        ndotv, ndotl, dot(anisotropy_tangent, view_direction),
                        dot(anisotropy_bitangent, view_direction),
                        dot(anisotropy_tangent, light_direction),
                        dot(anisotropy_bitangent, light_direction),
                        anisotropy_alpha_roughness, alpha_roughness)
                  : cubey_pbr_visibility_smith_ggx_correlated(ndotv, ndotl, roughness);
    vec3 ordinary_fresnel = cubey_pbr_fresnel_schlick(vdoth, f0, f90);
    vec3 view_facing_fresnel = cubey_pbr_fresnel_schlick(ndotv, f0, f90);
    vec3 thin_film_fresnel = mix(iridescence_fresnel_dielectric,
                                  iridescence_fresnel_metallic, metallic);
    vec3 f = mix(ordinary_fresnel, thin_film_fresnel, iridescence_factor);
    vec3 view_interface_fresnel = mix(view_facing_fresnel, thin_film_fresnel, iridescence_factor);
    vec3 specular = d * v * f * energy_compensation;
    vec3 diffuse_direct = cubey_pbr_lambert_diffuse(diffuse_color) *
                          (1.0 - max(max(f.r, f.g), f.b));

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
    float clearcoat_layer_weight =
        cubey_pbr_clearcoat_layer_weight(clearcoat_factor, clearcoat_ndotv);
    float clearcoat_attenuation = 1.0 - clearcoat_layer_weight;
    vec3 base_direct = diffuse_direct + specular;
    float sheen_direct_attenuation = 1.0;
    if (sheen_color_max > 0.0) {
        float sheen_light_energy = texture(brdf_lut, vec2(ndotl, sheen_roughness)).a;
        sheen_direct_attenuation = min(
            sheen_view_attenuation, clamp(1.0 - (sheen_color_max * sheen_light_energy), 0.0, 1.0));
        vec3 sheen_direct =
            cubey_pbr_sheen_direct(sheen_color, sheen_roughness, ndotv, ndotl, ndoth);
        base_direct = sheen_direct + (base_direct * sheen_direct_attenuation);
    }
    base_direct *= clearcoat_attenuation;
    vec3 clearcoat_direct =
        vec3(clearcoat_layer_weight *
             cubey_pbr_clearcoat_direct(clearcoat_ndotv, clearcoat_ndotl, clearcoat_ndoth,
                                        clearcoat_roughness));
    vec3 direct = ((base_direct * ndotl) + (clearcoat_direct * clearcoat_ndotl)) * radiance *
                  visibility;
    vec3 diffuse_direct_contribution =
        diffuse_direct * sheen_direct_attenuation * clearcoat_attenuation * ndotl * radiance *
        visibility;

    vec3 irradiance = cubey_pbr_diffuse_irradiance(normal);
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
    vec3 prefiltered =
        cubey_pbr_prefiltered_environment(reflection, roughness * max_prefiltered_lod);
    float specular_occlusion =
        cubey_pbr_specular_ao(ndotv, occlusion, roughness) *
        cubey_pbr_horizon_specular_occlusion(reflection, geometric_normal);
    vec3 specular_ibl =
        prefiltered * cubey_pbr_indirect_specular(f0, f90, dfg) * specular_occlusion;
    vec3 base_ibl = (diffuse_ibl * occlusion) + specular_ibl;
    vec3 base_diffuse_ibl = diffuse_ibl * occlusion;
    if (iridescence_factor > 0.0) {
        // Follow the Khronos glTF Sample Renderer composition: the ordinary
        // base response is blended with a thin-film response that performs one
        // RGB diffuse/specular Fresnel mix per dielectric or metallic base.
        vec3 diffuse_ibl_base = irradiance * diffuse_color * occlusion;
        vec3 dielectric_thin_film_ibl = mix(
            diffuse_ibl_base, prefiltered * specular_occlusion,
            iridescence_fresnel_dielectric);
        vec3 metallic_thin_film_ibl =
            prefiltered * iridescence_fresnel_metallic * specular_occlusion;
        vec3 thin_film_base_ibl =
            mix(dielectric_thin_film_ibl, metallic_thin_film_ibl, metallic);
        base_ibl = mix(base_ibl, thin_film_base_ibl, iridescence_factor);
        vec3 thin_film_diffuse_ibl =
            diffuse_ibl_base * (vec3(1.0) - iridescence_fresnel_dielectric) * (1.0 - metallic);
        base_diffuse_ibl = mix(base_diffuse_ibl, thin_film_diffuse_ibl, iridescence_factor);
    }
    vec3 sheen_ibl = vec3(0.0);
    if (sheen_color_max > 0.0) {
        vec3 sheen_reflection = reflect(-view_direction, normal);
        vec3 sheen_prefiltered = cubey_pbr_prefiltered_environment(
            sheen_reflection, sheen_roughness * max_prefiltered_lod);
        float sheen_occlusion =
            cubey_pbr_specular_ao(ndotv, occlusion, sheen_roughness) *
            cubey_pbr_horizon_specular_occlusion(sheen_reflection, geometric_normal);
        sheen_ibl = sheen_prefiltered * sheen_color * sheen_view_energy * sheen_occlusion;
        base_ibl *= sheen_view_attenuation;
    }
    vec3 clearcoat_reflection = reflect(-view_direction, clearcoat_normal);
    vec3 clearcoat_prefiltered = cubey_pbr_prefiltered_environment(
        clearcoat_reflection, clearcoat_roughness * max_prefiltered_lod);
    vec3 clearcoat_dfg =
        texture(brdf_lut, vec2(clearcoat_ndotv, clearcoat_roughness)).rgb;
    float clearcoat_specular_occlusion =
        cubey_pbr_specular_ao(clearcoat_ndotv, occlusion, clearcoat_roughness) *
        cubey_pbr_horizon_specular_occlusion(clearcoat_reflection, geometric_normal);
    vec3 clearcoat_ibl =
        clearcoat_layer_weight * clearcoat_prefiltered *
        cubey_pbr_clearcoat_indirect(clearcoat_dfg) *
        clearcoat_specular_occlusion;
    vec3 ambient = ((base_ibl + sheen_ibl) * clearcoat_attenuation +
                    clearcoat_ibl) *
                   scene.environment_intensity_mip_count.x;
    vec3 legacy_ambient_diffuse = scene.ambient_color_intensity.rgb *
                                  scene.ambient_color_intensity.a * diffuse_color *
                                  diffuse_ibl_attenuation * sheen_view_attenuation *
                                  clearcoat_attenuation * occlusion;
    ambient += legacy_ambient_diffuse;
    vec3 color = ambient + direct + (emissive * clearcoat_attenuation);
    if (transmission > 0.0) {
        vec3 interface_transmittance = vec3(1.0) - view_interface_fresnel;
        vec2 refraction_screen_uv = gl_FragCoord.xy /
                                    max(vec2(textureSize(refraction_radiance, 0)), vec2(1.0));
        vec3 refraction_fallback_direction = -view_direction;
        float volume_ray_length = 0.0;
        vec3 transmitted_radiance = vec3(0.0);
        if (volume_thickness > 0.0) {
            vec3 volume_transmission_ray = cubey_pbr_volume_transmission_ray(
                normal, view_direction, volume_thickness, material.material_model.x,
                frag_model_scale);
            volume_ray_length = length(volume_transmission_ray);
            if (volume_ray_length > 0.0) {
                refraction_screen_uv = cubey_pbr_project_refraction_exit(
                    frag_world_position + volume_transmission_ray);
                // The thick path looks through the refracted direction when
                // the projected exit has no same-frame screen radiance.
                refraction_fallback_direction = volume_transmission_ray / volume_ray_length;
            }
            if (material.transmission_factor.y > 0.0) {
                // Dispersion is deliberately confined to the thick-volume
                // branch. With zero dispersion this remains the exact
                // one-sample volume path above.
                transmitted_radiance = cubey_pbr_volume_dispersion_radiance(
                    normal, view_direction, volume_thickness, material.material_model.x,
                    perceptual_roughness, material.transmission_factor.y, frag_model_scale,
                    frag_world_position);
            }
        }
        if (volume_thickness <= 0.0 || material.transmission_factor.y <= 0.0) {
            transmitted_radiance = cubey_pbr_transmission_radiance(
                refraction_screen_uv, refraction_fallback_direction, perceptual_roughness,
                material.material_model.x);
        }
        transmitted_radiance = cubey_pbr_apply_volume_attenuation(
            transmitted_radiance, volume_ray_length, material.volume_attenuation_color.rgb,
            material.volume_thickness_attenuation_distance.y);
        vec3 transmission_layer = transmitted_radiance * base_color.rgb * transmission *
                                  interface_transmittance * sheen_view_attenuation *
                                  clearcoat_attenuation;
        vec3 replaced_diffuse = diffuse_direct_contribution +
                                (base_diffuse_ibl * sheen_view_attenuation *
                                 clearcoat_attenuation *
                                 scene.environment_intensity_mip_count.x) +
                                legacy_ambient_diffuse;
        // Preserve reflected dielectric/metallic specular, sheen, clearcoat,
        // iridescence, emissive, AO, debug, and premultiplied output paths.
        color += transmission_layer - (transmission * replaced_diffuse);
    }
    out_color = vec4(color * output_alpha, output_alpha);
}
