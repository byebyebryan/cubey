#ifndef CUBEY_CLOUD_REF_NOISE_COMMON_GLSL
#define CUBEY_CLOUD_REF_NOISE_COMMON_GLSL

float cloud_ref_noise_hash(vec3 p) {
    p = fract(p * 0.3183099 + vec3(0.13, 0.17, 0.19));
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float cloud_ref_noise_hash2(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float cloud_ref_value_noise_3d(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    float v000 = cloud_ref_noise_hash(i + vec3(0.0, 0.0, 0.0));
    float v100 = cloud_ref_noise_hash(i + vec3(1.0, 0.0, 0.0));
    float v010 = cloud_ref_noise_hash(i + vec3(0.0, 1.0, 0.0));
    float v110 = cloud_ref_noise_hash(i + vec3(1.0, 1.0, 0.0));
    float v001 = cloud_ref_noise_hash(i + vec3(0.0, 0.0, 1.0));
    float v101 = cloud_ref_noise_hash(i + vec3(1.0, 0.0, 1.0));
    float v011 = cloud_ref_noise_hash(i + vec3(0.0, 1.0, 1.0));
    float v111 = cloud_ref_noise_hash(i + vec3(1.0, 1.0, 1.0));
    float x00 = mix(v000, v100, u.x);
    float x10 = mix(v010, v110, u.x);
    float x01 = mix(v001, v101, u.x);
    float x11 = mix(v011, v111, u.x);
    return mix(mix(x00, x10, u.y), mix(x01, x11, u.y), u.z);
}

float cloud_ref_fbm_3d(vec3 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float norm = 0.0;
    for (int i = 0; i < octaves; ++i) {
        value += cloud_ref_value_noise_3d(p) * amplitude;
        norm += amplitude;
        p = p * 2.02 + vec3(13.7, 7.3, 5.1);
        amplitude *= 0.5;
    }
    return clamp(value / max(norm, 0.0001), 0.0, 1.0);
}

float cloud_ref_worley_3d(vec3 p, float cell_count) {
    vec3 cell = floor(p * cell_count);
    vec3 local = fract(p * cell_count);
    float min_dist = 1.0;
    for (int z = -1; z <= 1; ++z) {
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                vec3 offset = vec3(float(x), float(y), float(z));
                vec3 feature = vec3(
                    cloud_ref_noise_hash(cell + offset + vec3(11.0, 0.0, 0.0)),
                    cloud_ref_noise_hash(cell + offset + vec3(0.0, 17.0, 0.0)),
                    cloud_ref_noise_hash(cell + offset + vec3(0.0, 0.0, 23.0)));
                vec3 delta = offset + feature - local;
                min_dist = min(min_dist, length(delta));
            }
        }
    }
    return clamp(min_dist, 0.0, 1.0);
}

float cloud_ref_value_noise_2d(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(cloud_ref_noise_hash2(i + vec2(0.0, 0.0)),
                   cloud_ref_noise_hash2(i + vec2(1.0, 0.0)), u.x),
               mix(cloud_ref_noise_hash2(i + vec2(0.0, 1.0)),
                   cloud_ref_noise_hash2(i + vec2(1.0, 1.0)), u.x),
               u.y);
}

float cloud_ref_fbm_2d(vec2 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float norm = 0.0;
    for (int i = 0; i < octaves; ++i) {
        value += cloud_ref_value_noise_2d(p) * amplitude;
        norm += amplitude;
        p = p * 2.03 + vec2(17.1, 5.7);
        amplitude *= 0.52;
    }
    return clamp(value / max(norm, 0.0001), 0.0, 1.0);
}

float cloud_ref_noise_remap(float value, float old_min, float old_max, float new_min,
                            float new_max) {
    return new_min + ((value - old_min) / max(old_max - old_min, 0.00001)) *
                         (new_max - new_min);
}

#endif
