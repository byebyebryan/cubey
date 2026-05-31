#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/pbr.glsl"

const uint MAX_IRRADIANCE_SAMPLES = 128u;

layout(set = 0, binding = 0) uniform IrradianceFrame {
    vec4 right_roughness;
    vec4 up_sample_count;
    vec4 forward_mip_level;
} probe;

layout(set = 0, binding = 1) uniform samplerCube sky_radiance_cube;

layout(location = 0) in vec2 frag_ndc;
layout(location = 0) out vec4 out_color;

float radical_inverse_vdc(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint index, uint count) {
    return vec2(float(index) / float(count), radical_inverse_vdc(index));
}

vec3 cosine_sample_hemisphere(vec2 xi) {
    float radius = sqrt(xi.x);
    float phi = 2.0 * CUBEY_PBR_PI * xi.y;
    float x = radius * cos(phi);
    float y = radius * sin(phi);
    float z = sqrt(max(1.0 - xi.x, 0.0));
    return vec3(x, y, z);
}

vec3 tangent_to_world(vec3 tangent_sample, vec3 normal) {
    vec3 up = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);
    return normalize(tangent * tangent_sample.x + bitangent * tangent_sample.y +
                     normal * tangent_sample.z);
}

void main() {
    vec3 normal = normalize(
        probe.forward_mip_level.xyz +
        probe.right_roughness.xyz * frag_ndc.x -
        probe.up_sample_count.xyz * frag_ndc.y);

    vec3 accumulated = vec3(0.0);
    for (uint sample_index = 0u; sample_index < MAX_IRRADIANCE_SAMPLES; ++sample_index) {
        vec3 sample_direction = tangent_to_world(
            cosine_sample_hemisphere(hammersley(sample_index, MAX_IRRADIANCE_SAMPLES)), normal);
        accumulated += textureLod(sky_radiance_cube, sample_direction, 0.0).rgb;
    }

    out_color = vec4(accumulated / float(MAX_IRRADIANCE_SAMPLES), 1.0);
}
