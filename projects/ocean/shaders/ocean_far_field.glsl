#ifndef CUBEY_OCEAN_FAR_FIELD_GLSL
#define CUBEY_OCEAN_FAR_FIELD_GLSL

vec2 rotate2(vec2 value, float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return vec2(c * value.x - s * value.y, s * value.x + c * value.y);
}

float detail_anti_repeat_angle(uint cascade, uint domain) {
    float slot = float(cascade);
    return domain == 0u ? 0.39 + slot * 0.271 : -0.83 - slot * 0.193;
}

float detail_anti_repeat_scale(uint cascade, uint domain) {
    float slot = float(cascade);
    return domain == 0u ? 1.27 + slot * 0.10 : max(0.48, 0.76 - slot * 0.035);
}

vec2 detail_anti_repeat_offset(uint cascade, uint domain) {
    float slot = float(cascade);
    return domain == 0u ? vec2(719.0 - slot * 147.0, -277.0 + slot * 73.0)
                        : vec2(-607.0 + slot * 89.0, 431.0 - slot * 127.0);
}

vec2 detail_anti_repeat_weights(uint cascade, vec2 position, float factor) {
    float seed = float(cascade) * 19.37;
    float first = cubey_proc_value_noise_pcg_2d(position * 0.0011 + vec2(seed, seed * 0.37));
    float second =
        cubey_proc_value_noise_pcg_2d(position * 0.00073 + vec2(seed + 41.0, seed - 13.0));
    return factor * vec2(mix(0.38, 0.72, first), mix(0.28, 0.58, second));
}

float foam_breakup_weight(uint cascade, vec2 position, float factor) {
    if (factor <= 0.0) {
        return 1.0;
    }
    float seed = float(cascade) * 31.19;
    float broad = cubey_proc_value_noise_pcg_2d(position * 0.00041 + vec2(seed, seed * 0.53));
    float mid = cubey_proc_value_noise_pcg_2d(position * 0.0017 + vec2(seed - 17.0, seed + 29.0));
    float breakup = mix(0.58, 1.22, broad) * mix(0.82, 1.12, mid);
    return mix(1.0, breakup, factor);
}

float ocean_far_reflection_variation(vec2 position, float far_material_energy) {
    float strength = ocean_far_reflection_variation_strength() * far_material_energy;
    if (strength <= 0.0) {
        return 1.0;
    }
    float time = ocean.camera_time.w;
    float broad =
        cubey_proc_value_noise_pcg_2d(position / 5200.0 + vec2(19.0 + time * 0.00005, -7.0));
    float mid = cubey_proc_value_noise_pcg_2d(rotate2(position, 0.57) / 1800.0 +
                                              vec2(-31.0, 23.0 - time * 0.00008));
    float signal = (broad * 0.65 + mid * 0.35) * 2.0 - 1.0;
    return clamp(1.0 + signal * strength, 0.65, 1.25);
}

float ocean_far_sun_glitter(vec3 reflection_dir, vec3 sun_dir, vec2 position,
                            float far_material_energy) {
    float strength = ocean_far_glint_strength() * far_material_energy;
    if (strength <= 0.0) {
        return 0.0;
    }
    float alignment = max(dot(normalize(reflection_dir), normalize(sun_dir)), 0.0);
    float corridor = smoothstep(max(0.0, 1.0 - ocean_sun_glitter_width()), 1.0, alignment);
    float light_gate = smoothstep(0.015, 0.35, ocean_sun_light_intensity());
    float broad_variation = ocean_far_reflection_variation(position, far_material_energy);
    float variation = mix(0.72, 1.22, clamp((broad_variation - 0.65) / 0.60, 0.0, 1.0));
    return corridor * strength * light_gate * variation;
}


#endif
