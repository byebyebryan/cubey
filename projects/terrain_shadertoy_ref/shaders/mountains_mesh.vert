#version 450
#extension GL_GOOGLE_include_directive : require

#include "reference_frame.glsl"

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec3 out_world_position;
layout(location = 1) out vec2 out_atlas_uv;

void main() {
    const vec4 height_fields = textureLod(reference_height_atlas, in_uv, 0.0);
    const float height = reference_frame.domain_center_extent_surface.w > 0.5
        ? height_fields.g
        : height_fields.r;
    const vec2 world_xz = reference_frame.domain_center_extent_surface.xy +
        (in_uv - 0.5) * reference_frame.domain_center_extent_surface.z;
    out_world_position = vec3(world_xz.x, height, world_xz.y);
    out_atlas_uv = in_uv;
    gl_Position = reference_frame.view_projection * vec4(out_world_position, 1.0);
}
