#version 450

layout(set = 1, binding = 0) uniform sampler2D base_color_texture;

layout(set = 1, binding = 5) uniform PbrMaterialUniforms {
    vec4 base_color_factor;
    vec4 emissive_alpha_cutoff;
    vec4 metallic_roughness_normal_occlusion;
    vec4 specular_color_factor;
    vec4 material_model;
} material;

layout(location = 0) in vec2 frag_uv0;

void main() {
    vec4 base_color = texture(base_color_texture, frag_uv0) * material.base_color_factor;
    float alpha_cutoff = material.emissive_alpha_cutoff.w;
    if (alpha_cutoff > 0.0 && base_color.a < alpha_cutoff) {
        discard;
    }
}
