#version 450

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

layout(push_constant) uniform PbrPushConstants {
    mat4 model;
    vec4 base_color_factor;
    vec4 emissive_alpha_cutoff;
    vec4 metallic_roughness_normal_occlusion;
} push_constants;

layout(location = 0) in vec3 frag_world_position;
layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec3 frag_tangent;
layout(location = 3) in vec3 frag_bitangent;
layout(location = 4) in vec2 frag_uv0;
layout(location = 5) in vec4 frag_shadow_position;

layout(location = 0) out vec4 out_color;

const float kPi = 3.14159265359;

float distribution_ggx(vec3 normal, vec3 half_vector, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float ndoth = max(dot(normal, half_vector), 0.0);
    float ndoth2 = ndoth * ndoth;
    float denom = (ndoth2 * (a2 - 1.0)) + 1.0;
    return a2 / max(kPi * denom * denom, 0.00001);
}

float geometry_schlick_ggx(float ndotv, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return ndotv / max((ndotv * (1.0 - k)) + k, 0.00001);
}

float geometry_smith(vec3 normal, vec3 view_direction, vec3 light_direction, float roughness) {
    float ndotv = max(dot(normal, view_direction), 0.0);
    float ndotl = max(dot(normal, light_direction), 0.0);
    return geometry_schlick_ggx(ndotv, roughness) *
           geometry_schlick_ggx(ndotl, roughness);
}

vec3 fresnel_schlick(float cos_theta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

vec3 fresnel_schlick_roughness(float cos_theta, vec3 f0, float roughness) {
    return f0 + (max(vec3(1.0 - roughness), f0) - f0) *
                    pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
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
    vec4 base_color = texture(base_color_texture, frag_uv0) * push_constants.base_color_factor;
    float alpha_cutoff = push_constants.emissive_alpha_cutoff.w;
    if (alpha_cutoff > 0.0 && base_color.a < alpha_cutoff) {
        discard;
    }

    vec4 metallic_roughness_sample = texture(metallic_roughness_texture, frag_uv0);
    float metallic = clamp(push_constants.metallic_roughness_normal_occlusion.x *
                               metallic_roughness_sample.b,
                           0.0, 1.0);
    float roughness = clamp(push_constants.metallic_roughness_normal_occlusion.y *
                                metallic_roughness_sample.g,
                            0.04, 1.0);
    float normal_scale = push_constants.metallic_roughness_normal_occlusion.z;
    float occlusion_strength = push_constants.metallic_roughness_normal_occlusion.w;

    mat3 tbn = mat3(normalize(frag_tangent), normalize(frag_bitangent),
                    normalize(frag_normal));
    vec3 sampled_normal = texture(normal_texture, frag_uv0).xyz * 2.0 - 1.0;
    sampled_normal.xy *= normal_scale;
    vec3 normal = normalize(tbn * sampled_normal);

    vec3 view_direction = normalize(scene.camera_position.xyz - frag_world_position);
    vec3 light_direction = normalize(scene.light_direction.xyz);
    vec3 half_vector = normalize(view_direction + light_direction);
    float ndotl = max(dot(normal, light_direction), 0.0);
    float ndotv = max(dot(normal, view_direction), 0.0);

    vec3 albedo = base_color.rgb;
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    float d = distribution_ggx(normal, half_vector, roughness);
    float g = geometry_smith(normal, view_direction, light_direction, roughness);
    vec3 f = fresnel_schlick(max(dot(half_vector, view_direction), 0.0), f0);
    vec3 specular = (d * g * f) / max(4.0 * ndotv * ndotl, 0.0001);
    vec3 kd = (vec3(1.0) - f) * (1.0 - metallic);

    float occlusion = mix(1.0, texture(occlusion_texture, frag_uv0).r, occlusion_strength);
    vec3 radiance = scene.light_color_intensity.rgb * scene.light_color_intensity.a;
    float visibility = shadow_visibility(frag_shadow_position, normal, light_direction);
    vec3 direct = ((kd * albedo / kPi) + specular) * radiance * ndotl * visibility;

    vec3 ibl_f = fresnel_schlick_roughness(ndotv, f0, roughness);
    vec3 ibl_kd = (vec3(1.0) - ibl_f) * (1.0 - metallic);
    vec3 irradiance = texture(irradiance_cube, normal).rgb;
    vec3 diffuse_ibl = irradiance * albedo;
    vec3 reflection = reflect(-view_direction, normal);
    float max_prefiltered_lod = max(scene.environment_intensity_mip_count.y - 1.0, 0.0);
    vec3 prefiltered = textureLod(prefiltered_cube, reflection,
                                  roughness * max_prefiltered_lod)
                           .rgb;
    vec2 brdf = texture(brdf_lut, vec2(ndotv, roughness)).rg;
    vec3 specular_ibl = prefiltered * ((ibl_f * brdf.x) + brdf.y);
    vec3 ambient = ((ibl_kd * diffuse_ibl) + specular_ibl) *
                   scene.environment_intensity_mip_count.x * occlusion;
    ambient += scene.ambient_color_intensity.rgb * scene.ambient_color_intensity.a *
               albedo * occlusion;
    vec3 emissive = texture(emissive_texture, frag_uv0).rgb *
                    push_constants.emissive_alpha_cutoff.rgb;
    vec3 color = ambient + direct + emissive;
    out_color = vec4(color, base_color.a);
}
