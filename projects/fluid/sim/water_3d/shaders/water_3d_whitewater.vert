#version 450
#extension GL_GOOGLE_include_directive : require

#include "water_3d_surface_common.glsl"

WATER3D_SURFACE_PARAMS;

#include "water_3d_surface_helpers.glsl"

layout(set = 0, binding = WATER3D_BINDING_WHITEWATER_POSITIONS, std430) readonly buffer WhitewaterPositions {
    vec4 values[];
} whitewater_positions;
layout(set = 0, binding = WATER3D_BINDING_WHITEWATER_VELOCITIES, std430) readonly buffer WhitewaterVelocities {
    vec4 values[];
} whitewater_velocities;
layout(set = 0, binding = WATER3D_BINDING_WHITEWATER_STATE, std430) readonly buffer WhitewaterState {
    vec4 values[];
} whitewater_state;
layout(set = 0, binding = WATER3D_BINDING_WHITEWATER_ACTIVE_INDICES, std430) readonly buffer WhitewaterActiveIndices {
    uint values[];
} whitewater_active_indices;

layout(location = 0) out vec2 frag_local;
layout(location = 1) out float frag_kind;
layout(location = 2) out float frag_age;
layout(location = 3) out float frag_energy;
layout(location = 4) out float frag_linear_depth;
layout(location = 5) out float frag_radius_px;
layout(location = 6) out float frag_seed;

float particle_seed(uint value) {
    value ^= value >> 16u;
    value *= 2246822519u;
    value ^= value >> 13u;
    value *= 3266489917u;
    value ^= value >> 16u;
    return float(value & 65535u) / 65535.0;
}

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
    uint active_slot = uint(gl_InstanceIndex);
    uint particle_count = water_surface_particle_count();
    if (active_slot >= particle_count) {
        frag_local = vec2(2.0);
        frag_kind = 0.0;
        frag_age = 1.0;
        frag_energy = 0.0;
        frag_linear_depth = WATER3D_SURFACE_DEPTH_SENTINEL;
        frag_radius_px = 1.0;
        frag_seed = 0.0;
        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
        return;
    }
    uint particle_id = whitewater_active_indices.values[active_slot];
    if (particle_id >= particle_count || whitewater_positions.values[particle_id].w < 0.5) {
        frag_local = vec2(2.0);
        frag_kind = 0.0;
        frag_age = 1.0;
        frag_energy = 0.0;
        frag_linear_depth = WATER3D_SURFACE_DEPTH_SENTINEL;
        frag_radius_px = 1.0;
        frag_seed = 0.0;
        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
        return;
    }

    vec3 center = water_surface_sim_to_world(whitewater_positions.values[particle_id].xyz);
    vec4 velocity_age = whitewater_velocities.values[particle_id];
    vec4 state = whitewater_state.values[particle_id];
    float kind = state.y;
    float age = clamp(velocity_age.w / max(0.05, state.x), 0.0, 1.0);
    float radius_scale = max(0.2, state.z) * mix(0.95, 0.72, step(0.5, kind));
    float radius =
        water_surface_screen_limited_radius(center, water_surface_particle_radius() * radius_scale);
    vec3 world_position = center + ((water_surface_camera_right() * corner.x) +
                                    (water_surface_camera_up() * corner.y)) *
                                       radius;
    frag_local = corner;
    frag_kind = kind;
    frag_age = age;
    frag_energy = state.w;
    frag_linear_depth = length(center - water_surface_camera_position());
    frag_radius_px = water_surface_projected_radius_px(radius, frag_linear_depth);
    frag_seed = particle_seed(particle_id);
    gl_Position = surface_params.view_projection * vec4(world_position, 1.0);
}
