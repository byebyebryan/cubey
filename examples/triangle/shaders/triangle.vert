#version 450

layout(location = 0) out vec3 frag_color;

vec2 positions[3] = vec2[](
    vec2(0.0, -0.62),
    vec2(0.62, 0.48),
    vec2(-0.62, 0.48)
);

vec3 colors[3] = vec3[](
    vec3(0.95, 0.24, 0.18),
    vec3(0.20, 0.72, 0.42),
    vec3(0.20, 0.46, 0.95)
);

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    frag_color = colors[gl_VertexIndex];
}
