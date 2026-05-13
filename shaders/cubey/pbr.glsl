const float CUBEY_PBR_PI = 3.14159265359;

float cubey_pbr_saturate(float value) {
    return clamp(value, 0.0, 1.0);
}

vec3 cubey_pbr_saturate(vec3 value) {
    return clamp(value, vec3(0.0), vec3(1.0));
}

float cubey_pbr_distribution_ggx(float ndoth, float roughness) {
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float ndoth2 = ndoth * ndoth;
    float denom = (ndoth2 * (alpha2 - 1.0)) + 1.0;
    return alpha2 / max(CUBEY_PBR_PI * denom * denom, 0.00001);
}

float cubey_pbr_visibility_smith_ggx_correlated(float ndotv, float ndotl, float roughness) {
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float lambda_v = ndotl * sqrt(max(((ndotv - (alpha2 * ndotv)) * ndotv) + alpha2, 0.0));
    float lambda_l = ndotv * sqrt(max(((ndotl - (alpha2 * ndotl)) * ndotl) + alpha2, 0.0));
    return 0.5 / max(lambda_v + lambda_l, 0.00001);
}

vec3 cubey_pbr_fresnel_schlick(float cos_theta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(cubey_pbr_saturate(1.0 - cos_theta), 5.0);
}

vec3 cubey_pbr_fresnel_schlick_roughness(float cos_theta, vec3 f0, float roughness) {
    return f0 + (max(vec3(1.0 - roughness), f0) - f0) *
                    pow(cubey_pbr_saturate(1.0 - cos_theta), 5.0);
}

vec3 cubey_pbr_energy_compensation(vec3 f0, float white_conductor_single_scatter) {
    float energy = max(white_conductor_single_scatter, 0.0001);
    return 1.0 + f0 * ((1.0 / energy) - 1.0);
}

vec3 cubey_pbr_indirect_specular(vec3 f0, vec3 dfg) {
    return (f0 * dfg.r + vec3(dfg.g)) * cubey_pbr_energy_compensation(f0, dfg.b);
}

float cubey_pbr_specular_ao(float ndotv, float ambient_occlusion, float roughness) {
    return cubey_pbr_saturate(pow(ndotv + ambient_occlusion, exp2(-16.0 * roughness - 1.0)) -
                              1.0 + ambient_occlusion);
}

float cubey_pbr_horizon_specular_occlusion(vec3 reflection, vec3 geometric_normal) {
    float horizon = min(1.0 + dot(reflection, geometric_normal), 1.0);
    return horizon * horizon;
}
