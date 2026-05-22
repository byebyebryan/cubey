#version 450
#extension GL_GOOGLE_include_directive : require

#include "water_3d_surface_common.glsl"

WATER3D_SURFACE_PARAMS;

#include "water_3d_surface_helpers.glsl"

layout(set = 0, binding = 0) uniform sampler2D surface_depth;
layout(set = 0, binding = 1) uniform sampler2D surface_thickness;

layout(location = 0) in vec2 frag_uv;
layout(location = 0) out vec4 out_packed;

void main() {
    float raw_depth = texture(surface_depth, frag_uv).r;
    float raw_thickness = texture(surface_thickness, frag_uv).r;
    if (!water_surface_has_depth(raw_depth) || raw_thickness <= 0.0) {
        out_packed = vec4(WATER3D_SURFACE_DEPTH_SENTINEL, 0.0, 0.0,
                          WATER3D_SURFACE_DEPTH_SENTINEL);
        return;
    }

    out_packed = vec4(raw_depth, raw_thickness, raw_thickness, raw_depth);
}
