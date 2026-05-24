#version 450
#extension GL_GOOGLE_include_directive : require

#include "water_3d_surface_common.glsl"

WATER3D_SURFACE_PARAMS;

#include "water_3d_surface_helpers.glsl"

layout(set = 0, binding = WATER3D_BINDING_PARTICLE_POSITIONS, std430) readonly buffer ParticlePositions {
    vec4 values[];
} particle_positions;

layout(location = 0) out vec2 frag_local;
layout(location = 1) out vec3 frag_center;
layout(location = 2) out float frag_radius;

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
    uint particle_id = uint(gl_InstanceIndex);
    uint particle_count = water_surface_particle_count();
    if (particle_id >= particle_count || particle_positions.values[particle_id].w < 0.5) {
        frag_local = vec2(2.0);
        frag_center = vec3(0.0);
        frag_radius = 0.0;
        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
        return;
    }

    float particle_state = particle_positions.values[particle_id].w;
    float radius_scale = particle_state >= WATER3D_PARTICLE_STATE_RAIN - 0.5
                             ? WATER3D_RAIN_RENDER_RADIUS_SCALE
                             : 1.0;
    float radius = water_surface_particle_radius() * radius_scale;
    vec3 center = water_surface_sim_to_world(particle_positions.values[particle_id].xyz);
    vec3 world_position = center + ((water_surface_camera_right() * corner.x) +
                                    (water_surface_camera_up() * corner.y)) *
                                       radius;
    frag_local = corner;
    frag_center = center;
    frag_radius = radius;
    gl_Position = surface_params.view_projection * vec4(world_position, 1.0);
}
