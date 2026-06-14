#ifndef CUBEY_CLOUD_REF_COMMON_GLSL
#define CUBEY_CLOUD_REF_COMMON_GLSL

const float CLOUD_REF_PI = 3.14159265359;

const int CLOUD_REF_DEBUG_FINAL = 0;
const int CLOUD_REF_DEBUG_WEATHER = 1;
const int CLOUD_REF_DEBUG_DENSITY = 2;
const int CLOUD_REF_DEBUG_TRANSMITTANCE = 3;
const int CLOUD_REF_DEBUG_LIGHTING = 4;
const int CLOUD_REF_DEBUG_SHADOW = 5;
const int CLOUD_REF_DEBUG_STEPS = 6;
const int CLOUD_REF_DEBUG_BACKGROUND = 7;
const int CLOUD_REF_DEBUG_CLOUD_ALPHA = 11;
const int CLOUD_REF_DEBUG_DISTANCE = 15;
const int CLOUD_REF_DEBUG_BASE_DENSITY = 16;
const int CLOUD_REF_DEBUG_DETAIL_DENSITY = 17;

layout(std140, set = 0, binding = 0) uniform CloudRefFrame {
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
} params;

float cloud_ref_saturate(float value) {
    return clamp(value, 0.0, 1.0);
}

float cloud_ref_remap(float value, float old_min, float old_max, float new_min, float new_max) {
    return new_min + ((value - old_min) / max(old_max - old_min, 0.00001)) *
                         (new_max - new_min);
}

vec3 cloud_ref_view_direction(vec2 position) {
    vec3 right = params.camera_right_aspect.xyz;
    vec3 up = params.camera_up_tan_half_fovy.xyz;
    vec3 forward = params.camera_forward_mode.xyz;
    float aspect = params.camera_right_aspect.w;
    float tan_half_fovy = params.camera_up_tan_half_fovy.w;
    return normalize(forward + right * (position.x * aspect * tan_half_fovy) -
                     up * (position.y * tan_half_fovy));
}

vec3 cloud_ref_planet_up() {
    return vec3(0.0, 1.0, 0.0);
}

float cloud_ref_planet_radius() {
    return params.camera_position_radius.w;
}

float cloud_ref_camera_altitude() {
    return params.camera_position_radius.y;
}

vec3 cloud_ref_sphere_center() {
    return vec3(params.camera_position_radius.x, -cloud_ref_planet_radius(),
                params.camera_position_radius.z);
}

float cloud_ref_inner_radius() {
    return cloud_ref_planet_radius() + params.cloud_shell.x;
}

float cloud_ref_outer_radius() {
    return cloud_ref_inner_radius() + params.cloud_shell.y;
}

bool cloud_ref_ray_sphere(vec3 origin, vec3 direction, float radius, out float near_t,
                          out float far_t) {
    vec3 local_origin = origin - cloud_ref_sphere_center();
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

float cloud_ref_hg(float cos_theta, float g) {
    float g2 = g * g;
    float denom = pow(max(1.0 + g2 - 2.0 * g * cos_theta, 0.001), 1.5);
    return (1.0 - g2) / (4.0 * CLOUD_REF_PI * denom);
}

float cloud_ref_phase(float cos_theta) {
    float blend = cloud_ref_saturate(cos_theta * 0.5 + 0.5);
    float phase = mix(cloud_ref_hg(cos_theta, -0.08), cloud_ref_hg(cos_theta, 0.08), blend);
    return max(phase, 1.0);
}

float cloud_ref_hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float cloud_ref_value_noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(cloud_ref_hash(i + vec2(0.0, 0.0)), cloud_ref_hash(i + vec2(1.0, 0.0)), u.x),
               mix(cloud_ref_hash(i + vec2(0.0, 1.0)), cloud_ref_hash(i + vec2(1.0, 1.0)), u.x),
               u.y);
}

float cloud_ref_starfield(vec3 direction) {
    vec2 p = vec2(atan(direction.z, direction.x) / (2.0 * CLOUD_REF_PI) + 0.5,
                  asin(clamp(direction.y, -1.0, 1.0)) / CLOUD_REF_PI + 0.5);
    float stars = cloud_ref_value_noise(p * 900.0);
    return smoothstep(0.993, 1.0, stars);
}

vec3 cloud_ref_background(vec3 direction) {
    vec3 up = cloud_ref_planet_up();
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
    sky += vec3(cloud_ref_starfield(direction)) * (1.0 - day) * 0.9;

    if (view_height < -0.015) {
        vec3 ground = mix(vec3(0.030, 0.045, 0.048), vec3(0.18, 0.20, 0.16), day);
        sky = mix(ground, sky, smoothstep(-0.06, 0.015, view_height));
    }
    return sky;
}

vec3 cloud_ref_tonemap(vec3 color) {
    vec3 mapped = color / (vec3(1.0) + color);
    return pow(max(mapped, vec3(0.0)), vec3(1.0 / 2.2));
}

#endif
