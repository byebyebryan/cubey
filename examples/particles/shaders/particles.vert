#version 450

struct Particle {
    vec4 position_radius;
    vec4 velocity_seed;
    vec4 color;
};

layout(set = 0, binding = 0, std430) readonly buffer ParticleBuffer {
    Particle particles[];
};

layout(push_constant) uniform DrawParams {
    vec4 inv_extent_scale_time;
} params;

layout(location = 0) out vec2 frag_uv;
layout(location = 1) out vec4 frag_color;

const vec2 kCorners[6] = vec2[](
    vec2(-1.0, -1.0),
    vec2(1.0, -1.0),
    vec2(1.0, 1.0),
    vec2(-1.0, -1.0),
    vec2(1.0, 1.0),
    vec2(-1.0, 1.0)
);

void main() {
    Particle particle = particles[gl_InstanceIndex];
    vec2 corner = kCorners[gl_VertexIndex];
    float radius = particle.position_radius.w * params.inv_extent_scale_time.z;
    vec2 ndc_radius = vec2(
        radius * params.inv_extent_scale_time.x * 2.0,
        radius * params.inv_extent_scale_time.y * 2.0);
    vec2 position = particle.position_radius.xy + corner * ndc_radius;

    gl_Position = vec4(position, particle.position_radius.z, 1.0);
    frag_uv = corner;
    frag_color = particle.color;
}
