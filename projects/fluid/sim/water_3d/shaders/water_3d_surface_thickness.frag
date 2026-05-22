#version 450
#extension GL_GOOGLE_include_directive : require

#include "water_3d_surface_common.glsl"

WATER3D_SURFACE_PARAMS;

#include "water_3d_surface_helpers.glsl"

layout(set = 1, binding = 0) uniform sampler2D surface_depth;

layout(location = 0) in vec2 frag_local;
layout(location = 1) in vec3 frag_center;
layout(location = 0) out float out_thickness;

void main() {
    float radius_squared = dot(frag_local, frag_local);
    if (radius_squared > 1.0) {
        discard;
    }

    vec2 texel_size = 1.0 / vec2(textureSize(surface_depth, 0));
    float raw_depth = texture(surface_depth, gl_FragCoord.xy * texel_size).r;
    if (!water_surface_has_depth(raw_depth)) {
        discard;
    }

    float radius = water_surface_particle_radius();
    float sphere_z = sqrt(max(0.0, 1.0 - radius_squared)) * radius;
    vec3 back_world = frag_center + water_surface_camera_forward() * sphere_z;
    float back_depth = length(back_world - water_surface_camera_position());
    if (back_depth < raw_depth - (radius * 0.25)) {
        discard;
    }

    out_thickness = sphere_z * 2.0 * max(0.0, surface_params.particle_options.y);
}
