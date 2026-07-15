#version 450

layout(set = 0, binding = 0) uniform sampler2D source_texture;

layout(push_constant) uniform FilterOptions {
    float radius;
} filter_options;

layout(location = 0) in vec2 frag_position;
layout(location = 0) out vec4 out_color;

void main() {
    vec2 uv = frag_position * 0.5 + 0.5;
    if (filter_options.radius <= 0.0) {
        out_color = texture(source_texture, uv);
        return;
    }

    vec2 texel = filter_options.radius / vec2(max(textureSize(source_texture, 0), ivec2(1)));
    vec4 value = texture(source_texture, uv) * 4.0;
    value += texture(source_texture, uv + vec2(texel.x, 0.0)) * 2.0;
    value += texture(source_texture, uv - vec2(texel.x, 0.0)) * 2.0;
    value += texture(source_texture, uv + vec2(0.0, texel.y)) * 2.0;
    value += texture(source_texture, uv - vec2(0.0, texel.y)) * 2.0;
    value += texture(source_texture, uv + texel);
    value += texture(source_texture, uv - texel);
    value += texture(source_texture, uv + vec2(texel.x, -texel.y));
    value += texture(source_texture, uv + vec2(-texel.x, texel.y));
    out_color = value * (1.0 / 16.0);
}
