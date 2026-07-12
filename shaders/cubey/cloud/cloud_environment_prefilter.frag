#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/pbr.glsl"
#include "cubey/reflection_prefilter.glsl"

const uint MAX_PREFILTER_SAMPLES = 96u;

layout(set = 0, binding = 0) uniform PrefilterFrame {
    vec4 right_roughness;
    vec4 up_sample_count;
    vec4 forward_mip_level;
} probe;

layout(set = 0, binding = 1) uniform samplerCube sky_radiance_cube;
layout(set = 0, binding = 2) uniform samplerCube cloud_contribution_cube;

layout(location = 0) in vec2 frag_ndc;
layout(location = 0) out vec4 out_color;

vec3 cloud_environment_radiance(vec3 direction) {
    vec4 cloud = textureLod(cloud_contribution_cube, direction, 0.0);
    vec3 clear_sky = textureLod(sky_radiance_cube, direction, 0.0).rgb;
    return max(cloud.rgb + clamp(cloud.a, 0.0, 1.0) * clear_sky, vec3(0.0));
}

void main() {
    vec3 direction = normalize(
        probe.forward_mip_level.xyz +
        probe.right_roughness.xyz * frag_ndc.x -
        probe.up_sample_count.xyz * frag_ndc.y);
    float roughness = clamp(probe.right_roughness.w, 0.0, 1.0);
    uint sample_count = clamp(uint(probe.up_sample_count.w + 0.5), 1u,
                              MAX_PREFILTER_SAMPLES);

    if (roughness <= 0.0001 || sample_count == 1u) {
        out_color = vec4(cloud_environment_radiance(direction), 1.0);
        return;
    }

    vec3 accumulated = vec3(0.0);
    float total_weight = 0.0;
    for (uint sample_index = 0u; sample_index < MAX_PREFILTER_SAMPLES; ++sample_index) {
        if (sample_index >= sample_count) {
            break;
        }
        vec3 halfway = cubey_reflection_tangent_to_world(
            cubey_reflection_importance_sample_ggx(
                cubey_reflection_hammersley(sample_index, sample_count), roughness),
            direction);
        vec3 sample_direction =
            normalize(2.0 * dot(direction, halfway) * halfway - direction);
        float ndotl = max(dot(direction, sample_direction), 0.0);
        if (ndotl > 0.0) {
            accumulated += cloud_environment_radiance(sample_direction) * ndotl;
            total_weight += ndotl;
        }
    }

    out_color = vec4(accumulated / max(total_weight, 0.00001), 1.0);
}
