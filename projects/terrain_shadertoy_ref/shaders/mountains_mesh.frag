#version 450
#extension GL_GOOGLE_include_directive : require

#include "reference_frame.glsl"
#include "mountains.glsl"

layout(location = 0) in vec3 in_world_position;
layout(location = 1) in vec2 in_atlas_uv;
layout(location = 0) out vec4 out_color;

vec3 geometryNormal() {
    vec3 normal = normalize(cross(dFdy(in_world_position), dFdx(in_world_position)));
    return normal.y < 0.0 ? -normal : normal;
}

vec3 detailedNormal(float distance_to_camera) {
    const float p = 0.02 + 0.00005 * distance_to_camera * distance_to_camera;
    const vec3 center = vec3(0.0, Terrain2(in_world_position.xz), 0.0);
    const vec3 x_sample = center -
        vec3(p, Terrain2(in_world_position.xz + vec2(p, 0.0)), 0.0);
    const vec3 z_sample = center -
        vec3(0.0, Terrain2(in_world_position.xz + vec2(0.0, -p)), -p);
    return normalize(cross(x_sample, z_sample));
}

void main() {
    cameraPos = reference_frame.camera_position_time.xyz;
    if (reference_frame.domain_center_extent_surface.w > 0.5) {
        Map(in_world_position);
    } else {
        treeLine = 0.0;
        treeCol = 0.0;
    }
    const float distance_to_camera = length(in_world_position - cameraPos);
    const vec3 normal = reference_frame.resolution_options.z > 0.5
        ? detailedNormal(distance_to_camera)
        : geometryNormal();

    vec3 color;
    if (reference_frame.resolution_options.w > 0.5) {
        color = TerrainColour(in_world_position, normal, distance_to_camera);
    } else {
        const float light = max(dot(sunLight, normal), 0.0) + 0.14;
        color = vec3(0.46, 0.45, 0.43) * sunColour * light;
        color = ApplyFog(color, distance_to_camera * distance_to_camera,
                         normalize(in_world_position - cameraPos));
    }
    out_color = vec4(PostEffects(color, in_atlas_uv * 2.0 - 1.0), 1.0);
}
