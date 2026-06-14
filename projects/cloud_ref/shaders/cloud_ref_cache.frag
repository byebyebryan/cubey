#version 450
#extension GL_GOOGLE_include_directive : require

#include "cloud_ref_common.glsl"

layout(set = 0, binding = 1) uniform sampler2D weather_texture;

layout(location = 0) in vec2 frag_position;
layout(location = 0) out vec4 out_cloud;

struct CloudRefDensitySample {
    float density;
    float weather;
    float height;
    float light;
};

CloudRefDensitySample sample_cloud_ref(vec3 direction) {
    vec3 up = cloud_ref_planet_up();
    float vertical = dot(direction, up);
    float camera_altitude = cloud_ref_camera_altitude_km();
    float bottom = params.cloud_shell.x;
    float top = params.cloud_shell.y;
    float denom = abs(vertical) < 0.018 ? (vertical < 0.0 ? -0.018 : 0.018) : vertical;
    float entry = (bottom - camera_altitude) / denom;
    float exit = (top - camera_altitude) / denom;
    float start = max(min(entry, exit), 0.0);
    float end = max(max(entry, exit), 0.0);
    float ray_length = clamp(end - start, 0.0, 220.0);
    float sample_distance = start + ray_length * 0.42;
    vec2 weather_uv = cloud_ref_weather_uv(direction, sample_distance);
    vec4 weather = texture(weather_texture, weather_uv);
    float broad = weather.r;
    float fronts = weather.g * params.weather_feature_weights.x;
    float cells = weather.b * params.weather_feature_weights.y;
    float erosion = mix(weather.a, cloud_ref_fbm(weather_uv * 24.0), 0.36) *
                    params.weather_feature_weights.w;
    float target = mix(0.88, 0.62, params.weather.x);
    float coverage = smoothstep(target - 0.18, target + 0.18,
                                broad * 0.68 + fronts * 0.18 + cells * 0.14);
    float height_fraction =
        clamp((camera_altitude + sample_distance * vertical - bottom) / max(top - bottom, 0.001),
              0.0, 1.0);
    float height_profile = smoothstep(0.02, 0.22, height_fraction) *
                           (1.0 - smoothstep(0.68, 1.0, height_fraction));
    float detail_cut = smoothstep(0.18, 0.76, coverage - erosion * 0.55);
    float density = coverage * height_profile * detail_cut * params.weather.y;
    density *= 0.68;
    density *= smoothstep(0.0, 18.0, ray_length);
    vec3 sun_dir = normalize(params.sun_direction_intensity.xyz);
    float light = cloud_ref_phase(dot(direction, sun_dir)) * params.sun_direction_intensity.w;
    light += 0.08 + params.cloud_shell.z * 0.20;
    return CloudRefDensitySample(density, coverage, height_fraction, light);
}

void main() {
    int debug_view = int(params.ref_options.x + 0.5);
    vec3 direction = cloud_ref_view_direction(frag_position);
    vec3 up = cloud_ref_planet_up();
    if (dot(direction, up) < -0.02) {
        out_cloud = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    CloudRefDensitySample sample_data = sample_cloud_ref(direction);
    float optical_depth = sample_data.density * 1.65;
    float alpha = 1.0 - exp(-optical_depth);
    float transmittance = 1.0 - alpha;
    vec3 sun_tint = mix(vec3(0.92, 0.96, 1.0), vec3(1.0, 0.80, 0.55),
                        exp(-abs(dot(normalize(params.sun_direction_intensity.xyz), up)) * 4.0));
    vec3 cloud_color = sun_tint * sample_data.light * alpha * 1.35;
    cloud_color += vec3(0.55, 0.62, 0.70) * alpha * (0.05 + params.cloud_shell.z * 0.10);

    if (debug_view == CLOUD_REF_DEBUG_WEATHER) {
        out_cloud = vec4(vec3(sample_data.weather), 1.0);
    } else if (debug_view == CLOUD_REF_DEBUG_DENSITY) {
        out_cloud = vec4(vec3(sample_data.density), 1.0);
    } else if (debug_view == CLOUD_REF_DEBUG_TRANSMITTANCE) {
        out_cloud = vec4(vec3(transmittance), 1.0);
    } else if (debug_view == CLOUD_REF_DEBUG_LIGHTING) {
        out_cloud = vec4(vec3(sample_data.light), 1.0);
    } else if (debug_view == CLOUD_REF_DEBUG_CLOUD_ALPHA) {
        out_cloud = vec4(vec3(alpha), 1.0);
    } else {
        out_cloud = vec4(cloud_color, transmittance);
    }
}
