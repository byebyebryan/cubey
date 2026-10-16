#version 450

layout(set = 0, binding = 0) uniform PlanetOrbitUniforms {
    mat4 view_projection;
    vec4 camera_position;
    vec4 sun_direction_intensity;
    vec4 surface_options;
} frame;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec3 in_normal;
layout(location = 3) in vec2 in_uv;

layout(location = 0) out vec3 frag_position;
layout(location = 1) out vec3 frag_normal;

void main() {
    frag_position = in_position;
    frag_normal = normalize(in_normal);
    gl_Position = frame.view_projection * vec4(in_position, 1.0);
}
