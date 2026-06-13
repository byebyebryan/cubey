#ifndef CUBEY_CLOUDS_MODEL_GLSL
#define CUBEY_CLOUDS_MODEL_GLSL

const float CLOUDS_PI = 3.14159265359;
const float CLOUDS_STYLE_FAIR_WEATHER = 0.0;
const float CLOUDS_STYLE_BROKEN_CUMULUS = 1.0;
const float CLOUDS_STYLE_OVERCAST_STRATUS = 2.0;
const float CLOUDS_STYLE_STORM_CELLS = 3.0;
const float CLOUDS_STYLE_HIGH_CIRRUS = 4.0;

float cloud_style_id() {
    return floor(params.cloud_shell.w + 0.5);
}

float cloud_style_value(float fair_weather, float broken_cumulus, float overcast_stratus,
                        float storm_cells, float high_cirrus) {
    float style = cloud_style_id();
    if (style < 0.5) {
        return fair_weather;
    }
    if (style < 1.5) {
        return broken_cumulus;
    }
    if (style < 2.5) {
        return overcast_stratus;
    }
    if (style < 3.5) {
        return storm_cells;
    }
    return high_cirrus;
}

float hash31(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float value_noise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);

    float c000 = hash31(i + vec3(0.0, 0.0, 0.0));
    float c100 = hash31(i + vec3(1.0, 0.0, 0.0));
    float c010 = hash31(i + vec3(0.0, 1.0, 0.0));
    float c110 = hash31(i + vec3(1.0, 1.0, 0.0));
    float c001 = hash31(i + vec3(0.0, 0.0, 1.0));
    float c101 = hash31(i + vec3(1.0, 0.0, 1.0));
    float c011 = hash31(i + vec3(0.0, 1.0, 1.0));
    float c111 = hash31(i + vec3(1.0, 1.0, 1.0));

    float x00 = mix(c000, c100, u.x);
    float x10 = mix(c010, c110, u.x);
    float x01 = mix(c001, c101, u.x);
    float x11 = mix(c011, c111, u.x);
    float y0 = mix(x00, x10, u.y);
    float y1 = mix(x01, x11, u.y);
    return mix(y0, y1, u.z);
}

float fbm(vec3 p) {
    float sum = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 5; ++i) {
        sum += value_noise(p) * amp;
        p = p * 2.03 + vec3(17.13, 9.21, 3.77);
        amp *= 0.5;
    }
    return sum;
}

vec2 ray_sphere(vec3 origin, vec3 direction, vec3 center, float radius) {
    return cubey_atmosphere_ray_sphere_intersection(origin, direction, center, radius);
}

vec3 planet_center() {
    return vec3(0.0, -params.camera_position_radius.w, 0.0);
}

vec3 view_direction() {
    vec3 right = params.camera_right_aspect.xyz;
    vec3 up = params.camera_up_tan_half_fovy.xyz;
    vec3 forward = params.camera_forward_mode.xyz;
    float aspect = params.camera_right_aspect.w;
    float tan_half_fovy = params.camera_up_tan_half_fovy.w;
    return normalize(forward + right * frag_position.x * aspect * tan_half_fovy -
                     up * frag_position.y * tan_half_fovy);
}

float cloud_normalized_height(float altitude_km) {
    float bottom = params.cloud_shell.x;
    float top = params.cloud_shell.y;
    return clamp((altitude_km - bottom) / max(top - bottom, 0.001), 0.0, 1.0);
}

float cloud_height_profile(float altitude_km) {
    float h = cloud_normalized_height(altitude_km);
    float low_base = smoothstep(0.03, 0.16, h);
    float thin_base = smoothstep(0.10, 0.30, h);
    float cumulus_body = pow(max(sin(h * CLOUDS_PI), 0.0), 0.45);

    float fair_weather = low_base * (1.0 - smoothstep(0.48, 0.88, h)) *
                         mix(0.64, 1.08, cumulus_body);
    float broken_cumulus = low_base * (1.0 - smoothstep(0.56, 1.0, h)) *
                           mix(0.72, 1.20, cumulus_body);
    float overcast_stratus = smoothstep(0.02, 0.12, h) *
                             (1.0 - smoothstep(0.76, 1.0, h)) * mix(1.02, 0.74, h);
    float storm_cells = smoothstep(0.01, 0.10, h) * (1.0 - smoothstep(0.94, 1.0, h)) *
                        mix(0.76, 1.48, pow(h, 0.42));
    float high_cirrus = thin_base * (1.0 - smoothstep(0.74, 1.0, h)) *
                        mix(0.38, 0.86, smoothstep(0.26, 0.66, h));

    return cloud_style_value(fair_weather, broken_cumulus, overcast_stratus, storm_cells,
                             high_cirrus);
}

vec3 cloud_wind_offset() {
    return vec3(params.weather.w * 0.004, params.weather.w * 0.0017,
                -params.weather.w * 0.0028);
}

float cloud_weather_frequency() {
    float scale = max(params.weather.z, 0.001);
    return clamp(params.camera_position_radius.w / (scale * 3.5), 0.75, 32.0);
}

vec3 cloud_weather_domain(vec3 position_km, float frequency, vec3 wind_offset) {
    vec3 center = planet_center();
    vec3 up = normalize(position_km - center);
    vec3 warp =
        vec3(fbm(up * (frequency * 0.33) + wind_offset + vec3(11.0, 3.0, 7.0)),
             fbm(up * (frequency * 0.29) - wind_offset.yzx + vec3(23.0, 19.0, 5.0)),
             fbm(up * (frequency * 0.37) + wind_offset.zxy + vec3(2.0, 29.0, 31.0))) -
        vec3(0.5);
    return normalize(up + warp * 0.30);
}

float weather_coverage(vec3 position_km) {
    float frequency = cloud_weather_frequency();
    float orbit_view = smoothstep(1.5, 2.0, params.camera_forward_mode.w);
    vec3 wind_offset = cloud_wind_offset();
    vec3 domain = cloud_weather_domain(position_km, frequency, wind_offset);

    float macro = fbm(domain * (frequency * 0.74) + wind_offset * 0.62);
    float secondary =
        fbm(domain.yzx * (frequency * 1.35) - wind_offset.zxy + vec3(17.0, 5.0, 13.0));
    float broad = mix(macro, secondary, 0.28);

    float front_wave =
        0.5 + 0.5 * sin(dot(domain, normalize(vec3(0.62, 0.17, -0.77))) * frequency * 0.88 +
                          params.weather.w * 0.006);
    float fronts = smoothstep(0.22, 0.84, mix(front_wave, broad, 0.48));

    float cell_seed =
        fbm(domain.zxy * (frequency * 2.15) + wind_offset.xzy + vec3(41.0, 7.0, 13.0));
    float cell_mod =
        fbm(domain.xyz * (frequency * 0.92) - wind_offset * 0.35 + vec3(5.0, 31.0, 2.0));
    float cells = smoothstep(0.47, 0.88, cell_seed) * mix(0.70, 1.24, cell_mod);

    float streak_phase =
        dot(domain, normalize(vec3(0.85, 0.06, 0.52))) * frequency * 4.2 +
        params.weather.w * 0.012 + secondary * 5.0;
    float streak_wave = 0.5 + 0.5 * sin(streak_phase);
    float streaks = smoothstep(0.35, 0.94, streak_wave * mix(0.72, 1.16, broad));

    float calm = smoothstep(0.60, 0.91,
                            fbm(domain.xzy * (frequency * 0.48) -
                                wind_offset.zyx * 0.42 + vec3(9.0, 27.0, 3.0)));

    float fair_weather = mix(broad, cells, 0.30) * (1.0 - calm * 0.48);
    float broken_cumulus = mix(mix(broad, fronts, 0.40), cells, mix(0.33, 0.08, orbit_view)) *
                           (1.0 - calm * 0.30);
    float overcast_stratus = mix(fronts, broad, 0.22) * (1.0 - calm * 0.14);
    float storm_cells = max(fronts * 0.84, cells * 1.08) * (1.0 - calm * 0.22);
    float high_cirrus = mix(streaks, fronts, 0.18) * (1.0 - calm * 0.25);

    return clamp(cloud_style_value(fair_weather, broken_cumulus, overcast_stratus, storm_cells,
                                   high_cirrus),
                 0.0, 1.0);
}

struct CloudDensityContext {
    float sample_distance;
    float step_length;
    float grazing;
    float orbit_view;
    float local_view;
    float distance_fraction;
};

struct CloudDensitySample {
    float density;
    float base_density;
    float detail_density;
    float detail_lod;
    float weather;
    float height;
};

CloudDensityContext cloud_density_context(float sample_distance, float step_length,
                                          float grazing, float distance_fraction) {
    CloudDensityContext context;
    context.sample_distance = max(sample_distance, 0.0);
    context.step_length = max(step_length, 0.0);
    context.grazing = clamp(grazing, 0.0, 1.0);
    context.orbit_view = smoothstep(1.5, 2.0, params.camera_forward_mode.w);
    context.local_view = 1.0 - context.orbit_view;
    context.distance_fraction = clamp(distance_fraction, 0.0, 1.0);
    return context;
}

CloudDensityContext cloud_density_default_context() {
    return cloud_density_context(0.0, 0.0, 1.0, 0.0);
}

CloudDensitySample empty_cloud_density_sample() {
    CloudDensitySample density_result;
    density_result.density = 0.0;
    density_result.base_density = 0.0;
    density_result.detail_density = 0.0;
    density_result.detail_lod = 1.0;
    density_result.weather = 0.0;
    density_result.height = 0.0;
    return density_result;
}

CloudDensitySample cloud_density_sample(vec3 position_km, CloudDensityContext context) {
    CloudDensitySample density_result = empty_cloud_density_sample();
    float altitude = length(position_km - planet_center()) - params.camera_position_radius.w;
    float height = cloud_height_profile(altitude);
    density_result.height = height;
    if (height <= 0.0) {
        return density_result;
    }

    float h = cloud_normalized_height(altitude);
    float weather = weather_coverage(position_km);
    density_result.weather = weather;
    float coverage = clamp(params.weather.x, 0.0, 1.0);
    float orbit_view = context.orbit_view;
    float distance_suppression = smoothstep(80.0, 340.0, context.sample_distance);
    float footprint_suppression = smoothstep(2.5, 14.0, context.step_length);
    float grazing_support = smoothstep(0.04, 0.30, context.grazing);
    float local_suppression = max(distance_suppression * 0.92, footprint_suppression * 0.86);
    float local_detail_lod =
        clamp(min(1.0 - local_suppression, mix(0.04, 1.0, grazing_support)), 0.02, 1.0);
    float detail_lod = mix(local_detail_lod, 1.0, orbit_view);
    density_result.detail_lod = detail_lod;

    float threshold =
        cloud_style_value(0.44, 0.34, 0.25, 0.23, 0.48) +
        (1.0 - coverage) * cloud_style_value(0.42, 0.36, 0.28, 0.25, 0.34);
    float softness = cloud_style_value(0.18, 0.22, 0.34, 0.18, 0.30);
    float coverage_mask = smoothstep(threshold, threshold + softness, weather);

    float detail_scale =
        mix(cloud_style_value(0.54, 0.42, 0.18, 0.34, 0.10), 0.055, orbit_view);
    vec3 detail_coord = position_km * detail_scale + vec3(params.weather.w * 0.03, 13.5, 0.0);
    float detail = fbm(detail_coord);
    float puffy = smoothstep(0.34, 0.78, detail);
    float sheets = smoothstep(0.18, 0.76, mix(detail, weather, 0.66));
    float storm_core = smoothstep(0.42, 0.82, mix(detail, weather, 0.54));
    float wisps =
        smoothstep(0.38, 0.84,
                   fbm(position_km * mix(0.11, 0.035, orbit_view) +
                       vec3(params.weather.w * 0.055, 19.0, 7.0)));

    float base_puffy = smoothstep(0.18, 0.78, weather);
    float base_sheets = coverage_mask;
    float base_storm_core = max(coverage_mask, weather);
    float base_wisps = weather;

    puffy = mix(puffy, smoothstep(0.18, 0.78, weather), orbit_view * 0.75);
    sheets = mix(sheets, coverage_mask, orbit_view * 0.68);
    storm_core = mix(storm_core, max(coverage_mask, weather), orbit_view * 0.58);
    wisps = mix(wisps, weather, orbit_view * 0.42);

    float fair_weather =
        smoothstep(0.08, 0.72, coverage_mask * 0.58 + puffy * 0.56) *
        mix(0.66, 1.16, puffy);
    float broken_cumulus =
        smoothstep(0.07, 0.74, coverage_mask * 0.72 + puffy * 0.48) *
        mix(0.62, 1.22, puffy);
    float overcast_stratus = mix(coverage_mask, sheets, 0.22) * mix(0.92, 0.72, h);
    float storm_cells =
        smoothstep(0.05, 0.78, max(coverage_mask * 0.84, storm_core * 0.78)) *
        mix(0.90, 1.42, storm_core * smoothstep(0.18, 0.88, h));
    float high_cirrus = smoothstep(0.08, 0.70, coverage_mask * 0.55 + wisps * 0.62) *
                        mix(0.38, 0.76, wisps);

    float edge = cloud_style_value(fair_weather, broken_cumulus, overcast_stratus, storm_cells,
                                   high_cirrus);
    float base_fair_weather =
        smoothstep(0.08, 0.72, coverage_mask * 0.58 + base_puffy * 0.56) *
        mix(0.66, 1.16, base_puffy);
    float base_broken_cumulus =
        smoothstep(0.07, 0.74, coverage_mask * 0.72 + base_puffy * 0.48) *
        mix(0.62, 1.22, base_puffy);
    float base_overcast_stratus = mix(coverage_mask, base_sheets, 0.22) * mix(0.92, 0.72, h);
    float base_storm_cells =
        smoothstep(0.05, 0.78, max(coverage_mask * 0.84, base_storm_core * 0.78)) *
        mix(0.90, 1.42, base_storm_core * smoothstep(0.18, 0.88, h));
    float base_high_cirrus =
        smoothstep(0.08, 0.70, coverage_mask * 0.55 + base_wisps * 0.62) *
        mix(0.38, 0.76, base_wisps);
    float base_edge =
        cloud_style_value(base_fair_weather, base_broken_cumulus, base_overcast_stratus,
                          base_storm_cells, base_high_cirrus);

    float scallop_scale =
        mix(cloud_style_value(0.82, 0.78, 0.24, 0.62, 0.13), 0.075, orbit_view);
    float scallop =
        fbm(position_km * scallop_scale - vec3(params.weather.w * 0.016, 5.0, 19.0));
    float erosion = cloud_style_value(mix(0.58, 1.08, puffy), mix(0.20, 1.22, puffy),
                                      mix(0.86, 1.05, sheets), mix(0.72, 1.28, storm_core),
                                      mix(0.30, 0.82, wisps));
    erosion *= mix(0.72, 1.12, scallop);
    erosion = mix(erosion, 0.92, orbit_view * 0.64);

    float base_density = max(base_edge * height * 0.92 * params.weather.y, 0.0);
    float detail_density = max(edge * height * erosion * params.weather.y, 0.0);
    density_result.base_density = base_density;
    density_result.detail_density = detail_density;
    density_result.density = mix(base_density, detail_density, detail_lod);
    return density_result;
}

float cloud_density(vec3 position_km) {
    return cloud_density_sample(position_km, cloud_density_default_context()).density;
}

#endif
