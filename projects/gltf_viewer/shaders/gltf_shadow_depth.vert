#version 450

layout(push_constant) uniform ShadowPushConstants {
    mat4 light_mvp;
} push_constants;

layout(location = 0) in vec3 in_position;
layout(location = 3) in vec2 in_uv0;
layout(location = 4) in vec2 in_uv1;
layout(location = 5) in vec4 in_color0;

layout(location = 0) out vec2 frag_uv0;
layout(location = 1) out vec2 frag_uv1;
layout(location = 2) out vec4 frag_color0;

void main() {
    gl_Position = push_constants.light_mvp * vec4(in_position, 1.0);
    frag_uv0 = in_uv0;
    frag_uv1 = in_uv1;
    frag_color0 = in_color0;
}
