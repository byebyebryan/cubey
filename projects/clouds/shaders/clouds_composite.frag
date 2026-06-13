#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/atmosphere.glsl"
#include "cubey/pbr.glsl"
#include "clouds_debug.glsl"

const float CLOUDS_ATMOSPHERE_HEIGHT_KM = 100.0;

layout(set = 0, binding = 1) uniform sampler2D cloud_product_texture;
layout(set = 0, binding = 2) uniform sampler2D cloud_metadata_texture;

layout(std140, set = 0, binding = 0) uniform CloudsFrame {
    vec4 camera_right_aspect;
    vec4 camera_up_tan_half_fovy;
    vec4 camera_forward_mode;
    vec4 camera_position_radius;
    vec4 cloud_shell;
    vec4 weather;
    vec4 sun_direction_intensity;
    vec4 render_options;
    vec4 feature_options;
    vec4 weather_feature_weights;
} params;

layout(location = 0) in vec2 frag_position;
layout(location = 0) out vec4 out_color;

#include "clouds_model.glsl"

CubeyAtmosphereMedium atmosphere_medium() {
    float ground_radius = params.camera_position_radius.w;
    float top_radius = ground_radius + CLOUDS_ATMOSPHERE_HEIGHT_KM;
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

float surface_cloud_shadow(vec3 surface_position, vec3 sun_dir) {
    float shadow_strength = max(params.render_options.w, 0.0);
    vec3 center = planet_center();
    vec3 up = normalize(surface_position - center);
    float sun_visibility = smoothstep(-0.08, 0.04, dot(up, sun_dir));
    if (shadow_strength <= 0.001) {
        return 1.0;
    }
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
    float cloud_shadow = exp(-optical_depth * 0.42 * shadow_strength);
    return mix(1.0, cloud_shadow, sun_visibility);
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
    float twilight_visibility = smoothstep(-0.30, 0.08, dot(up, sun_dir));
    vec3 ambient = vec3(0.014, 0.019, 0.030) * mix(0.10, 1.0, twilight_visibility);
    return albedo * (ambient + direct);
}

float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float star_visibility(vec3 sun_dir) {
    float sun_elevation = clamp(sun_dir.y, -1.0, 1.0);
    return 1.0 - smoothstep(-0.28, -0.08, sun_elevation);
}

vec3 cloud_space_radiance(vec3 direction) {
    vec3 sun_dir = normalize(params.sun_direction_intensity.xyz);
    float sun_alignment = max(dot(direction, sun_dir), 0.0);
    float disk = smoothstep(0.999985, 1.0, sun_alignment);
    float glow = pow(sun_alignment, 360.0) * smoothstep(-0.08, 0.12, sun_dir.y);
    vec3 base = vec3(0.0010, 0.0016, 0.0048);

    vec3 cell = floor(direction * 1150.0);
    float stars = smoothstep(0.9988, 1.0, hash13(cell));
    stars *= 0.55 + 0.45 * hash13(cell + vec3(19.0, 37.0, 71.0));
    stars *= star_visibility(sun_dir);

    return base + vec3(0.78, 0.84, 1.0) * stars * 0.030 +
           vec3(1.0, 0.86, 0.58) * (disk * 12.0 + glow * 0.24);
}

struct BackgroundSample {
    vec3 color;
    vec3 atmosphere;
    vec3 space;
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
    result.space = vec3(0.0);
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
        cubey_atmosphere_classify_sky_background_ray(medium, origin, direction, -1.0);
    result.space = cloud_space_radiance(direction);
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
    result.color = result.atmosphere + result.transmittance * result.space;
    if (ground_segment.hit_ground) {
        result.ground_hit = 1.0;
        vec3 surface_position = origin + direction * ground_segment.ground_t;
        result.cloud_shadow =
            surface_cloud_shadow(surface_position, normalize(params.sun_direction_intensity.xyz));
        CubeyAtmosphereSample ground_atmosphere =
            cubey_atmosphere_integrate_ray(medium, origin, direction, ground_segment.start,
                                           ground_segment.end);
        result.ground =
            ground_atmosphere.color +
            ground_atmosphere.transmittance *
                planet_surface_radiance(surface_position, result.cloud_shadow);
        vec3 camera_up = normalize(origin - planet_center());
        float view_horizon = dot(direction, camera_up);
        float orbit_view = smoothstep(1.5, 2.0, params.camera_forward_mode.w);
        float ground_weight =
            mix(1.0 - smoothstep(-0.045, 0.020, view_horizon), 1.0, orbit_view);
        result.color = mix(result.color, result.ground, ground_weight);
    }
    return result;
}

float high_view_horizon_cloud_fade(vec3 origin, vec3 direction) {
    float high_view = smoothstep(0.5, 1.1, params.camera_forward_mode.w);
    float view_horizon = dot(direction, normalize(origin - planet_center()));
    float horizon_fade = 1.0 - smoothstep(-0.56, -0.04, view_horizon);
    return mix(1.0, horizon_fade, high_view);
}

float local_horizon_filter_strength(vec3 origin, vec3 direction) {
    float orbit_view = smoothstep(1.5, 2.0, params.camera_forward_mode.w);
    float local_view = 1.0 - orbit_view;
    float view_horizon = dot(direction, normalize(origin - planet_center()));
    float above_horizon = smoothstep(-0.08, 0.06, view_horizon);
    float lower_sky = 1.0 - smoothstep(0.30, 0.70, view_horizon);
    float high_view = smoothstep(0.5, 1.1, params.camera_forward_mode.w);
    return clamp(local_view * above_horizon * lower_sky * mix(0.84, 0.52, high_view), 0.0, 0.84);
}

vec4 sample_cloud_product_filtered(vec2 uv, float filter_strength) {
    vec4 center = texture(cloud_product_texture, uv);
    if (filter_strength <= 0.001) {
        return center;
    }

    vec2 texel = 1.0 / vec2(textureSize(cloud_product_texture, 0));
    vec2 y = vec2(0.0, texel.y);
    float radius = mix(1.0, 4.6, filter_strength);
    vec4 blur = center * 0.28;
    blur += texture(cloud_product_texture, uv + y * radius * 0.45) * 0.18;
    blur += texture(cloud_product_texture, uv - y * radius * 0.45) * 0.18;
    blur += texture(cloud_product_texture, uv + y * radius * 0.95) * 0.12;
    blur += texture(cloud_product_texture, uv - y * radius * 0.95) * 0.12;
    blur += texture(cloud_product_texture, uv + y * radius * 1.55) * 0.06;
    blur += texture(cloud_product_texture, uv - y * radius * 1.55) * 0.06;
    return mix(center, blur, filter_strength);
}

void main() {
    vec2 uv = frag_position * 0.5 + 0.5;
    int debug_view = int(params.render_options.x + 0.5);
    vec4 cloud_product = texture(cloud_product_texture, uv);
    vec4 cloud_metadata = texture(cloud_metadata_texture, uv);
    vec3 scene_color = cloud_product.rgb;

    if (!cloud_product_debug_view(debug_view)) {
        vec3 origin = params.camera_position_radius.xyz;
        vec3 direction = view_direction();
        cloud_product = sample_cloud_product_filtered(
            uv, local_horizon_filter_strength(origin, direction));
        BackgroundSample background = sample_background(origin, direction);
        float high_horizon_fade = high_view_horizon_cloud_fade(origin, direction);
        float cloud_transmittance = clamp(cloud_product.a, 0.0, 1.0);
        cloud_transmittance = mix(1.0, cloud_transmittance, high_horizon_fade);
        scene_color = background.color * cloud_transmittance + cloud_product.rgb * high_horizon_fade;

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
        } else if (debug_view == CLOUDS_VIEW_CLOUD_DEPTH) {
            float depth = clamp(cloud_metadata.x / 240.0, 0.0, 1.0);
            scene_color = vec3(depth);
        } else if (debug_view == CLOUDS_VIEW_CLOUD_CONFIDENCE) {
            scene_color = vec3(cloud_metadata.w);
        } else if (debug_view == CLOUDS_VIEW_HORIZON_BLEND) {
            scene_color = vec3(cloud_metadata.z, cloud_metadata.y, 0.12);
        }
    }

    vec3 display = cubey_pbr_apply_display_transform(scene_color, vec4(-1.20, 1.0, 0.0, 0.0));
    out_color = vec4(display, 1.0);
}
