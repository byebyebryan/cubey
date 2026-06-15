#ifndef CUBEY_CLOUD_NOISE_COMMON_GLSL
#define CUBEY_CLOUD_NOISE_COMMON_GLSL

// Source-aligned helper set from TerrainEngine-OpenGL's cloud noise shaders.
// TerrainEngine credits Sebastien Hillaire's tileable volume noise work and
// NadirRoGue/Nadir Roman Guerrero for the cloud noise references.

const float kCloudFrequencyMul[6] = float[6](2.0, 8.0, 14.0, 20.0, 26.0, 32.0);

float cloud_hash_int(int n) {
    return fract(sin(float(n) + 1.951) * 43758.5453123);
}

float cloud_value_noise(vec3 x) {
    vec3 p = floor(x);
    vec3 f = fract(x);
    f = f * f * (vec3(3.0) - vec3(2.0) * f);

    float n = p.x + p.y * 57.0 + 113.0 * p.z;
    return mix(
        mix(mix(cloud_hash_int(int(n + 0.0)), cloud_hash_int(int(n + 1.0)), f.x),
            mix(cloud_hash_int(int(n + 57.0)), cloud_hash_int(int(n + 58.0)), f.x),
            f.y),
        mix(mix(cloud_hash_int(int(n + 113.0)), cloud_hash_int(int(n + 114.0)), f.x),
            mix(cloud_hash_int(int(n + 170.0)), cloud_hash_int(int(n + 171.0)), f.x),
            f.y),
        f.z);
}

float cloud_cells(vec3 p, float cell_count) {
    vec3 p_cell = p * cell_count;
    float d = 1.0e10;
    for (int xo = -1; xo <= 1; ++xo) {
        for (int yo = -1; yo <= 1; ++yo) {
            for (int zo = -1; zo <= 1; ++zo) {
                vec3 tp = floor(p_cell) + vec3(xo, yo, zo);
                tp = p_cell - tp - cloud_value_noise(mod(tp, cell_count));
                d = min(d, dot(tp, tp));
            }
        }
    }
    return clamp(d, 0.0, 1.0);
}

vec4 cloud_mod289(vec4 x) {
    return x - floor(x * vec4(1.0 / 289.0)) * vec4(289.0);
}

vec4 cloud_permute(vec4 x) {
    return cloud_mod289(((x * 34.0) + 1.0) * x);
}

vec4 cloud_taylor_inv_sqrt(vec4 r) {
    return vec4(1.79284291400159) - vec4(0.85373472095314) * r;
}

vec4 cloud_fade(vec4 t) {
    return (t * t * t) * (t * (t * vec4(6.0) - vec4(15.0)) + vec4(10.0));
}

float cloud_glm_perlin_4d(vec4 position, vec4 rep) {
    vec4 pi0 = mod(floor(position), rep);
    vec4 pi1 = mod(pi0 + vec4(1.0), rep);
    vec4 pf0 = fract(position);
    vec4 pf1 = pf0 - vec4(1.0);

    vec4 ix = vec4(pi0.x, pi1.x, pi0.x, pi1.x);
    vec4 iy = vec4(pi0.y, pi0.y, pi1.y, pi1.y);
    vec4 iz0 = vec4(pi0.z);
    vec4 iz1 = vec4(pi1.z);
    vec4 iw0 = vec4(pi0.w);
    vec4 iw1 = vec4(pi1.w);

    vec4 ixy = cloud_permute(cloud_permute(ix) + iy);
    vec4 ixy0 = cloud_permute(ixy + iz0);
    vec4 ixy1 = cloud_permute(ixy + iz1);
    vec4 ixy00 = cloud_permute(ixy0 + iw0);
    vec4 ixy01 = cloud_permute(ixy0 + iw1);
    vec4 ixy10 = cloud_permute(ixy1 + iw0);
    vec4 ixy11 = cloud_permute(ixy1 + iw1);

    vec4 gx00 = ixy00 / vec4(7.0);
    vec4 gy00 = floor(gx00) / vec4(7.0);
    vec4 gz00 = floor(gy00) / vec4(6.0);
    gx00 = fract(gx00) - vec4(0.5);
    gy00 = fract(gy00) - vec4(0.5);
    gz00 = fract(gz00) - vec4(0.5);
    vec4 gw00 = vec4(0.75) - abs(gx00) - abs(gy00) - abs(gz00);
    vec4 sw00 = step(gw00, vec4(0.0));
    gx00 -= sw00 * (step(vec4(0.0), gx00) - vec4(0.5));
    gy00 -= sw00 * (step(vec4(0.0), gy00) - vec4(0.5));

    vec4 gx01 = ixy01 / vec4(7.0);
    vec4 gy01 = floor(gx01) / vec4(7.0);
    vec4 gz01 = floor(gy01) / vec4(6.0);
    gx01 = fract(gx01) - vec4(0.5);
    gy01 = fract(gy01) - vec4(0.5);
    gz01 = fract(gz01) - vec4(0.5);
    vec4 gw01 = vec4(0.75) - abs(gx01) - abs(gy01) - abs(gz01);
    vec4 sw01 = step(gw01, vec4(0.0));
    gx01 -= sw01 * (step(vec4(0.0), gx01) - vec4(0.5));
    gy01 -= sw01 * (step(vec4(0.0), gy01) - vec4(0.5));

    vec4 gx10 = ixy10 / vec4(7.0);
    vec4 gy10 = floor(gx10) / vec4(7.0);
    vec4 gz10 = floor(gy10) / vec4(6.0);
    gx10 = fract(gx10) - vec4(0.5);
    gy10 = fract(gy10) - vec4(0.5);
    gz10 = fract(gz10) - vec4(0.5);
    vec4 gw10 = vec4(0.75) - abs(gx10) - abs(gy10) - abs(gz10);
    vec4 sw10 = step(gw10, vec4(0.0));
    gx10 -= sw10 * (step(vec4(0.0), gx10) - vec4(0.5));
    gy10 -= sw10 * (step(vec4(0.0), gy10) - vec4(0.5));

    vec4 gx11 = ixy11 / vec4(7.0);
    vec4 gy11 = floor(gx11) / vec4(7.0);
    vec4 gz11 = floor(gy11) / vec4(6.0);
    gx11 = fract(gx11) - vec4(0.5);
    gy11 = fract(gy11) - vec4(0.5);
    gz11 = fract(gz11) - vec4(0.5);
    vec4 gw11 = vec4(0.75) - abs(gx11) - abs(gy11) - abs(gz11);
    vec4 sw11 = step(gw11, vec4(0.0));
    gx11 -= sw11 * (step(vec4(0.0), gx11) - vec4(0.5));
    gy11 -= sw11 * (step(vec4(0.0), gy11) - vec4(0.5));

    vec4 g0000 = vec4(gx00.x, gy00.x, gz00.x, gw00.x);
    vec4 g1000 = vec4(gx00.y, gy00.y, gz00.y, gw00.y);
    vec4 g0100 = vec4(gx00.z, gy00.z, gz00.z, gw00.z);
    vec4 g1100 = vec4(gx00.w, gy00.w, gz00.w, gw00.w);
    vec4 g0010 = vec4(gx10.x, gy10.x, gz10.x, gw10.x);
    vec4 g1010 = vec4(gx10.y, gy10.y, gz10.y, gw10.y);
    vec4 g0110 = vec4(gx10.z, gy10.z, gz10.z, gw10.z);
    vec4 g1110 = vec4(gx10.w, gy10.w, gz10.w, gw10.w);
    vec4 g0001 = vec4(gx01.x, gy01.x, gz01.x, gw01.x);
    vec4 g1001 = vec4(gx01.y, gy01.y, gz01.y, gw01.y);
    vec4 g0101 = vec4(gx01.z, gy01.z, gz01.z, gw01.z);
    vec4 g1101 = vec4(gx01.w, gy01.w, gz01.w, gw01.w);
    vec4 g0011 = vec4(gx11.x, gy11.x, gz11.x, gw11.x);
    vec4 g1011 = vec4(gx11.y, gy11.y, gz11.y, gw11.y);
    vec4 g0111 = vec4(gx11.z, gy11.z, gz11.z, gw11.z);
    vec4 g1111 = vec4(gx11.w, gy11.w, gz11.w, gw11.w);

    vec4 norm00 = cloud_taylor_inv_sqrt(
        vec4(dot(g0000, g0000), dot(g0100, g0100), dot(g1000, g1000), dot(g1100, g1100)));
    g0000 *= norm00.x;
    g0100 *= norm00.y;
    g1000 *= norm00.z;
    g1100 *= norm00.w;

    vec4 norm01 = cloud_taylor_inv_sqrt(
        vec4(dot(g0001, g0001), dot(g0101, g0101), dot(g1001, g1001), dot(g1101, g1101)));
    g0001 *= norm01.x;
    g0101 *= norm01.y;
    g1001 *= norm01.z;
    g1101 *= norm01.w;

    vec4 norm10 = cloud_taylor_inv_sqrt(
        vec4(dot(g0010, g0010), dot(g0110, g0110), dot(g1010, g1010), dot(g1110, g1110)));
    g0010 *= norm10.x;
    g0110 *= norm10.y;
    g1010 *= norm10.z;
    g1110 *= norm10.w;

    vec4 norm11 = cloud_taylor_inv_sqrt(
        vec4(dot(g0011, g0011), dot(g0111, g0111), dot(g1011, g1011), dot(g1111, g1111)));
    g0011 *= norm11.x;
    g0111 *= norm11.y;
    g1011 *= norm11.z;
    g1111 *= norm11.w;

    float n0000 = dot(g0000, pf0);
    float n1000 = dot(g1000, vec4(pf1.x, pf0.y, pf0.z, pf0.w));
    float n0100 = dot(g0100, vec4(pf0.x, pf1.y, pf0.z, pf0.w));
    float n1100 = dot(g1100, vec4(pf1.x, pf1.y, pf0.z, pf0.w));
    float n0010 = dot(g0010, vec4(pf0.x, pf0.y, pf1.z, pf0.w));
    float n1010 = dot(g1010, vec4(pf1.x, pf0.y, pf1.z, pf0.w));
    float n0110 = dot(g0110, vec4(pf0.x, pf1.y, pf1.z, pf0.w));
    float n1110 = dot(g1110, vec4(pf1.x, pf1.y, pf1.z, pf0.w));
    float n0001 = dot(g0001, vec4(pf0.x, pf0.y, pf0.z, pf1.w));
    float n1001 = dot(g1001, vec4(pf1.x, pf0.y, pf0.z, pf1.w));
    float n0101 = dot(g0101, vec4(pf0.x, pf1.y, pf0.z, pf1.w));
    float n1101 = dot(g1101, vec4(pf1.x, pf1.y, pf0.z, pf1.w));
    float n0011 = dot(g0011, vec4(pf0.x, pf0.y, pf1.z, pf1.w));
    float n1011 = dot(g1011, vec4(pf1.x, pf0.y, pf1.z, pf1.w));
    float n0111 = dot(g0111, vec4(pf0.x, pf1.y, pf1.z, pf1.w));
    float n1111 = dot(g1111, pf1);

    vec4 fade_xyzw = cloud_fade(pf0);
    vec4 n_0w = mix(vec4(n0000, n1000, n0100, n1100),
                    vec4(n0001, n1001, n0101, n1101), fade_xyzw.w);
    vec4 n_1w = mix(vec4(n0010, n1010, n0110, n1110),
                    vec4(n0011, n1011, n0111, n1111), fade_xyzw.w);
    vec4 n_zw = mix(n_0w, n_1w, fade_xyzw.z);
    vec2 n_yzw = mix(vec2(n_zw.x, n_zw.y), vec2(n_zw.z, n_zw.w), fade_xyzw.y);
    return 2.2 * mix(n_yzw.x, n_yzw.y, fade_xyzw.x);
}

float cloud_remap(float value, float old_min, float old_max, float new_min, float new_max) {
    return new_min + (((value - old_min) / (old_max - old_min)) * (new_max - new_min));
}

float cloud_worley_noise_3d(vec3 p, float cell_count) {
    return cloud_cells(p, cell_count);
}

float cloud_perlin_noise_3d(vec3 p_in, float frequency, int octave_count) {
    float sum = 0.0;
    float weight_sum = 0.0;
    float weight = 0.5;
    for (int oct = 0; oct < octave_count; ++oct) {
        vec4 p = vec4(p_in.x, p_in.y, p_in.z, 0.0) * vec4(frequency);
        float value = cloud_glm_perlin_4d(p, vec4(frequency));
        sum += value * weight;
        weight_sum += weight;
        weight *= weight;
        frequency *= 2.0;
    }
    return clamp(sum / weight_sum, 0.0, 1.0);
}

const vec3 kCloudWeatherSeed = vec3(0.0);

float cloud_random_2d(vec2 st) {
    return fract(sin(dot(st.xy, vec2(12.9898, 78.233) + kCloudWeatherSeed.xy)) *
                 43758.5453123);
}

float cloud_noise_interpolation_2d(vec2 coord, float size) {
    vec2 grid = coord * size;
    vec2 random_input = floor(grid);
    vec2 weights = fract(grid);

    float p0 = cloud_random_2d(random_input);
    float p1 = cloud_random_2d(random_input + vec2(1.0, 0.0));
    float p2 = cloud_random_2d(random_input + vec2(0.0, 1.0));
    float p3 = cloud_random_2d(random_input + vec2(1.0, 1.0));

    weights = smoothstep(vec2(0.0), vec2(1.0), weights);
    return p0 + (p1 - p0) * weights.x + (p2 - p0) * weights.y * (1.0 - weights.x) +
           (p3 - p1) * weights.y * weights.x;
}

float cloud_perlin_noise_2d(vec2 uv, float scale, float frequency, float amplitude,
                                int octaves) {
    float noise_value = 0.0;
    float local_amplitude = amplitude;
    float local_frequency = frequency;
    for (int index = 0; index < octaves; ++index) {
        noise_value += cloud_noise_interpolation_2d(uv, scale * local_frequency) *
                       local_amplitude;
        local_amplitude *= 0.25;
        local_frequency *= 3.0;
    }
    return noise_value * noise_value;
}

#endif
