#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"

layout(location = 0) in vec3 world_position;
layout(location = 1) in vec3 world_normal;
layout(location = 2) in vec2 world_xz;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform CatchmentParams {
    mat4 view_projection;
    vec4 grid_cell;
    vec4 camera_wet;
    vec4 presentation;
} params;

void main() {
    vec3 normal = normalize(world_normal);
    vec3 light_direction = normalize(vec3(-0.45, 0.82, 0.35));
    float diffuse = max(dot(normal, light_direction), 0.0);
    float contour = 0.006 * sin(world_position.y * 0.75 + world_xz.x * 0.03);
    vec3 lowland = cubey_srgb_to_linear(vec3(0.105, 0.205, 0.105));
    vec3 highland = cubey_srgb_to_linear(vec3(0.46, 0.31, 0.16));
    float elevation = clamp((world_position.y + 2.0) * 0.035, 0.0, 1.0);
    vec3 albedo = mix(lowland, highland, elevation) + vec3(contour);
    vec3 lighting = vec3(0.26) + vec3(0.74) * diffuse;
    vec3 color = max(albedo * lighting, vec3(0.0));
    // Water Isolation is a presentation-only reading aid: make the bed quiet
    // and neutral so the water edge carries the visual contrast. Composite and
    // Flow Inspection intentionally retain the original terrain shading.
    if (params.presentation.y > 0.5 && params.presentation.y < 1.5) {
        float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
        color = mix(vec3(luminance), vec3(0.020, 0.028, 0.038), 0.55) * 0.42;
    }
    out_color = vec4(color, 1.0);
}
