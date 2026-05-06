#version 450

layout(push_constant) uniform RenderParams {
    vec4 time_extent;
} params;

layout(location = 0) in vec2 frag_position;
layout(location = 0) out vec4 out_color;

void main() {
    vec2 uv = frag_position * 0.5 + 0.5;
    float wave = 0.5 + 0.5 * sin((uv.x * 8.0) + (uv.y * 5.0) + params.time_extent.x);
    vec3 base = mix(vec3(0.02, 0.05, 0.10), vec3(0.10, 0.46, 0.86), wave);
    out_color = vec4(base * smoothstep(0.0, 0.9, uv.y), 1.0);
}
