#version 450

layout(push_constant) uniform TerrainShadowPushConstants {
    mat4 light_view_projection;
} pc;

layout(location = 0) in vec3 in_position;

void main() {
    gl_Position = pc.light_view_projection * vec4(in_position, 1.0);
}
