#version 450
#extension GL_GOOGLE_include_directive : require

#include "water_3d_contract.glsl"

WATER3D_RENDER_PARAMS;

layout(set = 0, binding = WATER3D_BINDING_PARTICLE_POSITIONS, std430) readonly buffer ParticlePositions {
    vec4 values[];
} particle_positions;

layout(location = 0) out vec2 frag_uv;
layout(location = 1) out vec2 frag_local;
layout(location = 2) out float frag_particle;

vec2 quad_corner(uint index) {
    vec2 corners[6] = vec2[](
        vec2(-1.0, -1.0),
        vec2(1.0, -1.0),
        vec2(1.0, 1.0),
        vec2(-1.0, -1.0),
        vec2(1.0, 1.0),
        vec2(-1.0, 1.0)
    );
    return corners[index % 6u];
}

void main() {
    vec2 corner = quad_corner(uint(gl_VertexIndex));
    uint render_view = uint(params.camera_up_debug.w + 0.5);
    frag_local = corner;
    frag_uv = corner * 0.5 + 0.5;
    frag_particle = render_view == 1u ? 1.0 : 0.0;

    if (render_view != 1u) {
        gl_Position = vec4(corner, 0.0, 1.0);
        return;
    }

    uint particle_id = uint(gl_InstanceIndex);
    uint particle_count = uint(params.color_options.y + 0.5);
    if (particle_id >= particle_count || particle_positions.values[particle_id].w < 0.5) {
        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
        return;
    }

    vec3 center = particle_positions.values[particle_id].xyz;
    float radius = params.camera_right_radius.w;
    vec3 right = params.camera_right_radius.xyz;
    vec3 up = params.camera_up_debug.xyz;
    vec3 world_position = center + ((right * corner.x) + (up * corner.y)) * radius;
    gl_Position = params.view_projection * vec4(world_position, 1.0);
}
