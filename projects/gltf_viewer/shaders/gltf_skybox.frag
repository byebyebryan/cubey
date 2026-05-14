#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/pbr.glsl"

layout(set = 0, binding = 0) uniform SkyboxUniforms {
    mat4 inverse_view_projection;
    vec4 camera_position;
    vec4 environment_rotation_intensity;
    vec4 display_transform;
} skybox;

layout(set = 0, binding = 1) uniform samplerCube environment_cube;

layout(location = 0) in vec2 frag_ndc;
layout(location = 0) out vec4 out_color;

vec3 rotate_environment_direction(vec3 direction) {
    float c = skybox.environment_rotation_intensity.x;
    float s = skybox.environment_rotation_intensity.y;
    return vec3(
        (c * direction.x) + (s * direction.z),
        direction.y,
        (-s * direction.x) + (c * direction.z)
    );
}

void main() {
    vec4 world = skybox.inverse_view_projection * vec4(frag_ndc, 1.0, 1.0);
    vec3 direction = normalize((world.xyz / world.w) - skybox.camera_position.xyz);
    vec3 environment_direction = rotate_environment_direction(direction);
    vec3 color = textureLod(environment_cube, environment_direction, 0.0).rgb *
                 skybox.environment_rotation_intensity.z;
    out_color = vec4(color, 1.0);
}
