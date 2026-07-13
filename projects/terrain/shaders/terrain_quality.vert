#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec3 in_normal;

layout(location = 0) out vec3 control_position;
layout(location = 1) out vec3 control_metadata;
layout(location = 2) out vec3 control_ownership;

void main() {
    control_position = in_position;
    control_metadata = in_color;
    control_ownership = in_normal;
    gl_Position = vec4(in_position, 1.0);
}
