#version 450

struct ParticleCube {
    vec4 position_scale;
    vec4 velocity_seed;
    vec4 color;
};

layout(set = 0, binding = 0, std430) readonly buffer ParticleCubeBuffer {
    ParticleCube cubes[];
};

layout(push_constant) uniform DrawParams {
    mat4 view_projection;
} params;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec3 in_normal;

layout(location = 0) out vec3 frag_color;
layout(location = 1) out vec3 frag_normal;

void main() {
    ParticleCube cube = cubes[gl_InstanceIndex];
    vec3 world_position = cube.position_scale.xyz + (in_position * cube.position_scale.w);

    gl_Position = params.view_projection * vec4(world_position, 1.0);
    frag_color = in_color * cube.color.rgb;
    frag_normal = in_normal;
}
