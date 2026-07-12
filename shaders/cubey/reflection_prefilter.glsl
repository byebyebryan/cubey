#ifndef CUBEY_REFLECTION_PREFILTER_GLSL
#define CUBEY_REFLECTION_PREFILTER_GLSL

float cubey_reflection_radical_inverse_vdc(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 cubey_reflection_hammersley(uint index, uint count) {
    return vec2(float(index) / float(count), cubey_reflection_radical_inverse_vdc(index));
}

vec3 cubey_reflection_importance_sample_ggx(vec2 xi, float roughness) {
    float alpha = roughness * roughness;
    float phi = 2.0 * CUBEY_PBR_PI * xi.x;
    float cos_theta = sqrt((1.0 - xi.y) /
                           max(1.0 + ((alpha * alpha) - 1.0) * xi.y, 0.00001));
    float sin_theta = sqrt(max(1.0 - cos_theta * cos_theta, 0.0));
    return vec3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
}

vec3 cubey_reflection_tangent_to_world(vec3 tangent_sample, vec3 normal) {
    vec3 up = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);
    return normalize(tangent * tangent_sample.x + bitangent * tangent_sample.y +
                     normal * tangent_sample.z);
}

#endif
