#ifndef CUBEY_CLOUD_COMMON_GLSL
#define CUBEY_CLOUD_COMMON_GLSL

const float CLOUD_PI = 3.14159265359;

const int CLOUD_DEBUG_FINAL = 0;
const int CLOUD_DEBUG_WEATHER = 1;
const int CLOUD_DEBUG_DENSITY = 2;
const int CLOUD_DEBUG_TRANSMITTANCE = 3;
const int CLOUD_DEBUG_LIGHTING = 4;
const int CLOUD_DEBUG_SHADOW = 5;
const int CLOUD_DEBUG_STEPS = 6;
const int CLOUD_DEBUG_BACKGROUND = 7;
const int CLOUD_DEBUG_RAW_FINAL = 8;
const int CLOUD_DEBUG_CLOUD_ALPHA = 11;
const int CLOUD_DEBUG_DISTANCE = 15;
const int CLOUD_DEBUG_BASE_DENSITY = 16;
const int CLOUD_DEBUG_DETAIL_DENSITY = 17;
const int CLOUD_DEBUG_AMBIENT_LIGHT = 18;
const int CLOUD_DEBUG_DIRECT_LIGHT = 19;
const int CLOUD_DEBUG_PHASE_LIGHT = 20;
const int CLOUD_DEBUG_METADATA_DISTANCE = 21;
const int CLOUD_DEBUG_METADATA_ALPHA = 22;
const int CLOUD_DEBUG_METADATA_CONFIDENCE = 23;
const int CLOUD_DEBUG_METADATA_DENSITY = 24;
const int CLOUD_DEBUG_CLOUD_TYPE = 25;
const int CLOUD_DEBUG_VISIBLE_DENSITY = 26;
const int CLOUD_DEBUG_VISIBLE_CLOUD_TYPE = 27;
const int CLOUD_DEBUG_WEATHER_EDGE = 28;
const int CLOUD_DEBUG_WEATHER_BIAS = 29;

const int CLOUD_SAMPLING_INTERLEAVED = 0;
const int CLOUD_SAMPLING_BAYER = 1;
const int CLOUD_SAMPLING_OFF = 2;

const int CLOUD_BACKGROUND_ATMOSPHERE = 0;
const int CLOUD_BACKGROUND_WATER_CONTEXT = 1;

layout(std140, set = 0, binding = 0) uniform CloudFrame {
    vec4 camera_right_aspect;
    vec4 camera_up_tan_half_fovy;
    vec4 camera_forward_mode;
    vec4 camera_position_radius;
    vec4 cloud_shell;
    vec4 weather;
    vec4 sun_direction_intensity;
    vec4 ref_options;
    vec4 shape_options;
    vec4 weather_feature_weights;
    vec4 cloud_color_top_shadow;
    vec4 cloud_color_bottom_horizon;
    vec4 lighting_strengths;
    vec4 composite_options;
    vec4 sampling_options;
    vec4 temporal_options;
    vec4 background_options;
} params;

float cloud_saturate(float value) {
    return clamp(value, 0.0, 1.0);
}

float cloud_remap(float value, float old_min, float old_max, float new_min, float new_max) {
    return new_min + ((value - old_min) / max(old_max - old_min, 0.00001)) *
                         (new_max - new_min);
}

vec3 cloud_view_direction(vec2 position) {
    vec3 right = params.camera_right_aspect.xyz;
    vec3 up = params.camera_up_tan_half_fovy.xyz;
    vec3 forward = params.camera_forward_mode.xyz;
    float aspect = params.camera_right_aspect.w;
    float tan_half_fovy = params.camera_up_tan_half_fovy.w;
    return normalize(forward + right * (position.x * aspect * tan_half_fovy) -
                     up * (position.y * tan_half_fovy));
}

vec3 cloud_planet_up() {
    return vec3(0.0, 1.0, 0.0);
}

float cloud_planet_radius() {
    return params.camera_position_radius.w;
}

float cloud_camera_altitude() {
    return params.camera_position_radius.y;
}

vec3 cloud_sphere_center() {
    return vec3(params.camera_position_radius.x, -cloud_planet_radius(),
                params.camera_position_radius.z);
}

float cloud_inner_radius() {
    return cloud_planet_radius() + params.cloud_shell.x;
}

float cloud_outer_radius() {
    return cloud_inner_radius() + params.cloud_shell.y;
}

bool cloud_ray_sphere(vec3 origin, vec3 direction, float radius, out float near_t,
                          out float far_t) {
    vec3 local_origin = origin - cloud_sphere_center();
    float b = dot(local_origin, direction);
    float c = dot(local_origin, local_origin) - radius * radius;
    float h = b * b - c;
    if (h < 0.0) {
        near_t = 0.0;
        far_t = 0.0;
        return false;
    }
    h = sqrt(h);
    near_t = -b - h;
    far_t = -b + h;
    return far_t > 0.0;
}

float cloud_hg(float cos_theta, float g) {
    float g2 = g * g;
    float denom = pow(max(1.0 + g2 - 2.0 * g * cos_theta, 0.001), 1.5);
    return (1.0 - g2) / (4.0 * CLOUD_PI * denom);
}

float cloud_phase(float cos_theta) {
    float blend = cloud_saturate(cos_theta * 0.5 + 0.5);
    float phase = mix(cloud_hg(cos_theta, -0.08), cloud_hg(cos_theta, 0.08), blend);
    return max(phase, 1.0);
}

float cloud_hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float cloud_value_noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(cloud_hash(i + vec2(0.0, 0.0)), cloud_hash(i + vec2(1.0, 0.0)), u.x),
               mix(cloud_hash(i + vec2(0.0, 1.0)), cloud_hash(i + vec2(1.0, 1.0)), u.x),
               u.y);
}

float cloud_starfield(vec3 direction) {
    vec2 p = vec2(atan(direction.z, direction.x) / (2.0 * CLOUD_PI) + 0.5,
                  asin(clamp(direction.y, -1.0, 1.0)) / CLOUD_PI + 0.5);
    float stars = cloud_value_noise(p * 900.0);
    return smoothstep(0.993, 1.0, stars);
}

vec3 cloud_sky_color(vec3 direction) {
    vec3 up = cloud_planet_up();
    vec3 sun_dir = normalize(params.sun_direction_intensity.xyz);
    float sun_elevation = dot(sun_dir, up);
    float view_height = clamp(dot(direction, up), -1.0, 1.0);
    float day = smoothstep(-0.12, 0.08, sun_elevation);
    float twilight = exp(-abs(sun_elevation) * 10.0);

    vec3 zenith_day = vec3(0.19, 0.45, 0.82);
    vec3 horizon_day = vec3(0.72, 0.86, 0.95);
    vec3 zenith_night = vec3(0.004, 0.006, 0.014);
    vec3 horizon_night = vec3(0.020, 0.026, 0.045);
    float horizon = pow(1.0 - max(view_height, 0.0), 2.0);
    vec3 sky = mix(mix(zenith_night, horizon_night, horizon),
                   mix(zenith_day, horizon_day, horizon), day);
    sky += vec3(1.0, 0.34, 0.12) * twilight *
           pow(max(1.0 - abs(view_height), 0.0), 3.2) * 0.55;

    float sun_alignment = max(dot(direction, sun_dir), 0.0);
    sky += vec3(1.0, 0.82, 0.48) * pow(sun_alignment, 900.0) * day *
           params.sun_direction_intensity.w * 6.0;
    sky += vec3(1.0, 0.50, 0.18) * pow(sun_alignment, 42.0) * day *
           params.sun_direction_intensity.w * 0.45;
    sky += vec3(cloud_starfield(direction)) * (1.0 - day) * 0.9;
    return sky;
}

vec3 cloud_water_context(vec3 direction) {
    vec3 up = cloud_planet_up();
    vec3 sun_dir = normalize(params.sun_direction_intensity.xyz);
    float view_height = clamp(dot(direction, up), -1.0, 1.0);
    float camera_height = max(cloud_camera_altitude(), 1.0);
    float plane_t = camera_height / max(-direction.y, 0.001);
    vec2 water_position = params.camera_position_radius.xz + direction.xz * plane_t;
    vec2 ripple_uv = water_position * 0.00018;
    float broad = cloud_value_noise(ripple_uv * 2.1);
    float detail = cloud_value_noise(ripple_uv * 11.0 + vec2(13.7, 5.1));
    float wave = mix(broad, detail, 0.32);

    vec3 reflection_dir = reflect(direction, up);
    vec3 reflected_sky = cloud_sky_color(reflection_dir);
    vec3 deep_water = vec3(0.025, 0.100, 0.135);
    vec3 shallow_haze = vec3(0.420, 0.540, 0.560);
    float horizon = smoothstep(-0.42, -0.010, view_height);
    float fresnel = pow(1.0 - clamp(dot(-direction, up), 0.0, 1.0), 5.0);
    float glitter_mask = smoothstep(0.54, 0.92, wave);
    float sun_glitter = pow(max(dot(reflection_dir, sun_dir), 0.0), 220.0) *
                        (0.35 + 0.65 * glitter_mask);

    vec3 water = mix(deep_water, reflected_sky, 0.18 + 0.52 * fresnel);
    water = mix(water, shallow_haze, horizon * 0.68);
    water += vec3(1.0, 0.82, 0.52) * sun_glitter * params.sun_direction_intensity.w * 5.0;
    water += vec3(wave - 0.5) * 0.018;
    return max(water, vec3(0.0));
}

vec3 cloud_background(vec3 direction) {
    vec3 up = cloud_planet_up();
    float view_height = clamp(dot(direction, up), -1.0, 1.0);
    vec3 sky = cloud_sky_color(direction);
    int background_mode = int(params.background_options.x + 0.5);

    if (background_mode == CLOUD_BACKGROUND_WATER_CONTEXT && view_height < -0.015) {
        vec3 water = cloud_water_context(direction);
        sky = mix(water, sky, smoothstep(-0.075, 0.045, view_height));
    }
    return sky;
}

vec3 cloud_tonemap(vec3 color) {
    vec3 mapped = color / (vec3(1.0) + color);
    return pow(max(mapped, vec3(0.0)), vec3(1.0 / 2.2));
}

#endif
