#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/terrain/terrain_environment.glsl"
#include "cubey/terrain/terrain_lighting.glsl"

layout(push_constant) uniform TerrainStageProxyPushConstants {
    mat4 view_projection;
    vec4 camera_position;
    vec4 object_translation;
} pc;

layout(location = 0) in vec3 frag_world_position;
layout(location = 1) in vec3 frag_color;
layout(location = 2) in vec3 frag_normal;

layout(location = 0) out vec4 out_color;

void main() {
    vec3 normal = normalize(frag_normal);
    vec3 view_direction = normalize(pc.camera_position.xyz - frag_world_position);
    vec3 light_direction = normalize(atmosphere.primary_light_direction_intensity.xyz);
    vec3 light_radiance = atmosphere.primary_light_color_angular_radius.xyz *
        atmosphere.primary_light_direction_intensity.w;
    vec3 color = terrain_lighting_ambient(
        frag_color, terrain_diffuse_irradiance(normal), 1.0);
    color += terrain_lighting_direct(
        frag_color, 0.72, normal, view_direction, light_direction, light_radiance, 1.0);
    CubeyAtmosphereSample aerial = terrain_aerial_perspective(
        pc.camera_position.xyz, frag_world_position);
    out_color = vec4(color * aerial.transmittance + aerial.color, 1.0);
}
