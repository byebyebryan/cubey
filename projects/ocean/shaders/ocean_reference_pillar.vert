#version 450

layout(push_constant) uniform ReferencePillarParams {
    mat4 view_projection;
    vec4 light_direction;
} pillar;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec3 in_normal;

layout(location = 0) out vec3 frag_color;
layout(location = 1) out vec3 frag_normal;

void main() {
    gl_Position = pillar.view_projection * vec4(in_position, 1.0);
    frag_color = in_color;
    frag_normal = normalize(in_normal);
}
