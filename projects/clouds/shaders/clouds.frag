#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/atmosphere.glsl"
#include "cubey/pbr.glsl"

const float CLOUDS_PI = 3.14159265359;
const int CLOUDS_MAX_VIEW_STEPS = 64;
const int CLOUDS_MAX_LIGHT_STEPS = 8;

const int CLOUDS_VIEW_FINAL = 0;
const int CLOUDS_VIEW_WEATHER = 1;
const int CLOUDS_VIEW_DENSITY = 2;
const int CLOUDS_VIEW_TRANSMITTANCE = 3;
const int CLOUDS_VIEW_LIGHTING = 4;
const int CLOUDS_VIEW_SHADOW = 5;
const int CLOUDS_VIEW_STEPS = 6;

layout(push_constant) uniform CloudsParams {
    vec4 camera_right_aspect;
    vec4 camera_up_tan_half_fovy;
    vec4 camera_forward_mode;
    vec4 camera_position_radius;
    vec4 cloud_shell;
    vec4 weather;
    vec4 sun_direction_intensity;
    vec4 render_options;
} params;

layout(location = 0) in vec2 frag_position;
layout(location = 0) out vec4 out_color;

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

CubeyAtmosphereMedium atmosphere_medium() {
    float ground_radius = params.camera_position_radius.w;
    float top_radius = ground_radius + params.cloud_shell.z;
    return CubeyAtmosphereMedium(
        planet_center(),
        ground_radius,
        top_radius,
        vec3(0.005802, 0.013558, 0.033100),
        8.0,
        0.003996,
        0.004400,
        1.2,
        0.80,
        vec3(0.000650, 0.001881, 0.000085),
        25.0,
        15.0,
        normalize(params.sun_direction_intensity.xyz),
        0.004675,
        vec3(22.0),
        0.022);
}

vec3 ground_color(vec3 position, vec3 direction) {
    vec3 up = normalize(position - planet_center());
    float ocean = smoothstep(-0.15, 0.25, fbm(up * 9.0 + vec3(4.0, 1.0, 8.0)));
    vec3 ocean_color = vec3(0.015, 0.07, 0.12);
    vec3 land_color = vec3(0.18, 0.20, 0.11);
    float ndotl = max(dot(up, normalize(params.sun_direction_intensity.xyz)), 0.0);
    vec3 base = mix(ocean_color, land_color, ocean);
    vec3 ambient = vec3(0.018, 0.022, 0.030);
    return base * (ambient + vec3(0.75, 0.70, 0.62) * ndotl * params.sun_direction_intensity.w);
}

vec3 sky_color(vec3 origin, vec3 direction) {
    CubeyAtmosphereMedium medium = atmosphere_medium();
    CubeyAtmosphereRaySegment segment = cubey_atmosphere_classify_ray(medium, origin, direction, -1.0);
    CubeyAtmosphereSample sky_sample =
        cubey_atmosphere_integrate_view(medium, origin, direction, -1.0);
    vec3 color = sky_sample.color;
    if (segment.hit_ground && segment.ground_t > 0.0) {
        color += sky_sample.transmittance *
                 ground_color(origin + direction * segment.ground_t, direction);
    }
    return color;
}

float cloud_height_profile(float altitude_km) {
    float bottom = params.cloud_shell.x;
    float top = params.cloud_shell.y;
    float h = clamp((altitude_km - bottom) / max(top - bottom, 0.001), 0.0, 1.0);
    float base = smoothstep(0.02, 0.20, h);
    float anvil = smoothstep(1.0, 0.58, h);
    return base * anvil;
}

float weather_coverage(vec3 position_km) {
    vec3 center = planet_center();
    vec3 up = normalize(position_km - center);
    float scale = max(params.weather.z, 0.001);
    vec3 coord = vec3(position_km.xz / scale, 0.0).xzy;
    coord += up * 2.0;
    coord.x += params.weather.w * 0.020;
    coord.z -= params.weather.w * 0.011;
    float macro = fbm(coord);
    float bands = 0.5 + 0.5 * sin((up.x * 3.0 + up.z * 5.0) + params.weather.w * 0.03);
    return clamp(mix(macro, bands, 0.22), 0.0, 1.0);
}

float cloud_density(vec3 position_km) {
    float altitude = length(position_km - planet_center()) - params.camera_position_radius.w;
    float height = cloud_height_profile(altitude);
    if (height <= 0.0) {
        return 0.0;
    }
    float weather = weather_coverage(position_km);
    float coverage = clamp(params.weather.x, 0.0, 1.0);
    float coverage_mask = smoothstep(1.0 - coverage, 1.0, weather);
    vec3 detail_coord = position_km * 0.42 + vec3(params.weather.w * 0.03, 13.5, 0.0);
    float detail = fbm(detail_coord);
    float erosion = smoothstep(0.18, 0.88, detail);
    float puffy = mix(0.55, 1.25, erosion);
    return max(coverage_mask * height * puffy * params.weather.y, 0.0);
}

float light_transmittance(vec3 position_km, vec3 sun_dir, int light_steps) {
    vec3 center = planet_center();
    float top_radius = params.camera_position_radius.w + params.cloud_shell.y;
    vec2 hit = ray_sphere(position_km, sun_dir, center, top_radius);
    float ray_end = max(hit.y, 0.0);
    if (ray_end <= 0.0) {
        return 1.0;
    }

    float step_len = ray_end / float(max(light_steps, 1));
    float optical_depth = 0.0;
    for (int i = 0; i < CLOUDS_MAX_LIGHT_STEPS; ++i) {
        if (i >= light_steps) {
            break;
        }
        vec3 p = position_km + sun_dir * ((float(i) + 0.5) * step_len);
        optical_depth += cloud_density(p) * step_len;
    }
    return exp(-optical_depth * 0.50);
}

struct CloudSample {
    vec3 color;
    float transmittance;
    float mean_density;
    float mean_weather;
    float mean_light;
    float mean_shadow;
    float step_fraction;
};

CloudSample march_clouds(vec3 origin, vec3 direction, int view_steps, int light_steps) {
    CloudSample result;
    result.color = vec3(0.0);
    result.transmittance = 1.0;
    result.mean_density = 0.0;
    result.mean_weather = 0.0;
    result.mean_light = 0.0;
    result.mean_shadow = 0.0;
    result.step_fraction = 0.0;

    vec3 center = planet_center();
    float top_radius = params.camera_position_radius.w + params.cloud_shell.y;
    vec2 top_hit = ray_sphere(origin, direction, center, top_radius);
    if (top_hit.y <= 0.0) {
        return result;
    }
    float ray_start = max(top_hit.x, 0.0);
    float ray_end = top_hit.y;
    vec2 ground_hit = ray_sphere(origin, direction, center, params.camera_position_radius.w);
    if (ground_hit.x > 0.0) {
        ray_end = min(ray_end, ground_hit.x);
    }
    if (ray_end <= ray_start) {
        return result;
    }

    vec3 sun_dir = normalize(params.sun_direction_intensity.xyz);
    float step_len = (ray_end - ray_start) / float(max(view_steps, 1));
    int used_steps = 0;
    float density_sum = 0.0;
    float weather_sum = 0.0;
    float light_sum = 0.0;
    float shadow_sum = 0.0;
    for (int i = 0; i < CLOUDS_MAX_VIEW_STEPS; ++i) {
        if (i >= view_steps) {
            break;
        }
        float jitter = hash31(vec3(frag_position, float(i))) - 0.5;
        vec3 p = origin + direction * (ray_start + (float(i) + 0.5 + jitter * 0.35) * step_len);
        float density = cloud_density(p);
        float weather = weather_coverage(p);
        ++used_steps;
        density_sum += density;
        weather_sum += weather;
        if (density <= 0.0001) {
            continue;
        }

        float shadow = light_transmittance(p, sun_dir, light_steps);
        float powder = 1.0 - exp(-density * step_len * 0.75);
        float phase = mix(0.45, 1.55, pow(max(dot(direction, sun_dir), 0.0), 3.0));
        float sun_light = shadow * phase * (0.55 + powder * 0.85) * params.sun_direction_intensity.w;
        float ambient = 0.18 + 0.25 * smoothstep(-0.2, 0.8, dot(normalize(p - center), vec3(0.0, 1.0, 0.0)));
        vec3 cloud_light = vec3(0.95, 0.91, 0.82) * sun_light + vec3(0.30, 0.38, 0.48) * ambient;
        float alpha = 1.0 - exp(-density * step_len * 0.30);
        result.color += result.transmittance * alpha * cloud_light;
        result.transmittance *= 1.0 - alpha;
        light_sum += sun_light + ambient;
        shadow_sum += shadow;
        if (result.transmittance < 0.015) {
            break;
        }
    }

    float denom = max(float(used_steps), 1.0);
    result.mean_density = density_sum / denom;
    result.mean_weather = weather_sum / denom;
    result.mean_light = light_sum / denom;
    result.mean_shadow = shadow_sum / denom;
    result.step_fraction = float(used_steps) / float(max(view_steps, 1));
    return result;
}

void main() {
    vec3 origin = params.camera_position_radius.xyz;
    vec3 direction = view_direction();
    int debug_view = int(params.render_options.x + 0.5);
    int view_steps = clamp(int(params.render_options.y + 0.5), 1, CLOUDS_MAX_VIEW_STEPS);
    int light_steps = clamp(int(params.render_options.z + 0.5), 1, CLOUDS_MAX_LIGHT_STEPS);

    vec3 sky = sky_color(origin, direction);
    CloudSample clouds = march_clouds(origin, direction, view_steps, light_steps);
    vec3 final_color = sky * clouds.transmittance + clouds.color;

    if (debug_view == CLOUDS_VIEW_WEATHER) {
        final_color = vec3(clouds.mean_weather);
    } else if (debug_view == CLOUDS_VIEW_DENSITY) {
        final_color = vec3(clouds.mean_density * 1.6);
    } else if (debug_view == CLOUDS_VIEW_TRANSMITTANCE) {
        final_color = vec3(clouds.transmittance);
    } else if (debug_view == CLOUDS_VIEW_LIGHTING) {
        final_color = vec3(clouds.mean_light);
    } else if (debug_view == CLOUDS_VIEW_SHADOW) {
        final_color = vec3(clouds.mean_shadow);
    } else if (debug_view == CLOUDS_VIEW_STEPS) {
        final_color = vec3(clouds.step_fraction, 1.0 - clouds.step_fraction, 0.15);
    }

    vec3 display = cubey_pbr_apply_display_transform(final_color, vec4(-1.20, 1.0, 0.0, 0.0));
    out_color = vec4(display, 1.0);
}
