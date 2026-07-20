#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) out vec4 out_height_fields;

layout(set = 0, binding = 0) uniform sampler2D iChannel0;
layout(set = 0, binding = 1) uniform sampler2D iChannel1;

layout(push_constant) uniform BakePushConstants {
    vec4 resolution_time;
    vec4 mouse;
    vec4 domain_center_extent;
} bake_push;

#define iResolution bake_push.resolution_time.xyz
#define iTime bake_push.resolution_time.w
#define iMouse bake_push.mouse

#include "mountains.glsl"

void main() {
    const vec2 uv = gl_FragCoord.xy / iResolution.xy;
    const vec2 world = bake_push.domain_center_extent.xy +
        (uv - 0.5) * bake_push.domain_center_extent.z;
    const float base_height = Terrain(world);
    const float map_height = -Map(vec3(world.x, 0.0, world.y));
    const float map_tree = treeCol;
    const float detailed_height = Terrain2(world);
    out_height_fields = vec4(base_height, map_height, detailed_height, map_tree);
}
