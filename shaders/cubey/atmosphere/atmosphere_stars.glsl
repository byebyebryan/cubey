#ifndef CUBEY_ATMOSPHERE_STARS_GLSL
#define CUBEY_ATMOSPHERE_STARS_GLSL

const float CUBEY_STAR_PI = 3.14159265359;
const float CUBEY_STAR_TAU = 6.28318530718;

struct StarSampleContext {
    vec3 direction;
    vec2 equal_area_uv;
    float pixel_radius;
    float pixel_area;
};

float galactic_star_density(vec3 sky_direction) {
    vec3 pole = normalize(vec3(0.31, 0.84, 0.44));
    vec3 center_hint = normalize(vec3(-0.45, -0.12, -0.89));
    vec3 center = normalize(center_hint - pole * dot(center_hint, pole));
    vec3 tangent = normalize(cross(pole, center));
    float latitude = asin(clamp(dot(sky_direction, pole), -1.0, 1.0));
    float longitude = atan(dot(sky_direction, tangent), dot(sky_direction, center));
    float band = exp(-abs(latitude) * 10.5);
    float core = exp(-(longitude * longitude) / 0.36 - (latitude * latitude) / 0.040);
    return clamp(0.32 + band * 1.05 + core * 1.25, 0.25, 2.45);
}

vec3 star_temperature_color(float seed) {
    vec3 warm = cubey_srgb_to_linear(vec3(1.0, 0.86, 0.68));
    vec3 neutral = cubey_srgb_to_linear(vec3(0.95, 0.96, 1.0));
    vec3 cool = cubey_srgb_to_linear(vec3(0.78, 0.84, 1.0));
    float warm_weight = (1.0 - smoothstep(0.08, 0.42, seed)) * 0.45;
    float cool_weight = smoothstep(0.76, 0.98, seed) * 0.42;
    return mix(mix(neutral, warm, warm_weight), cool, cool_weight);
}

float star_sample_magnitude(float seed, float bright_magnitude, float faint_magnitude,
                            float faint_bias) {
    float t = 1.0 - pow(1.0 - clamp(seed, 0.0, 0.999), faint_bias);
    return mix(bright_magnitude, faint_magnitude, t);
}

float star_magnitude_to_radiance(float magnitude) {
    return pow(10.0, -0.4 * (magnitude - 1.0));
}

float star_magnitude_weight(float magnitude, float limiting_magnitude, float fade_width) {
    return 1.0 - smoothstep(limiting_magnitude - fade_width,
                            limiting_magnitude + fade_width, magnitude);
}

vec2 star_equal_area_uv(vec3 sky_direction) {
    float longitude = atan(sky_direction.z, sky_direction.x) / CUBEY_STAR_TAU + 0.5;
    return vec2(fract(longitude), clamp(sky_direction.y * 0.5 + 0.5, 0.0, 0.999999));
}

StarSampleContext star_sample_context(vec3 sky_direction) {
    float pixel_x = max(length(dFdx(sky_direction)), 1e-6);
    float pixel_y = max(length(dFdy(sky_direction)), 1e-6);
    float pixel_area = max(pixel_x * pixel_y, 1e-10);
    return StarSampleContext(sky_direction, star_equal_area_uv(sky_direction),
                             sqrt(pixel_area), pixel_area);
}

ivec2 star_wrap_spherical_cell(ivec2 cell, ivec2 dimensions) {
    int x = cell.x;
    int y = cell.y;
    if (y < 0) {
        y = -y - 1;
        x += dimensions.x / 2;
    } else if (y >= dimensions.y) {
        y = dimensions.y * 2 - y - 1;
        x += dimensions.x / 2;
    }
    x %= dimensions.x;
    if (x < 0) {
        x += dimensions.x;
    }
    return ivec2(x, y);
}

vec3 star_cell_direction(ivec2 cell, ivec2 dimensions, vec2 seed) {
    vec2 jitter = vec2(cubey_proc_hash_pcg_2d(seed + 19.17),
                       cubey_proc_hash_pcg_2d(seed + 71.31));
    vec2 equal_area = (vec2(cell) + jitter) / vec2(dimensions);
    float y = equal_area.y * 2.0 - 1.0;
    float longitude = equal_area.x * CUBEY_STAR_TAU - CUBEY_STAR_PI;
    float horizontal = sqrt(max(1.0 - y * y, 0.0));
    return vec3(cos(longitude) * horizontal, y, sin(longitude) * horizontal);
}

float star_angular_point_spread(float chord_distance, float native_sigma, float halo_strength,
                                float pixel_radius, float pixel_area) {
    float sigma = max(native_sigma, pixel_radius * 0.65);
    float sigma2 = max(sigma * sigma, 1e-10);
    float core = exp(-0.5 * chord_distance * chord_distance / sigma2) *
                 pixel_area / (CUBEY_STAR_TAU * sigma2);

    float halo_sigma = max(native_sigma * 3.2, pixel_radius * 1.25);
    float halo_sigma2 = max(halo_sigma * halo_sigma, 1e-10);
    float halo = exp(-0.5 * chord_distance * chord_distance / halo_sigma2) *
                 pixel_area / (CUBEY_STAR_TAU * halo_sigma2);
    return core + halo * halo_strength;
}

vec3 star_spherical_candidate_radiance(StarSampleContext sample_context, ivec2 cell,
                                       ivec2 dimensions,
                                       float probability, float bright_magnitude,
                                       float faint_magnitude, float magnitude_bias,
                                       float min_radius_fraction, float max_radius_fraction,
                                       float halo_strength, float limiting_magnitude,
                                       float cell_angle, vec2 layer_seed) {
    ivec2 wrapped_cell = star_wrap_spherical_cell(cell, dimensions);
    vec2 seed = vec2(wrapped_cell) + layer_seed;
    if (cubey_proc_hash_pcg_2d(seed) >= clamp(probability, 0.0, 0.75)) {
        return vec3(0.0);
    }

    float magnitude = star_sample_magnitude(cubey_proc_hash_pcg_2d(seed + 113.7),
                                            bright_magnitude, faint_magnitude, magnitude_bias);
    float magnitude_weight = star_magnitude_weight(magnitude, limiting_magnitude, 0.38);
    if (magnitude_weight <= 0.0) {
        return vec3(0.0);
    }

    vec3 candidate_direction = star_cell_direction(wrapped_cell, dimensions, seed);
    float brightness = clamp((faint_magnitude - magnitude) /
                                 max(faint_magnitude - bright_magnitude, 0.001),
                             0.0, 1.0);
    float radius_fraction =
        mix(min_radius_fraction, max_radius_fraction, pow(brightness, 1.7));
    float native_sigma = radius_fraction * cell_angle;
    float halo = halo_strength * pow(brightness, 1.4);
    float point = star_angular_point_spread(length(sample_context.direction - candidate_direction),
                                            native_sigma, halo, sample_context.pixel_radius,
                                            sample_context.pixel_area);
    float radiance = star_magnitude_to_radiance(magnitude) * magnitude_weight;
    return star_temperature_color(cubey_proc_hash_pcg_2d(seed + 211.9)) * point * radiance;
}

vec3 star_spherical_population_radiance(StarSampleContext sample_context, ivec2 dimensions,
                                        float probability, float bright_magnitude,
                                        float faint_magnitude, float magnitude_bias,
                                        float min_radius_fraction, float max_radius_fraction,
                                        float halo_strength, float limiting_magnitude,
                                        float population_gain, vec2 layer_seed) {
    vec2 star_uv = sample_context.equal_area_uv * vec2(dimensions);
    ivec2 base_cell = ivec2(floor(star_uv));
    vec2 local = fract(star_uv);
    float cell_angle = sqrt(4.0 * CUBEY_STAR_PI /
                            float(dimensions.x * dimensions.y));
    float support = clamp(max_radius_fraction * 4.0 +
                              sample_context.pixel_radius * 3.0 / cell_angle,
                          0.04, 0.50);

    ivec2 neighbor_offset = ivec2(local.x < 0.5 ? -1 : 1,
                                  local.y < 0.5 ? -1 : 1);
    bvec2 sample_neighbor = lessThan(min(local, 1.0 - local), vec2(support));
    vec3 radiance = vec3(0.0);
    for (int neighbor_y = 0; neighbor_y <= 1; ++neighbor_y) {
        if (neighbor_y > 0 && !sample_neighbor.y) {
            continue;
        }
        int y = neighbor_y * neighbor_offset.y;
        for (int neighbor_x = 0; neighbor_x <= 1; ++neighbor_x) {
            if (neighbor_x > 0 && !sample_neighbor.x) {
                continue;
            }
            int x = neighbor_x * neighbor_offset.x;
            radiance += star_spherical_candidate_radiance(
                sample_context, base_cell + ivec2(x, y), dimensions, probability,
                bright_magnitude, faint_magnitude, magnitude_bias, min_radius_fraction,
                max_radius_fraction, halo_strength, limiting_magnitude, cell_angle,
                layer_seed);
        }
    }
    return radiance * population_gain;
}

vec3 anchor_star_radiance(StarSampleContext sample_context, float limiting_magnitude,
                          float galactic_density_value) {
    float density = clamp(atmosphere.night_options.w, 0.0, 1.0);
    float galactic_bias = mix(0.85, 1.15, clamp((galactic_density_value - 0.25) /
                                                   2.20,
                                               0.0, 1.0));
    float probability = clamp(mix(0.002, 0.018, density) * galactic_bias, 0.0, 0.030);
    return star_spherical_population_radiance(
        sample_context, ivec2(192, 64), probability, -1.2, 2.1, 1.45,
        0.008, 0.022, 0.18, limiting_magnitude, 0.12, vec2(0.0, 0.0));
}

vec3 naked_eye_star_radiance(StarSampleContext sample_context, float limiting_magnitude,
                             float galactic_density_value) {
    float density = clamp(atmosphere.night_options.w, 0.0, 1.0);
    float galactic_bias = mix(0.72, 1.55, clamp((galactic_density_value - 0.25) /
                                                   2.20,
                                               0.0, 1.0));
    float probability = clamp(mix(0.020, 0.145, density) * galactic_bias, 0.0, 0.26);
    return star_spherical_population_radiance(
        sample_context, ivec2(384, 128), probability, 1.3, 6.4, 1.60,
        0.006, 0.015, 0.025, limiting_magnitude, 0.10, vec2(173.0, 311.0));
}

vec3 bright_star_radiance(StarSampleContext sample_context, float limiting_magnitude,
                          float galactic_density_value) {
    return anchor_star_radiance(sample_context, limiting_magnitude, galactic_density_value) +
           naked_eye_star_radiance(sample_context, limiting_magnitude, galactic_density_value);
}

vec3 faint_star_radiance(StarSampleContext sample_context, float limiting_magnitude,
                         float galactic_density_value) {
    float density = clamp(atmosphere.night_options.w, 0.0, 1.0);
    float probability = clamp(mix(0.015, 0.120, density) *
                                  mix(0.42, 1.85,
                                      clamp((galactic_density_value - 0.25) / 2.20, 0.0, 1.0)),
                              0.0, 0.34);
    return star_spherical_population_radiance(
               sample_context, ivec2(768, 256), probability, 4.8, 8.0, 1.30,
               0.004, 0.010, 0.0, limiting_magnitude, 0.18, vec2(1193.0, 631.0)) *
           mix(0.82, 1.15, clamp(galactic_density_value * 0.5, 0.0, 1.0));
}

vec3 star_field_radiance(vec3 sky_direction, float limiting_magnitude, float camera_mode) {
    StarSampleContext sample_context = star_sample_context(sky_direction);
    float galactic_density_value = galactic_star_density(sky_direction);
    vec3 radiance =
        bright_star_radiance(sample_context, limiting_magnitude, galactic_density_value);
    if (camera_mode > 0.5) {
        radiance +=
            faint_star_radiance(sample_context, limiting_magnitude, galactic_density_value);
    }
    return radiance;
}

#endif // CUBEY_ATMOSPHERE_STARS_GLSL
