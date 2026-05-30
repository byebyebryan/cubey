#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"
#include "cubey/pbr.glsl"

const float ATMOSPHERE_PI = 3.14159265359;
const int ATMOSPHERE_VIEW_SAMPLE_COUNT = 16;
const int ATMOSPHERE_LIGHT_SAMPLE_COUNT = 8;
const float ATMOSPHERE_SUN_INTENSITY = 22.0;
const float ATMOSPHERE_MIN_TWILIGHT_SOFTNESS = 0.022;

layout(push_constant) uniform AtmosphereParams {
    vec4 camera_right_aspect;
    vec4 camera_up_tan_half_fovy;
    vec4 camera_forward_debug_view;
    vec4 radii_ground;
    vec4 rayleigh;
    vec4 mie;
    vec4 ozone;
    vec4 sun_direction_radius;
    vec4 display_transform;
    vec4 atmosphere_options;
} atmosphere;

layout(location = 0) in vec2 frag_ndc;
layout(location = 0) out vec4 out_color;

struct OpticalDepth {
    float rayleigh;
    float mie;
    float ozone;
};

struct AtmosphereSample {
    vec3 color;
    vec3 rayleigh;
    vec3 mie;
    vec3 transmittance;
    OpticalDepth optical_depth;
};

OpticalDepth optical_depth_zero() {
    return OpticalDepth(0.0, 0.0, 0.0);
}

OpticalDepth optical_depth_add(OpticalDepth lhs, OpticalDepth rhs) {
    return OpticalDepth(lhs.rayleigh + rhs.rayleigh, lhs.mie + rhs.mie, lhs.ozone + rhs.ozone);
}

OpticalDepth optical_depth_scale(OpticalDepth value, float scale) {
    return OpticalDepth(value.rayleigh * scale, value.mie * scale, value.ozone * scale);
}

vec2 ray_sphere_intersection(vec3 ray_origin, vec3 ray_direction, vec3 sphere_center,
                             float sphere_radius) {
    vec3 offset = ray_origin - sphere_center;
    float b = dot(offset, ray_direction);
    float c = dot(offset, offset) - sphere_radius * sphere_radius;
    float discriminant = b * b - c;
    if (discriminant < 0.0) {
        return vec2(1.0, -1.0);
    }
    float root = sqrt(discriminant);
    return vec2(-b - root, -b + root);
}

float altitude_at(vec3 position, vec3 planet_center) {
    return max(length(position - planet_center) - atmosphere.radii_ground.x, 0.0);
}

float ozone_density(float altitude_km) {
    float center = atmosphere.ozone.w;
    float half_width = atmosphere.atmosphere_options.x;
    return max(1.0 - abs(altitude_km - center) / half_width, 0.0);
}

OpticalDepth sample_density(vec3 position, vec3 planet_center) {
    float altitude = altitude_at(position, planet_center);
    return OpticalDepth(
        exp(-altitude / atmosphere.rayleigh.w),
        exp(-altitude / atmosphere.mie.z),
        ozone_density(altitude)
    );
}

vec3 transmittance_from_depth(OpticalDepth depth) {
    vec3 rayleigh_extinction = atmosphere.rayleigh.xyz * depth.rayleigh;
    vec3 mie_extinction = vec3(atmosphere.mie.y * depth.mie);
    vec3 ozone_extinction = atmosphere.ozone.xyz * depth.ozone;
    return exp(-(rayleigh_extinction + mie_extinction + ozone_extinction));
}

OpticalDepth integrate_optical_depth(vec3 origin, vec3 direction, float ray_length,
                                     vec3 planet_center, int sample_count) {
    if (ray_length <= 0.0) {
        return optical_depth_zero();
    }
    OpticalDepth optical_depth = optical_depth_zero();
    float step_length = ray_length / float(sample_count);
    for (int i = 0; i < ATMOSPHERE_LIGHT_SAMPLE_COUNT; ++i) {
        if (i >= sample_count) {
            break;
        }
        float t = (float(i) + 0.5) * step_length;
        optical_depth =
            optical_depth_add(optical_depth, optical_depth_scale(sample_density(
                                  origin + direction * t, planet_center), step_length));
    }
    return optical_depth;
}

float rayleigh_phase(float cos_theta) {
    return (3.0 / (16.0 * ATMOSPHERE_PI)) * (1.0 + cos_theta * cos_theta);
}

float mie_phase(float cos_theta) {
    float g = clamp(atmosphere.mie.w, 0.0, 0.98);
    float g2 = g * g;
    float denom = pow(max(1.0 + g2 - 2.0 * g * cos_theta, 0.0001), 1.5);
    return (3.0 / (8.0 * ATMOSPHERE_PI)) * ((1.0 - g2) * (1.0 + cos_theta * cos_theta)) /
           max((2.0 + g2) * denom, 0.0001);
}

float sun_visibility(vec3 sample_position, vec3 sun_direction, vec3 planet_center) {
    vec3 to_planet_center = planet_center - sample_position;
    float center_distance = length(to_planet_center);
    float planet_angular_radius =
        asin(clamp(atmosphere.radii_ground.x / max(center_distance, 0.0001), 0.0, 1.0));
    float sun_center_angle =
        acos(clamp(dot(sun_direction, normalize(to_planet_center)), -1.0, 1.0));
    float limb_clearance = sun_center_angle - planet_angular_radius;
    float softness =
        max(atmosphere.sun_direction_radius.w * 4.0, ATMOSPHERE_MIN_TWILIGHT_SOFTNESS);
    return smoothstep(-softness, softness, limb_clearance);
}

float ground_sun_visibility(vec3 normal, vec3 sun_direction) {
    float softness =
        max(atmosphere.sun_direction_radius.w * 4.0, ATMOSPHERE_MIN_TWILIGHT_SOFTNESS);
    return smoothstep(-softness, softness, dot(normal, sun_direction));
}

AtmosphereSample integrate_atmosphere(vec3 ray_origin, vec3 ray_direction, float ray_start,
                                      float ray_end, vec3 planet_center) {
    AtmosphereSample result;
    result.color = vec3(0.0);
    result.rayleigh = vec3(0.0);
    result.mie = vec3(0.0);
    result.transmittance = vec3(1.0);
    result.optical_depth = optical_depth_zero();

    float ray_length = max(ray_end - ray_start, 0.0);
    if (ray_length <= 0.0) {
        return result;
    }

    vec3 sun_direction = normalize(atmosphere.sun_direction_radius.xyz);
    float cos_theta = dot(ray_direction, sun_direction);
    float rayleigh_phase_value = rayleigh_phase(cos_theta);
    float mie_phase_value = mie_phase(cos_theta);
    float step_length = ray_length / float(ATMOSPHERE_VIEW_SAMPLE_COUNT);

    for (int i = 0; i < ATMOSPHERE_VIEW_SAMPLE_COUNT; ++i) {
        float t = ray_start + (float(i) + 0.5) * step_length;
        vec3 sample_position = ray_origin + ray_direction * t;
        OpticalDepth density = sample_density(sample_position, planet_center);
        result.optical_depth =
            optical_depth_add(result.optical_depth, optical_depth_scale(density, step_length));

        vec2 sun_atmosphere_hit = ray_sphere_intersection(sample_position, sun_direction,
                                                          planet_center,
                                                          atmosphere.radii_ground.y);
        float sun_ray_length = max(sun_atmosphere_hit.y, 0.0);
        float solar_visibility = sun_visibility(sample_position, sun_direction, planet_center);
        if (solar_visibility <= 0.0001) {
            continue;
        }

        OpticalDepth light_depth = integrate_optical_depth(
            sample_position, sun_direction, sun_ray_length, planet_center,
            ATMOSPHERE_LIGHT_SAMPLE_COUNT);
        OpticalDepth total_depth = optical_depth_add(result.optical_depth, light_depth);
        vec3 transmittance = transmittance_from_depth(total_depth);
        vec3 rayleigh_scattering =
            density.rayleigh * atmosphere.rayleigh.xyz * rayleigh_phase_value;
        vec3 mie_scattering = density.mie * vec3(atmosphere.mie.x) * mie_phase_value;

        result.rayleigh += transmittance * rayleigh_scattering * step_length * solar_visibility;
        result.mie += transmittance * mie_scattering * step_length * solar_visibility;
    }

    result.rayleigh *= ATMOSPHERE_SUN_INTENSITY;
    result.mie *= ATMOSPHERE_SUN_INTENSITY;
    result.color = result.rayleigh + result.mie;
    result.transmittance = transmittance_from_depth(result.optical_depth);
    return result;
}

float nearest_positive_ground_hit(vec3 ray_origin, vec3 ray_direction, vec3 planet_center) {
    vec2 ground_hit = ray_sphere_intersection(ray_origin, ray_direction, planet_center,
                                              atmosphere.radii_ground.x);
    if (ground_hit.x > 0.0) {
        return ground_hit.x;
    }
    if (ground_hit.y > 0.0) {
        return ground_hit.y;
    }
    return -1.0;
}

vec3 sun_disk_luminance(vec3 ray_direction, vec3 planet_center) {
    vec3 sun_direction = normalize(atmosphere.sun_direction_radius.xyz);
    float sun_cos = dot(ray_direction, sun_direction);
    float sun_cutoff = cos(atmosphere.sun_direction_radius.w);
    float disk = smoothstep(sun_cutoff, min(1.0, sun_cutoff + 0.00045), sun_cos);
    if (disk <= 0.0) {
        return vec3(0.0);
    }
    vec3 ray_origin = vec3(0.0);
    vec2 atmosphere_hit = ray_sphere_intersection(ray_origin, ray_direction, planet_center,
                                                  atmosphere.radii_ground.y);
    float ray_end = max(atmosphere_hit.y, 0.0);
    OpticalDepth depth = integrate_optical_depth(ray_origin, ray_direction, ray_end,
                                                planet_center, ATMOSPHERE_LIGHT_SAMPLE_COUNT);
    return transmittance_from_depth(depth) * disk * ATMOSPHERE_SUN_INTENSITY;
}

float reference_line(vec2 position, float spacing) {
    vec2 uv = position / max(spacing, 0.0001);
    vec2 derivative = max(fwidth(uv), vec2(0.0001));
    vec2 grid = abs(fract(uv - 0.5) - 0.5) / derivative;
    return 1.0 - clamp(min(grid.x, grid.y), 0.0, 1.0);
}

float reference_axis(float coordinate) {
    float width = max(fwidth(coordinate) * 1.75, 0.012);
    return 1.0 - smoothstep(width * 0.35, width, abs(coordinate));
}

float reference_ring(vec2 position, float radius) {
    float distance_to_ring = abs(length(position) - radius);
    float width = max(fwidth(length(position)) * 1.5, 0.020);
    return 1.0 - smoothstep(width * 0.25, width, distance_to_ring);
}

vec4 ground_reference_geometry(vec3 ground_position) {
    if (atmosphere.atmosphere_options.y < 0.5) {
        return vec4(0.0);
    }

    vec2 ground_xz = ground_position.xz;
    float grid_scale = max(atmosphere.atmosphere_options.z, 0.001);
    float intensity = atmosphere.atmosphere_options.w;
    float ground_distance = length(ground_xz);
    float altitude = atmosphere.radii_ground.z;
    float range = mix(80.0, 650.0, smoothstep(0.5, 25.0, altitude));
    float distance_fade = 1.0 - smoothstep(range * 0.62, range, ground_distance);
    float horizon_fade = smoothstep(0.0, 0.08, dot(normalize(ground_position -
                                                            vec3(0.0, -atmosphere.radii_ground.x -
                                                                          atmosphere.radii_ground.z,
                                                                 0.0)),
                                                    vec3(0.0, 1.0, 0.0)));

    float minor_grid = reference_line(ground_xz, grid_scale);
    float major_grid = reference_line(ground_xz, grid_scale * 10.0);
    float x_axis = reference_axis(ground_xz.y);
    float z_axis = reference_axis(ground_xz.x);
    float origin = 1.0 - smoothstep(grid_scale * 0.16, grid_scale * 0.24, ground_distance);
    float five_ring = reference_ring(ground_xz, grid_scale * 5.0);
    float ten_ring = reference_ring(ground_xz, grid_scale * 10.0);

    vec3 minor_color = cubey_srgb_to_linear(vec3(0.35, 0.40, 0.42));
    vec3 major_color = cubey_srgb_to_linear(vec3(0.62, 0.67, 0.66));
    vec3 ring_color = cubey_srgb_to_linear(vec3(0.82, 0.78, 0.54));
    vec3 x_axis_color = cubey_srgb_to_linear(vec3(0.95, 0.20, 0.14));
    vec3 z_axis_color = cubey_srgb_to_linear(vec3(0.10, 0.78, 0.92));
    vec3 origin_color = cubey_srgb_to_linear(vec3(1.0, 0.90, 0.20));

    vec3 color = minor_color;
    float alpha = minor_grid * 0.18;
    color = mix(color, major_color, major_grid);
    alpha = max(alpha, major_grid * 0.36);
    color = mix(color, ring_color, max(five_ring * 0.45, ten_ring));
    alpha = max(alpha, max(five_ring * 0.20, ten_ring * 0.32));
    color = mix(color, x_axis_color, x_axis);
    alpha = max(alpha, x_axis * 0.68);
    color = mix(color, z_axis_color, z_axis);
    alpha = max(alpha, z_axis * 0.68);
    color = mix(color, origin_color, origin);
    alpha = max(alpha, origin * 0.85);

    alpha *= distance_fade * horizon_fade * intensity;
    return vec4(color, clamp(alpha, 0.0, 1.0));
}

vec3 ground_radiance(vec3 ray_direction, vec3 planet_center, float ground_t) {
    vec3 sun_direction = normalize(atmosphere.sun_direction_radius.xyz);
    vec3 ground_position = ray_direction * ground_t;
    vec3 normal = normalize(ground_position - planet_center);
    float ndotl = dot(normal, sun_direction);
    float sun_visibility = ground_sun_visibility(normal, sun_direction);
    float direct_light = max(ndotl, 0.0) * sun_visibility;
    float twilight_fill = (1.0 - sun_visibility) * smoothstep(-0.16, 0.02, ndotl) * 0.16;
    vec3 base = cubey_srgb_to_linear(vec3(0.18, 0.20, 0.16)) * atmosphere.radii_ground.w;
    vec3 lit_ground = base * (0.12 + direct_light * 1.35 + twilight_fill);
    vec4 reference = ground_reference_geometry(ground_position);
    return mix(lit_ground, reference.rgb, reference.a);
}

vec3 render_aerial_perspective_debug(vec3 ray_direction, vec3 planet_center) {
    vec2 atmosphere_hit = ray_sphere_intersection(vec3(0.0), ray_direction, planet_center,
                                                  atmosphere.radii_ground.y);
    float max_t = max(atmosphere_hit.y, 0.0);
    float ramp = smoothstep(-1.0, 1.0, frag_ndc.x);
    float scene_t = min(max_t, mix(8.0, 320.0, ramp));
    AtmosphereSample atmosphere_sample = integrate_atmosphere(vec3(0.0), ray_direction, 0.0,
                                                              scene_t, planet_center);
    vec3 scene_color = cubey_srgb_to_linear(mix(vec3(0.16, 0.18, 0.14),
                                                vec3(0.55, 0.55, 0.50), ramp));
    return scene_color * atmosphere_sample.transmittance + atmosphere_sample.color;
}

vec3 debug_optical_depth(OpticalDepth depth) {
    return vec3(depth.rayleigh * 0.020, depth.mie * 0.080, depth.ozone * 0.035);
}

void main() {
    float tan_half_fovy = atmosphere.camera_up_tan_half_fovy.w;
    vec3 ray_direction = normalize(
        atmosphere.camera_forward_debug_view.xyz +
        atmosphere.camera_right_aspect.xyz * frag_ndc.x * atmosphere.camera_right_aspect.w *
            tan_half_fovy -
        atmosphere.camera_up_tan_half_fovy.xyz * frag_ndc.y * tan_half_fovy);
    vec3 planet_center = vec3(0.0, -atmosphere.radii_ground.x - atmosphere.radii_ground.z, 0.0);

    vec2 atmosphere_hit = ray_sphere_intersection(vec3(0.0), ray_direction, planet_center,
                                                  atmosphere.radii_ground.y);
    if (atmosphere_hit.y <= 0.0) {
        vec3 space_color = sun_disk_luminance(ray_direction, planet_center);
        out_color = vec4(cubey_pbr_apply_display_transform(space_color,
                                                           atmosphere.display_transform), 1.0);
        return;
    }

    float ray_start = max(atmosphere_hit.x, 0.0);
    float ray_end = atmosphere_hit.y;
    float ground_t = nearest_positive_ground_hit(vec3(0.0), ray_direction, planet_center);
    bool hit_ground = ground_t > 0.0 && ground_t < ray_end;
    if (hit_ground) {
        ray_end = ground_t;
    }

    int debug_view = int(atmosphere.camera_forward_debug_view.w + 0.5);
    AtmosphereSample atmosphere_sample = integrate_atmosphere(vec3(0.0), ray_direction, ray_start,
                                                              ray_end, planet_center);
    vec3 color = atmosphere_sample.color + sun_disk_luminance(ray_direction, planet_center);
    if (hit_ground) {
        color += ground_radiance(ray_direction, planet_center, ground_t) *
                 atmosphere_sample.transmittance;
    }

    if (debug_view == 1) {
        color = atmosphere_sample.rayleigh;
    } else if (debug_view == 2) {
        color = atmosphere_sample.mie;
    } else if (debug_view == 3) {
        color = atmosphere_sample.transmittance;
    } else if (debug_view == 4) {
        color = debug_optical_depth(atmosphere_sample.optical_depth);
    } else if (debug_view == 5) {
        color = sun_disk_luminance(ray_direction, planet_center);
    } else if (debug_view == 6) {
        color = render_aerial_perspective_debug(ray_direction, planet_center);
    }

    out_color = vec4(cubey_pbr_apply_display_transform(color, atmosphere.display_transform), 1.0);
}
