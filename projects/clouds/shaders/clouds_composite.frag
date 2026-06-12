#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/atmosphere.glsl"
#include "cubey/pbr.glsl"

const int CLOUDS_VIEW_FINAL = 0;
const int CLOUDS_VIEW_WEATHER = 1;
const int CLOUDS_VIEW_DENSITY = 2;
const int CLOUDS_VIEW_TRANSMITTANCE = 3;
const int CLOUDS_VIEW_LIGHTING = 4;
const int CLOUDS_VIEW_SHADOW = 5;
const int CLOUDS_VIEW_STEPS = 6;
const int CLOUDS_VIEW_BACKGROUND = 7;
const int CLOUDS_VIEW_ATMOSPHERE = 8;
const int CLOUDS_VIEW_GROUND = 9;
const int CLOUDS_VIEW_GROUND_HIT = 10;
const int CLOUDS_VIEW_CLOUD_ALPHA = 11;
const int CLOUDS_VIEW_SHELL = 12;
const int CLOUDS_VIEW_SURFACE_SHADOW = 13;

layout(set = 0, binding = 0) uniform sampler2D cloud_product_texture;

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

vec3 planet_surface_albedo(vec3 position) {
    vec3 up = normalize(position - planet_center());
    float continents = fbm(up * 4.0 + vec3(3.0, 11.0, 19.0));
    float shoreline = smoothstep(0.45, 0.58, continents);
    float lowlands = fbm(up.yzx * 13.0 + vec3(17.0, 2.0, 5.0));
    vec3 ocean = mix(vec3(0.012, 0.043, 0.083), vec3(0.028, 0.085, 0.135), lowlands);
    vec3 land = mix(vec3(0.115, 0.095, 0.060), vec3(0.210, 0.185, 0.120), lowlands);
    return mix(ocean, land, shoreline);
}

float cloud_height_profile(float altitude_km) {
    float bottom = params.cloud_shell.x;
    float top = params.cloud_shell.y;
    float h = clamp((altitude_km - bottom) / max(top - bottom, 0.001), 0.0, 1.0);
    float base = smoothstep(0.03, 0.16, h);
    float top_fade = 1.0 - smoothstep(0.55, 1.0, h);
    float body = pow(max(sin(h * 3.14159265359), 0.0), 0.45);
    return base * top_fade * mix(0.72, 1.18, body);
}

float weather_coverage(vec3 position_km) {
    vec3 center = planet_center();
    vec3 up = normalize(position_km - center);
    float scale = max(params.weather.z, 0.001);
    float frequency = clamp(params.camera_position_radius.w / (scale * 3.5), 1.0, 24.0);
    float orbit_view = smoothstep(1.5, 2.0, params.camera_forward_mode.w);
    vec3 wind_offset = vec3(params.weather.w * 0.004, params.weather.w * 0.0017,
                            -params.weather.w * 0.0028);
    vec3 warp = vec3(
        fbm(up * (frequency * 0.42) + wind_offset + vec3(11.0, 3.0, 7.0)),
        fbm(up * (frequency * 0.38) - wind_offset.yzx + vec3(23.0, 19.0, 5.0)),
        fbm(up * (frequency * 0.46) + wind_offset.zxy + vec3(2.0, 29.0, 31.0))) -
                vec3(0.5);
    vec3 domain = normalize(up + warp * 0.26);
    float macro = fbm(domain * frequency + wind_offset);
    float secondary = fbm(domain.yzx * (frequency * 1.65) - wind_offset.zxy + vec3(17.0));
    float cells = fbm(domain.zxy * (frequency * 2.45) + wind_offset.xzy + vec3(41.0, 7.0, 13.0));
    float bands =
        0.5 + 0.5 * sin(dot(domain, normalize(vec3(0.62, 0.18, -0.76))) * frequency * 1.15 +
                          params.weather.w * 0.006);
    float weather = mix(mix(macro, secondary, 0.34), bands, 0.10);
    weather = mix(weather, cells, mix(0.18, 0.04, orbit_view));
    return clamp(weather, 0.0, 1.0);
}

float cloud_density(vec3 position_km) {
    float altitude = length(position_km - planet_center()) - params.camera_position_radius.w;
    float height = cloud_height_profile(altitude);
    if (height <= 0.0) {
        return 0.0;
    }
    float weather = weather_coverage(position_km);
    float coverage = clamp(params.weather.x, 0.0, 1.0);
    float orbit_view = smoothstep(1.5, 2.0, params.camera_forward_mode.w);
    float coverage_mask = smoothstep(0.34 + (1.0 - coverage) * 0.36, 0.83, weather);
    float detail_scale = mix(0.42, 0.055, orbit_view);
    vec3 detail_coord = position_km * detail_scale + vec3(params.weather.w * 0.03, 13.5, 0.0);
    float detail = fbm(detail_coord);
    float puffy = smoothstep(0.34, 0.78, detail);
    puffy = mix(puffy, smoothstep(0.18, 0.78, weather), orbit_view * 0.85);
    float scallop_scale = mix(0.78, 0.075, orbit_view);
    float scallop = fbm(position_km * scallop_scale - vec3(params.weather.w * 0.016, 5.0, 19.0));
    float edge = smoothstep(0.08, 0.72, coverage_mask * 0.78 + puffy * 0.42);
    edge = mix(edge, coverage_mask, orbit_view * 0.70);
    float erosion = mix(0.20, 1.22, puffy) * mix(0.62, 1.12, scallop);
    erosion = mix(erosion, 0.92, orbit_view * 0.70);
    return max(edge * height * erosion * params.weather.y, 0.0);
}

float surface_cloud_shadow(vec3 surface_position, vec3 sun_dir) {
    float shadow_strength = max(params.render_options.w, 0.0);
    if (shadow_strength <= 0.001) {
        return 1.0;
    }
    vec3 center = planet_center();
    vec3 origin = surface_position + normalize(surface_position - center) * 0.02;
    float top_radius = params.camera_position_radius.w + params.cloud_shell.y;
    vec2 top_hit = ray_sphere(origin, sun_dir, center, top_radius);
    if (top_hit.y <= 0.0) {
        return 1.0;
    }
    float ray_start = max(top_hit.x, 0.0);
    float ray_end = top_hit.y;
    if (ray_end <= ray_start) {
        return 1.0;
    }

    const int shadow_steps = 12;
    float step_len = (ray_end - ray_start) / float(shadow_steps);
    float optical_depth = 0.0;
    for (int i = 0; i < shadow_steps; ++i) {
        vec3 p = origin + sun_dir * (ray_start + (float(i) + 0.5) * step_len);
        optical_depth += cloud_density(p) * step_len;
    }
    return exp(-optical_depth * 0.42 * shadow_strength);
}

vec3 planet_surface_radiance(vec3 position, float cloud_shadow) {
    vec3 up = normalize(position - planet_center());
    vec3 sun_dir = normalize(params.sun_direction_intensity.xyz);
    float sun_visibility = smoothstep(-0.08, 0.04, dot(up, sun_dir));
    float ndotl = max(dot(up, sun_dir), 0.0);
    float light_intensity = params.sun_direction_intensity.w;
    vec3 albedo = planet_surface_albedo(position);
    vec3 direct = vec3(1.00, 0.93, 0.80) * ndotl * sun_visibility * light_intensity *
                  cloud_shadow;
    vec3 ambient = vec3(0.014, 0.019, 0.030) * mix(0.30, 1.0, light_intensity);
    return albedo * (ambient + direct);
}

struct BackgroundSample {
    vec3 color;
    vec3 atmosphere;
    vec3 ground;
    vec3 transmittance;
    float ground_hit;
    float atmosphere_hit;
    float ray_fraction;
    float cloud_shadow;
};

BackgroundSample sample_background(vec3 origin, vec3 direction) {
    BackgroundSample result;
    result.color = vec3(0.0);
    result.atmosphere = vec3(0.0);
    result.ground = vec3(0.0);
    result.transmittance = vec3(1.0);
    result.ground_hit = 0.0;
    result.atmosphere_hit = 0.0;
    result.ray_fraction = 0.0;
    result.cloud_shadow = 1.0;

    CubeyAtmosphereMedium medium = atmosphere_medium();
    CubeyAtmosphereRaySegment ground_segment =
        cubey_atmosphere_classify_ray(medium, origin, direction, -1.0);
    CubeyAtmosphereRaySegment sky_segment =
        ground_segment.hit_ground
            ? ground_segment
            : cubey_atmosphere_classify_sky_background_ray(medium, origin, direction, -1.0);
    if (sky_segment.hit_atmosphere) {
        CubeyAtmosphereSample atmosphere =
            cubey_atmosphere_integrate_ray(medium, origin, direction, sky_segment.start,
                                           sky_segment.end);
        result.atmosphere = atmosphere.color;
        result.transmittance = atmosphere.transmittance;
        result.atmosphere_hit = 1.0;
        result.ray_fraction = clamp(atmosphere.ray_length / max(medium.top_radius * 0.08, 1.0),
                                    0.0, 1.0);
    }
    result.color = result.atmosphere;
    if (ground_segment.hit_ground) {
        result.ground_hit = 1.0;
        vec3 surface_position = origin + direction * ground_segment.ground_t;
        result.cloud_shadow =
            surface_cloud_shadow(surface_position, normalize(params.sun_direction_intensity.xyz));
        result.ground = planet_surface_radiance(surface_position, result.cloud_shadow);
        result.color += result.transmittance * result.ground;
    }
    return result;
}

bool cloud_product_debug_view(int debug_view) {
    return debug_view == CLOUDS_VIEW_WEATHER || debug_view == CLOUDS_VIEW_DENSITY ||
           debug_view == CLOUDS_VIEW_TRANSMITTANCE || debug_view == CLOUDS_VIEW_LIGHTING ||
           debug_view == CLOUDS_VIEW_SHADOW || debug_view == CLOUDS_VIEW_STEPS ||
           debug_view == CLOUDS_VIEW_CLOUD_ALPHA || debug_view == CLOUDS_VIEW_SHELL;
}

void main() {
    vec2 uv = frag_position * 0.5 + 0.5;
    vec4 cloud_product = texture(cloud_product_texture, uv);
    vec3 scene_color = cloud_product.rgb;

    int debug_view = int(params.render_options.x + 0.5);
    if (!cloud_product_debug_view(debug_view)) {
        vec3 origin = params.camera_position_radius.xyz;
        vec3 direction = view_direction();
        BackgroundSample background = sample_background(origin, direction);
        float cloud_transmittance = clamp(cloud_product.a, 0.0, 1.0);
        scene_color = background.color * cloud_transmittance + cloud_product.rgb;

        if (debug_view == CLOUDS_VIEW_BACKGROUND) {
            scene_color = background.color;
        } else if (debug_view == CLOUDS_VIEW_ATMOSPHERE) {
            scene_color = background.atmosphere;
        } else if (debug_view == CLOUDS_VIEW_GROUND) {
            scene_color = background.ground;
        } else if (debug_view == CLOUDS_VIEW_GROUND_HIT) {
            scene_color =
                vec3(background.ground_hit, background.atmosphere_hit, background.ray_fraction);
        } else if (debug_view == CLOUDS_VIEW_SURFACE_SHADOW) {
            scene_color = vec3(background.cloud_shadow * background.ground_hit);
        }
    }

    vec3 display = cubey_pbr_apply_display_transform(scene_color, vec4(-1.20, 1.0, 0.0, 0.0));
    out_color = vec4(display, 1.0);
}
