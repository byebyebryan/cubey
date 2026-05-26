#version 450
#extension GL_GOOGLE_include_directive : require

#include "water_2d_contract.glsl"

WATER2D_RENDER_PARAMS;

layout(set = 0, binding = WATER2D_BINDING_PARTICLE_POSITIONS, std430) readonly buffer ParticlePositions {
    vec4 values[];
} particle_positions;

layout(location = 0) out vec2 frag_local;

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
    uint particle_id = uint(gl_InstanceIndex);
    uint particle_count = uint(params.particle_options.x + 0.5);
    if (particle_id >= particle_count || particle_positions.values[particle_id].w < 0.5) {
        frag_local = vec2(2.0);
        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
        return;
    }

    vec2 corner = quad_corner(uint(gl_VertexIndex));
    float width = max(1.0, params.grid_debug.x);
    float height = max(1.0, params.grid_debug.y);
    float aspect = width / height;
    float radius = max(0.0005, params.particle_options.z * max(0.05, params.particle_options.w));
    vec2 center = particle_positions.values[particle_id].xy;
    vec2 screen_uv = vec2(center.x + (corner.x * radius / aspect),
                          1.0 - (center.y + (corner.y * radius)));

    frag_local = corner;
    gl_Position = vec4(screen_uv * 2.0 - 1.0, 0.0, 1.0);
}
