#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/pbr.glsl"

const uint MAX_PREFILTER_SAMPLES = 96u;

layout(set = 0, binding = 0) uniform PrefilterFrame {
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

vec3 importance_sample_ggx(vec2 xi, float roughness) {
    float alpha = roughness * roughness;
    float phi = 2.0 * CUBEY_PBR_PI * xi.x;
    float cos_theta = sqrt((1.0 - xi.y) / max(1.0 + ((alpha * alpha) - 1.0) * xi.y, 0.00001));
    float sin_theta = sqrt(max(1.0 - cos_theta * cos_theta, 0.0));
    return vec3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
}

vec3 tangent_to_world(vec3 tangent_sample, vec3 normal) {
    vec3 up = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);
    return normalize(tangent * tangent_sample.x + bitangent * tangent_sample.y +
                     normal * tangent_sample.z);
}

void main() {
    vec3 direction = normalize(
        probe.forward_mip_level.xyz +
        probe.right_roughness.xyz * frag_ndc.x -
        probe.up_sample_count.xyz * frag_ndc.y);
    float roughness = clamp(probe.right_roughness.w, 0.0, 1.0);
    uint sample_count = clamp(uint(probe.up_sample_count.w + 0.5), 1u, MAX_PREFILTER_SAMPLES);

    if (roughness <= 0.0001 || sample_count == 1u) {
        out_color = vec4(textureLod(sky_radiance_cube, direction, 0.0).rgb, 1.0);
        return;
    }

    vec3 accumulated = vec3(0.0);
    float total_weight = 0.0;
    for (uint sample_index = 0u; sample_index < MAX_PREFILTER_SAMPLES; ++sample_index) {
        if (sample_index >= sample_count) {
            break;
        }
        vec3 halfway = tangent_to_world(importance_sample_ggx(
                                            hammersley(sample_index, sample_count), roughness),
                                        direction);
        vec3 sample_direction = normalize(2.0 * dot(direction, halfway) * halfway - direction);
        float ndotl = max(dot(direction, sample_direction), 0.0);
        if (ndotl > 0.0) {
            accumulated += textureLod(sky_radiance_cube, sample_direction, 0.0).rgb * ndotl;
            total_weight += ndotl;
        }
    }

    out_color = vec4(accumulated / max(total_weight, 0.00001), 1.0);
}
