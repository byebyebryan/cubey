const float CUBEY_PBR_PI = 3.14159265359;
float cubey_pbr_saturate(float value) {
    return clamp(value, 0.0, 1.0);
}

vec3 cubey_pbr_saturate(vec3 value) {
    return clamp(value, vec3(0.0), vec3(1.0));
}

vec3 cubey_pbr_diffuse_color(vec3 base_color, float metallic) {
    return base_color * (1.0 - metallic);
}

float cubey_pbr_f0_from_reflectance(float reflectance) {
    float clamped = cubey_pbr_saturate(reflectance);
    return 0.16 * clamped * clamped;
}

vec3 cubey_pbr_dielectric_f0(vec3 specular_color_factor, float specular_factor,
                             float reflectance) {
    return vec3(cubey_pbr_f0_from_reflectance(reflectance)) *
           cubey_pbr_saturate(specular_color_factor) * cubey_pbr_saturate(specular_factor);
}

vec3 cubey_pbr_f0(vec3 base_color, float metallic, vec3 dielectric_f0) {
    return mix(dielectric_f0, base_color, metallic);
}

vec3 cubey_pbr_lambert_diffuse(vec3 diffuse_color) {
    return diffuse_color / CUBEY_PBR_PI;
}

float cubey_pbr_distribution_ggx(float ndoth, float roughness) {
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float ndoth2 = ndoth * ndoth;
    float denom = (ndoth2 * (alpha2 - 1.0)) + 1.0;
    return alpha2 / max(CUBEY_PBR_PI * denom * denom, 0.00001);
}

float cubey_pbr_distribution_ggx_anisotropic(float tdoth, float bdoth, float ndoth,
                                             float alpha_t, float alpha_b) {
    float at = max(alpha_t, 0.001);
    float ab = max(alpha_b, 0.001);
    float denom = ((tdoth * tdoth) / (at * at)) + ((bdoth * bdoth) / (ab * ab)) +
                  (ndoth * ndoth);
    return 1.0 / max(CUBEY_PBR_PI * at * ab * denom * denom, 0.00001);
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

float cubey_pbr_clearcoat_direct(float ndotv, float ndotl, float ndoth, float vdoth,
                                 float roughness) {
    if (ndotv <= 0.0 || ndotl <= 0.0) {
        return 0.0;
    }
    float d = cubey_pbr_distribution_ggx(ndoth, roughness);
    float v = cubey_pbr_visibility_smith_ggx_correlated(ndotv, ndotl, roughness);
    float f = cubey_pbr_fresnel_schlick(vdoth, vec3(0.04)).r;
    return d * v * f;
}

float cubey_pbr_distribution_charlie(float ndoth, float roughness) {
    float alpha_g = max(roughness * roughness, 0.0001);
    float inv_r = 1.0 / alpha_g;
    float sin2h = max(1.0 - (ndoth * ndoth), 0.0);
    return ((2.0 + inv_r) * pow(sin2h, inv_r * 0.5)) / (2.0 * CUBEY_PBR_PI);
}

vec3 cubey_pbr_sheen_direct(vec3 sheen_color, float sheen_roughness, float ndotv, float ndotl,
                            float ndoth) {
    if (ndotv <= 0.0 || ndotl <= 0.0) {
        return vec3(0.0);
    }
    float d = cubey_pbr_distribution_charlie(ndoth, clamp(sheen_roughness, 0.01, 1.0));
    float v = 1.0 / max(4.0 * (ndotl + ndotv - (ndotl * ndotv)), 0.00001);
    return sheen_color * d * v;
}

vec3 cubey_pbr_iridescence_f0(vec3 base_f0, float factor, float ior, float thickness_nm) {
    float strength = cubey_pbr_saturate(factor);
    float clamped_ior = max(ior, 1.0);
    float root_f0 = (clamped_ior - 1.0) / (clamped_ior + 1.0);
    float film_f0 = root_f0 * root_f0;
    vec3 phase = vec3(0.0, 2.0943951, 4.1887902) + (thickness_nm * 0.024);
    vec3 film_color = 0.5 + (0.5 * cos(phase));
    vec3 iridescent_f0 = cubey_pbr_saturate(base_f0 + (film_color * film_f0));
    return mix(base_f0, iridescent_f0, strength);
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

vec3 cubey_pbr_tonemap_aces(vec3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return cubey_pbr_saturate((color * ((a * color) + b)) /
                              (color * ((c * color) + d) + e));
}

vec3 cubey_pbr_linear_to_srgb(vec3 color) {
    vec3 clamped = max(color, vec3(0.0));
    vec3 lower = clamped * 12.92;
    vec3 higher = (1.055 * pow(clamped, vec3(1.0 / 2.4))) - vec3(0.055);
    return mix(higher, lower, lessThanEqual(clamped, vec3(0.0031308)));
}

vec3 cubey_pbr_apply_display_transform(vec3 linear_color, vec4 display_transform) {
    float exposure = display_transform.x;
    float tonemap_mode = display_transform.y;
    float output_encoding = display_transform.z;

    vec3 color = max(linear_color * exp2(exposure), vec3(0.0));
    if (tonemap_mode > 0.5) {
        color = cubey_pbr_tonemap_aces(color);
    }
    color = cubey_pbr_saturate(color);
    if (output_encoding > 0.5) {
        color = cubey_pbr_linear_to_srgb(color);
    }
    return cubey_pbr_saturate(color);
}
