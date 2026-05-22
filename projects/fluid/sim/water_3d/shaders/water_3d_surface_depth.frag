#version 450
#extension GL_GOOGLE_include_directive : require

#include "water_3d_surface_common.glsl"

WATER3D_SURFACE_PARAMS;

#include "water_3d_surface_helpers.glsl"

layout(location = 0) in vec2 frag_local;
layout(location = 1) in vec3 frag_center;
layout(location = 0) out float out_depth;

void main() {
    float radius_squared = dot(frag_local, frag_local);
    if (radius_squared > 1.0) {
        discard;
    }

    float radius = water_surface_particle_radius();
    float sphere_z = sqrt(max(0.0, 1.0 - radius_squared)) * radius;
    vec3 surface_world = frag_center - water_surface_camera_forward() * sphere_z;
    vec4 clip = surface_params.view_projection * vec4(surface_world, 1.0);
    if (clip.w <= 0.0) {
        discard;
    }

    float device_depth = clip.z / clip.w;
    if (device_depth < 0.0 || device_depth > 1.0) {
        discard;
    }

    out_depth = length(surface_world - water_surface_camera_position());
    gl_FragDepth = device_depth;
}
