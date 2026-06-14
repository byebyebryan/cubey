#ifndef CUBEY_CLOUD_REF_COMMON_GLSL
#define CUBEY_CLOUD_REF_COMMON_GLSL

const float CLOUD_REF_PI = 3.14159265359;
const int CLOUD_REF_DEBUG_FINAL = 0;
const int CLOUD_REF_DEBUG_WEATHER = 1;
const int CLOUD_REF_DEBUG_DENSITY = 2;
const int CLOUD_REF_DEBUG_TRANSMITTANCE = 3;
const int CLOUD_REF_DEBUG_LIGHTING = 4;
const int CLOUD_REF_DEBUG_BACKGROUND = 7;
const int CLOUD_REF_DEBUG_CLOUD_ALPHA = 11;

layout(std140, set = 0, binding = 0) uniform CloudRefFrame {
    vec4 camera_right_aspect;
    vec4 camera_up_tan_half_fovy;
    vec4 camera_forward_mode;
    vec4 camera_position_radius;
    vec4 cloud_shell;
    vec4 weather;
    vec4 sun_direction_intensity;
    vec4 ref_options;
    vec4 weather_feature_weights;
} params;

vec3 cloud_ref_view_direction(vec2 position) {
    vec3 right = params.camera_right_aspect.xyz;
    vec3 up = params.camera_up_tan_half_fovy.xyz;
    vec3 forward = params.camera_forward_mode.xyz;
    float aspect = params.camera_right_aspect.w;
    float tan_half_fovy = params.camera_up_tan_half_fovy.w;
    return normalize(forward + right * (position.x * aspect * tan_half_fovy) +
                     up * (position.y * tan_half_fovy));
}

vec3 cloud_ref_planet_up() {
    return normalize(params.camera_position_radius.xyz);
}

float cloud_ref_camera_altitude_km() {
    return length(params.camera_position_radius.xyz) - params.camera_position_radius.w;
}

vec2 cloud_ref_weather_uv(vec3 direction, float distance_km) {
    vec3 up = cloud_ref_planet_up();
    vec3 east = normalize(params.camera_right_aspect.xyz);
    vec3 north = normalize(cross(up, east));
    vec2 plane = vec2(dot(direction, east), dot(direction, north)) * distance_km;
    float scale = max(params.weather.z, 1.0);
    return plane / scale + vec2(0.31, 0.47) + vec2(params.weather.w * 0.018, params.weather.w * 0.006);
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

float cloud_ref_fbm(vec2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 5; ++i) {
        value += cloud_ref_value_noise(p) * amplitude;
        p = p * 2.03 + vec2(17.1, 5.7);
        amplitude *= 0.52;
    }
    return value;
}

float cloud_ref_phase(float cos_theta) {
    float g = 0.62;
    float g2 = g * g;
    float denom = pow(max(1.0 + g2 - 2.0 * g * cos_theta, 0.001), 1.5);
    float forward = (1.0 - g2) / (4.0 * CLOUD_REF_PI * denom);
    float silver = pow(max(cos_theta, 0.0), 12.0) * 0.18;
    return forward + silver + 0.045;
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
    sky += vec3(1.0, 0.34, 0.12) * twilight * pow(max(1.0 - abs(view_height), 0.0), 3.2) * 0.55;
    float sun_alignment = max(dot(direction, sun_dir), 0.0);
    sky += vec3(1.0, 0.82, 0.48) * pow(sun_alignment, 900.0) * day * 8.0;
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
