#version 450

layout(push_constant) uniform PushConstants {
    mat4 mvp;
} pc;

layout(location = 0) out vec3 frag_color;

const vec3 positions[8] = vec3[](
    vec3(-1.0, -1.0, -1.0),
    vec3( 1.0, -1.0, -1.0),
    vec3( 1.0,  1.0, -1.0),
    vec3(-1.0,  1.0, -1.0),
    vec3(-1.0, -1.0,  1.0),
    vec3( 1.0, -1.0,  1.0),
    vec3( 1.0,  1.0,  1.0),
    vec3(-1.0,  1.0,  1.0)
);

const int indices[36] = int[](
    4, 5, 6, 4, 6, 7,
    1, 0, 3, 1, 3, 2,
    0, 4, 7, 0, 7, 3,
    5, 1, 2, 5, 2, 6,
    3, 7, 6, 3, 6, 2,
    0, 1, 5, 0, 5, 4
);

const vec3 face_colors[6] = vec3[](
    vec3(0.95, 0.25, 0.18),
    vec3(0.18, 0.56, 0.95),
    vec3(0.22, 0.78, 0.42),
    vec3(0.96, 0.76, 0.18),
    vec3(0.65, 0.34, 0.95),
    vec3(0.18, 0.82, 0.82)
);

void main() {
    int vertex_index = indices[gl_VertexIndex];
    int face_index = gl_VertexIndex / 6;

    vec3 position = positions[vertex_index];
    gl_Position = pc.mvp * vec4(position, 1.0);
    frag_color = face_colors[face_index];
}
