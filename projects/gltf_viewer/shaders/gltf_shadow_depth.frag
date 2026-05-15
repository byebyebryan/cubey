#version 450

layout(set = 1, binding = 0) uniform sampler2D base_color_texture;

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
    PbrTextureTransform base_color_transform;
    PbrTextureTransform metallic_roughness_transform;
    PbrTextureTransform normal_transform;
    PbrTextureTransform occlusion_transform;
    PbrTextureTransform emissive_transform;
    PbrTextureTransform specular_transform;
    PbrTextureTransform specular_color_transform;
} material;

layout(location = 0) in vec2 frag_uv0;
layout(location = 1) in vec2 frag_uv1;
layout(location = 2) in vec4 frag_color0;

vec2 cubey_pbr_transformed_uv(PbrTextureTransform transform) {
    vec2 uv = transform.rotation_texcoord.z > 0.5 ? frag_uv1 : frag_uv0;
    vec2 scaled = uv * transform.offset_scale.zw;
    float c = transform.rotation_texcoord.x;
    float s = transform.rotation_texcoord.y;
    vec2 rotated = vec2((c * scaled.x) - (s * scaled.y),
                        (s * scaled.x) + (c * scaled.y));
    return rotated + transform.offset_scale.xy;
}

void main() {
    vec4 base_color =
        texture(base_color_texture, cubey_pbr_transformed_uv(material.base_color_transform)) *
        material.base_color_factor;
    base_color *= frag_color0;
    float alpha_cutoff = material.emissive_alpha_cutoff.w;
    if (alpha_cutoff > 0.0 && base_color.a < alpha_cutoff) {
        discard;
    }
}
