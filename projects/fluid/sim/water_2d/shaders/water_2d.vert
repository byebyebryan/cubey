#version 450

layout(location = 0) out vec2 frag_position;

vec2 positions[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2(3.0, -1.0),
    vec2(-1.0, 3.0)
);

void main() {
    const vec2 position = positions[gl_VertexIndex];
    frag_position = position;
    gl_Position = vec4(position, 0.0, 1.0);
}
