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

float cubey_pbr_f0_from_ior(float ior) {
    if (ior == 0.0) {
        // KHR_materials_ior's specular-glossiness compatibility mode.
        return 1.0;
    }
    float clamped_ior = max(ior, 1.0);
    float root_f0 = (clamped_ior - 1.0) / (clamped_ior + 1.0);
    return root_f0 * root_f0;
}

vec3 cubey_pbr_dielectric_f0(vec3 specular_color_factor, float specular_factor,
                             float ior) {
    vec3 specular_color = max(specular_color_factor, vec3(0.0));
    float f0 = cubey_pbr_f0_from_ior(ior);
    return min(vec3(f0) * specular_color, vec3(1.0)) * cubey_pbr_saturate(specular_factor);
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

float cubey_pbr_visibility_smith_ggx_correlated_anisotropic(
    float ndotv, float ndotl, float tdotv, float bdotv, float tdotl, float bdotl,
    float alpha_t, float alpha_b) {
    float at = max(alpha_t, 0.001);
    float ab = max(alpha_b, 0.001);
    float lambda_v = ndotl * length(vec3(at * tdotv, ab * bdotv, ndotv));
    float lambda_l = ndotv * length(vec3(at * tdotl, ab * bdotl, ndotl));
    float visibility = 0.5 / max(lambda_v + lambda_l, 0.00001);
    return clamp(visibility, 0.0, 1.0);
}

vec3 cubey_pbr_anisotropic_bent_normal(vec3 normal, vec3 view_direction,
                                        vec3 anisotropy_bitangent, float roughness,
                                        float anisotropy_strength) {
    // The prefiltered environment is GGX-isotropic. This Khronos sample-renderer
    // heuristic bends its lookup normal toward the anisotropy direction instead of
    // requiring a second anisotropic prefilter. Preserve its raw intermediate
    // cross-product scale before the final normalization. Strength zero returns
    // exactly the isotropic normal.
    if (anisotropy_strength <= 0.0) {
        return normal;
    }
    vec3 anisotropic_tangent = cross(anisotropy_bitangent, view_direction);
    vec3 anisotropic_normal = cross(anisotropic_tangent, anisotropy_bitangent);
    float bend_factor = 1.0 - (anisotropy_strength * (1.0 - roughness));
    float bend_factor_pow4 = bend_factor * bend_factor * bend_factor * bend_factor;
    vec3 bent_normal = mix(anisotropic_normal, normal, bend_factor_pow4);
    if (dot(bent_normal, bent_normal) <= 1.0e-8) {
        return normal;
    }
    return normalize(bent_normal);
}

vec3 cubey_pbr_fresnel_schlick(float cos_theta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(cubey_pbr_saturate(1.0 - cos_theta), 5.0);
}

vec3 cubey_pbr_fresnel_schlick(float cos_theta, vec3 f0, vec3 f90) {
    return f0 + (f90 - f0) * pow(cubey_pbr_saturate(1.0 - cos_theta), 5.0);
}

float cubey_pbr_clearcoat_direct(float ndotv, float ndotl, float ndoth, float roughness) {
    if (ndotv <= 0.0 || ndotl <= 0.0) {
        return 0.0;
    }
    float d = cubey_pbr_distribution_ggx(ndoth, roughness);
    float v = cubey_pbr_visibility_smith_ggx_correlated(ndotv, ndotl, roughness);
    return d * v;
}

float cubey_pbr_clearcoat_layer_weight(float clearcoat, float ndotv) {
    // KHR_materials_clearcoat fixes the coating IOR at 1.5 (F0 = 0.04) and
    // layers its BRDF over the complete base BSDF with this one view Fresnel.
    float fresnel = cubey_pbr_fresnel_schlick(cubey_pbr_saturate(ndotv), vec3(0.04)).r;
    return cubey_pbr_saturate(clearcoat) * fresnel;
}

vec3 cubey_pbr_clearcoat_indirect(vec3 dfg) {
    // The layer Fresnel is applied by cubey_pbr_clearcoat_layer_weight. The
    // .b term stores the Fresnel-free white-conductor single-scatter lobe.
    return vec3(dfg.b);
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

// Khronos glTF Sample Renderer thin-film model. The XYZ sensitivity fit is
// evaluated in Fourier space, then converted to Rec.709 linear RGB.
const mat3 CUBEY_PBR_IRIDESCENCE_XYZ_TO_REC709 = mat3(
     3.2404542, -0.9692660,  0.0556434,
    -1.5371385,  1.8760108, -0.2040259,
    -0.4985314,  0.0415560,  1.0572252
);

float cubey_pbr_square(float value) {
    return value * value;
}

vec3 cubey_pbr_iridescence_fresnel0_to_ior(vec3 fresnel0) {
    vec3 sqrt_fresnel0 = sqrt(clamp(fresnel0, vec3(0.0), vec3(0.9999)));
    return (vec3(1.0) + sqrt_fresnel0) / max(vec3(1.0) - sqrt_fresnel0, vec3(0.0001));
}

float cubey_pbr_iridescence_ior_to_fresnel0(float transmitted_ior, float incident_ior) {
    float denominator = max(transmitted_ior + incident_ior, 0.0001);
    return cubey_pbr_square((transmitted_ior - incident_ior) / denominator);
}

vec3 cubey_pbr_iridescence_sensitivity(float optical_path_difference, vec3 shift) {
    float phase = 2.0 * CUBEY_PBR_PI * optical_path_difference * 1.0e-9;
    vec3 value = vec3(5.4856e-13, 4.4201e-13, 5.2481e-13);
    vec3 position = vec3(1.6810e+06, 1.7953e+06, 2.2084e+06);
    vec3 variance = vec3(4.3278e+09, 9.3046e+09, 6.6121e+09);
    float phase_squared = cubey_pbr_square(phase);

    vec3 xyz = value * sqrt(2.0 * CUBEY_PBR_PI * variance) *
               cos((position * phase) + shift) * exp(-phase_squared * variance);
    xyz.x += 9.7470e-14 * sqrt(2.0 * CUBEY_PBR_PI * 4.5282e+09) *
             cos((2.2399e+06 * phase) + shift.x) * exp(-4.5282e+09 * phase_squared);
    return CUBEY_PBR_IRIDESCENCE_XYZ_TO_REC709 * (xyz / 1.0685e-7);
}

vec3 cubey_pbr_iridescence_fresnel(float outside_ior, float film_ior, float cos_theta1,
                                    float thickness_nm, vec3 base_f0) {
    if (thickness_nm <= 0.0) {
        return cubey_pbr_saturate(base_f0);
    }

    float clamped_outside_ior = max(outside_ior, 1.0);
    float iridescence_ior = mix(clamped_outside_ior, max(film_ior, clamped_outside_ior),
                                smoothstep(0.0, 0.03, thickness_nm));
    float clamped_cos_theta1 = cubey_pbr_saturate(cos_theta1);
    float sin_theta2_squared = cubey_pbr_square(clamped_outside_ior / iridescence_ior) *
                               (1.0 - cubey_pbr_square(clamped_cos_theta1));
    float cos_theta2_squared = 1.0 - sin_theta2_squared;
    if (cos_theta2_squared < 0.0) {
        return vec3(1.0);
    }
    float cos_theta2 = sqrt(max(cos_theta2_squared, 0.0));

    float r0 = cubey_pbr_iridescence_ior_to_fresnel0(iridescence_ior, clamped_outside_ior);
    float r12 = cubey_pbr_fresnel_schlick(clamped_cos_theta1, vec3(r0)).r;
    float t121 = 1.0 - r12;
    float phi12 = iridescence_ior < clamped_outside_ior ? CUBEY_PBR_PI : 0.0;
    float phi21 = CUBEY_PBR_PI - phi12;

    vec3 base_ior = cubey_pbr_iridescence_fresnel0_to_ior(base_f0);
    vec3 r1 = pow((base_ior - vec3(iridescence_ior)) /
                      max(base_ior + vec3(iridescence_ior), vec3(0.0001)),
                  vec3(2.0));
    vec3 r23 = cubey_pbr_fresnel_schlick(cos_theta2, r1);
    vec3 phi23 = vec3(0.0);
    if (base_ior.r < iridescence_ior) {
        phi23.r = CUBEY_PBR_PI;
    }
    if (base_ior.g < iridescence_ior) {
        phi23.g = CUBEY_PBR_PI;
    }
    if (base_ior.b < iridescence_ior) {
        phi23.b = CUBEY_PBR_PI;
    }

    float optical_path_difference = 2.0 * iridescence_ior * thickness_nm * cos_theta2;
    vec3 phase_shift = vec3(phi21) + phi23;
    vec3 r123 = clamp(vec3(r12) * r23, vec3(1.0e-5), vec3(0.9999));
    vec3 r123_sqrt = sqrt(r123);
    vec3 rs = (t121 * t121) * r23 / max(vec3(1.0) - r123, vec3(0.0001));
    vec3 iridescence = vec3(r12) + rs;
    vec3 cm = rs - vec3(t121);
    for (int order = 1; order <= 2; ++order) {
        cm *= r123_sqrt;
        iridescence += cm * 2.0 * cubey_pbr_iridescence_sensitivity(
                                           float(order) * optical_path_difference,
                                           float(order) * phase_shift);
    }
    return max(iridescence, vec3(0.0));
}

vec3 cubey_pbr_fresnel_schlick_roughness(float cos_theta, vec3 f0, float roughness) {
    return f0 + (max(vec3(1.0 - roughness), f0) - f0) *
                    pow(cubey_pbr_saturate(1.0 - cos_theta), 5.0);
}

vec3 cubey_pbr_energy_compensation(vec3 f0, float white_conductor_single_scatter) {
    float energy = max(white_conductor_single_scatter, 0.0001);
    return 1.0 + f0 * ((1.0 / energy) - 1.0);
}

vec3 cubey_pbr_indirect_specular(vec3 f0, vec3 f90, vec3 dfg) {
    return (f0 * dfg.r + f90 * dfg.g) * cubey_pbr_energy_compensation(f0, dfg.b);
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
