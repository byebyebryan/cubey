#ifndef CUBEY_ATMOSPHERE_NIGHT_SKY_GLSL
#define CUBEY_ATMOSPHERE_NIGHT_SKY_GLSL

vec3 night_airglow_radiance(vec3 ray_direction, vec3 sun_direction) {
    float sun_elevation = sun_elevation_degrees(sun_direction);
    float night = star_visibility(sun_elevation);
    if (night <= 0.0) {
        return vec3(0.0);
    }

    float above_horizon = smoothstep(-0.045, 0.080, ray_direction.y);
    float horizon = exp(-max(ray_direction.y, 0.0) / 0.26) * above_horizon;
    float upper_sky = smoothstep(0.02, 0.82, ray_direction.y) * above_horizon;
    float star_control = clamp(atmosphere.night_options.z, 0.0, 4.0);
    float pollution = clamp(atmosphere.milky_way_options.z, 0.0, 1.0);

    vec3 horizon_color = cubey_srgb_to_linear(vec3(0.020, 0.030, 0.072));
    vec3 zenith_color = cubey_srgb_to_linear(vec3(0.008, 0.015, 0.048));
    vec3 pollution_haze = cubey_srgb_to_linear(vec3(0.045, 0.034, 0.026)) * horizon * pollution;
    vec3 airglow = mix(horizon_color, zenith_color, upper_sky) * (0.42 + 0.58 * star_control);
    return (airglow + pollution_haze * 0.36) * night * above_horizon * 0.48;
}

float surface_airmass_star_visibility(float ray_up) {
    float above_horizon = smoothstep(-0.015, 0.080, ray_up);
    float horizon_airmass = exp(-max(ray_up, 0.0) / 0.16);
    float clear_air = 1.0 - smoothstep(0.12, 0.78, horizon_airmass);
    return above_horizon * clear_air;
}

float surface_dark_sky_visibility(float ray_up, float sun_elevation, float daylight,
                                  float twilight) {
    float astronomical_night = 1.0 - smoothstep(-0.48, -0.31, sun_elevation);
    float twilight_cut = 1.0 - smoothstep(0.005, 0.14, twilight);
    float daylight_cut = pow(clamp(1.0 - daylight, 0.0, 1.0), 5.0);
    return clamp(astronomical_night * twilight_cut * daylight_cut *
                     surface_airmass_star_visibility(ray_up),
                 0.0, 1.0);
}

float surface_twilight_window(float sun_elevation, float daylight, float twilight) {
    float low_sun = smoothstep(-0.50, -0.015, sun_elevation);
    float daylight_fade = 1.0 - smoothstep(0.08, 0.42, sun_elevation);
    float exposure_fade = 1.0 - smoothstep(0.38, 0.96, daylight);
    return clamp(max(twilight, low_sun * daylight_fade) * exposure_fade, 0.0, 1.0);
}

vec3 surface_twilight_radiance(float ray_up, float sun_elevation, float toward_sun,
                               float daylight, float twilight) {
    float window = surface_twilight_window(sun_elevation, daylight, twilight);
    float above_horizon = smoothstep(-0.045, 0.075, ray_up);
    float horizon_band = exp(-abs(ray_up) / 0.20) * above_horizon;
    float upper_band = smoothstep(0.02, 0.42, ray_up) *
                       (1.0 - smoothstep(0.68, 1.00, ray_up)) * above_horizon;
    float sun_lobe = pow(clamp(toward_sun, 0.0, 1.0), 0.38);
    float antisun_lobe = pow(clamp(1.0 - toward_sun, 0.0, 1.0), 0.55);
    float sun_horizon = horizon_band * (0.06 + 0.94 * sun_lobe);

    vec3 ember = cubey_srgb_to_linear(vec3(1.00, 0.34, 0.08)) * sun_horizon * 0.13;
    vec3 gold = cubey_srgb_to_linear(vec3(1.00, 0.62, 0.24)) * horizon_band *
                smoothstep(-0.24, 0.12, sun_elevation) * (0.030 + 0.055 * sun_lobe);
    vec3 violet = cubey_srgb_to_linear(vec3(0.22, 0.18, 0.48)) * upper_band *
                  (0.36 + 0.36 * sun_lobe + 0.36 * antisun_lobe) * 0.24;
    return (ember + gold + violet) * window;
}

vec3 twilight_radiance(vec3 ray_direction, vec3 sun_direction) {
    float sun_elevation = sun_elevation_degrees(sun_direction);
    float legacy_visibility = twilight_visibility(sun_elevation);
    float sun_elevation_dot = clamp(sun_direction.y, -1.0, 1.0);
    float daylight = smoothstep(-0.08, 0.24, sun_elevation_dot);
    float twilight_window = exp(-abs(sun_elevation_dot) / 0.16) *
                            smoothstep(-0.22, 0.08, sun_elevation_dot);
    if (legacy_visibility <= 0.0 && twilight_window <= 0.0) {
        return vec3(0.0);
    }

    float horizon = 1.0 - smoothstep(0.02, 0.42, abs(ray_direction.y));
    float upper_sky = smoothstep(-0.10, 0.65, ray_direction.y);
    vec3 ray_horizontal = safe_horizontal_direction(ray_direction, sun_direction);
    vec3 sun_horizontal = safe_horizontal_direction(sun_direction, vec3(0.0, 0.0, 1.0));
    float sun_azimuth_lobe = pow(max(dot(ray_horizontal, sun_horizontal), 0.0), 2.5);
    float horizon_warmth = atmosphere.night_options.y;

    vec3 zenith = cubey_srgb_to_linear(vec3(0.010, 0.024, 0.074)) * 0.18;
    vec3 horizon_blue = cubey_srgb_to_linear(vec3(0.075, 0.092, 0.155)) * 0.20;
    vec3 sun_warmth = cubey_srgb_to_linear(vec3(1.0, 0.34, 0.09)) * 0.10;
    vec3 twilight_color = mix(horizon_blue, zenith, upper_sky);
    twilight_color += sun_warmth * horizon * sun_azimuth_lobe * horizon_warmth;
    vec3 surface_twilight =
        surface_twilight_radiance(ray_direction.y, sun_elevation_dot, sun_azimuth_lobe, daylight,
                                  twilight_window);
    return (twilight_color * legacy_visibility + surface_twilight) * atmosphere.night_options.x;
}

vec3 rotate_around_axis(vec3 value, vec3 axis, float cos_angle, float sin_angle) {
    return value * cos_angle + cross(axis, value) * sin_angle +
           axis * dot(axis, value) * (1.0 - cos_angle);
}

vec3 star_sample_direction(vec3 ray_direction) {
    vec3 celestial_pole =
        normalize(vec3(0.0, atmosphere.celestial_options.z, atmosphere.celestial_options.w));
    return rotate_around_axis(ray_direction, celestial_pole, atmosphere.celestial_options.x,
                              -atmosphere.celestial_options.y);
}

float night_haze_visibility() {
    return 1.0 - smoothstep(0.006, 0.018, atmosphere.mie.x);
}

float night_sky_camera_mode() {
    return step(0.5, atmosphere.milky_way_options.w);
}

float moon_washout(vec3 ray_direction, float base_strength, float lobe_strength,
                   float min_visibility) {
    vec3 moon_direction = normalize(atmosphere.moon_direction_radius.xyz);
    float moon_strength = atmosphere.moon_options.x * atmosphere.moon_options.y *
                          atmosphere.moon_options.w;
    float moon_angle = acos(clamp(dot(ray_direction, moon_direction), -1.0, 1.0));
    float moon_radius = max(atmosphere.moon_direction_radius.w, 0.0001);
    float lobe_inner = max(moon_radius * 1.8, moon_radius + fwidth(moon_angle) * 2.0);
    float lobe_outer = moon_radius * 12.0;
    float moon_lobe = 1.0 - smoothstep(lobe_inner, lobe_outer, moon_angle);
    return clamp(1.0 - moon_strength * (base_strength + moon_lobe * lobe_strength),
                 min_visibility, 1.0);
}

float night_object_visibility(vec3 ray_direction, vec3 sun_direction, float horizon_end,
                              float moon_base, float moon_lobe, float moon_min,
                              float pollution_min) {
    float sun_elevation_dot = clamp(sun_direction.y, -1.0, 1.0);
    float daylight = smoothstep(-0.08, 0.24, sun_elevation_dot);
    float twilight = exp(-abs(sun_elevation_dot) / 0.16) *
                     smoothstep(-0.22, 0.08, sun_elevation_dot);
    float night = star_visibility(sun_elevation_degrees(sun_direction));
    float horizon = smoothstep(-0.03, horizon_end, ray_direction.y);
    float surface_dark =
        surface_dark_sky_visibility(ray_direction.y, sun_elevation_dot, daylight, twilight);
    float pollution = mix(1.0, pollution_min, clamp(atmosphere.milky_way_options.z, 0.0, 1.0));
    return max(night * horizon, surface_dark) * night_haze_visibility() * pollution *
           moon_washout(ray_direction, moon_base, moon_lobe, moon_min);
}

float star_limiting_magnitude(vec3 ray_direction, vec3 sun_direction) {
    float night = star_visibility(sun_elevation_degrees(sun_direction));
    float moon_clear = moon_washout(ray_direction, 0.55, 0.92, 0.22);
    float horizon_clear = smoothstep(-0.02, 0.42, ray_direction.y);
    float pollution = clamp(atmosphere.milky_way_options.z, 0.0, 1.0);
    float dark_sky_limit = mix(6.25, 7.65, night_sky_camera_mode());
    float limiting_magnitude = mix(-0.8, dark_sky_limit, night);
    limiting_magnitude -= (1.0 - moon_clear) * 2.9;
    limiting_magnitude -= (1.0 - horizon_clear) * 1.35;
    limiting_magnitude -= (1.0 - night_haze_visibility()) * 2.2;
    limiting_magnitude -= pollution * 3.2;
    return clamp(limiting_magnitude, -1.2, 7.75);
}

float space_sun_clear_visibility(vec3 ray_direction, vec3 sun_direction) {
    float sun_angle = acos(clamp(dot(ray_direction, sun_direction), -1.0, 1.0));
    return smoothstep(0.18, 0.62, sun_angle);
}

float space_night_object_visibility(vec3 ray_direction, vec3 sun_direction,
                                    float moon_lobe, float moon_min) {
    return space_sun_clear_visibility(ray_direction, sun_direction) *
           moon_washout(ray_direction, 0.0, moon_lobe, moon_min);
}

float space_star_limiting_magnitude(vec3 ray_direction, vec3 sun_direction) {
    float camera_mode = night_sky_camera_mode();
    float sun_clear = space_sun_clear_visibility(ray_direction, sun_direction);
    float moon_clear = moon_washout(ray_direction, 0.0, mix(0.90, 0.68, camera_mode),
                                    mix(0.24, 0.42, camera_mode));
    float dark_sky_limit = mix(6.25, 7.85, camera_mode);
    float limiting_magnitude = mix(-1.0, dark_sky_limit, sun_clear);
    limiting_magnitude -= (1.0 - moon_clear) * mix(2.5, 1.6, camera_mode);
    return clamp(limiting_magnitude, -1.2, 7.95);
}

#include "atmosphere_stars.glsl"

vec3 procedural_star_radiance(vec3 ray_direction, vec3 sun_direction) {
    float visibility = night_object_visibility(ray_direction, sun_direction, 0.18, 0.10, 0.24,
                                               0.18, 0.20) *
                       atmosphere.night_options.z;
    if (visibility <= 0.0 || atmosphere.night_options.w <= 0.0) {
        return vec3(0.0);
    }

    float limiting_magnitude = star_limiting_magnitude(ray_direction, sun_direction);
    vec3 sky_direction = star_sample_direction(ray_direction);
    return star_field_radiance(sky_direction, limiting_magnitude, night_sky_camera_mode()) *
           visibility;
}

vec3 space_procedural_star_radiance(vec3 ray_direction, vec3 sun_direction) {
    float visibility = space_night_object_visibility(ray_direction, sun_direction, 0.76, 0.30) *
                       atmosphere.night_options.z;
    if (visibility <= 0.0 || atmosphere.night_options.w <= 0.0) {
        return vec3(0.0);
    }

    float limiting_magnitude = space_star_limiting_magnitude(ray_direction, sun_direction);
    vec3 sky_direction = star_sample_direction(ray_direction);
    return star_field_radiance(sky_direction, limiting_magnitude, night_sky_camera_mode()) *
           visibility * 1.35;
}

vec3 milky_way_radiance_with_visibility(vec3 ray_direction, float visibility) {
    float source_intensity = atmosphere.milky_way_options.x;
    if (source_intensity <= 0.0) {
        return vec3(0.0);
    }

    vec3 atlas = max(texture(night_sky_atlas, star_sample_direction(ray_direction)).rgb, vec3(0.0));
    float luma = dot(atlas, vec3(0.2126, 0.7152, 0.0722));
    if (luma <= 0.0) {
        return vec3(0.0);
    }

    float camera_mode = night_sky_camera_mode();
    float saturation = mix(0.16, 0.72, camera_mode);
    vec3 color = mix(vec3(luma), atlas, saturation);
    float contrast = clamp(atmosphere.milky_way_options.y, 0.0, 4.0);
    float contrast_gain = mix(0.45, 1.55, contrast * 0.25);
    float exposure_gain = mix(0.85, 1.70, camera_mode);
    return color * source_intensity * contrast_gain * exposure_gain * visibility;
}

vec3 milky_way_radiance(vec3 ray_direction, vec3 sun_direction) {
    float visibility =
        night_object_visibility(ray_direction, sun_direction, 0.24, 0.34, 0.46, 0.06, 0.04);
    return milky_way_radiance_with_visibility(ray_direction, visibility);
}

vec3 space_milky_way_radiance(vec3 ray_direction, vec3 sun_direction) {
    float visibility = space_night_object_visibility(ray_direction, sun_direction, 0.46, 0.10);
    return milky_way_radiance_with_visibility(ray_direction, visibility);
}

vec3 galactic_debug_direction() {
    vec3 pole = normalize(vec3(0.31, 0.84, 0.44));
    vec3 center_hint = normalize(vec3(-0.45, -0.12, -0.89));
    vec3 center = normalize(center_hint - pole * dot(center_hint, pole));
    vec3 tangent = normalize(cross(pole, center));
    float longitude = frag_ndc.x * 3.14159265359;
    float latitude = frag_ndc.y * 0.52;
    float horizontal = cos(latitude);
    return normalize(center * cos(longitude) * horizontal +
                     tangent * sin(longitude) * horizontal + pole * sin(latitude));
}

vec3 render_milky_way_debug() {
    vec3 atlas = max(texture(night_sky_atlas, galactic_debug_direction()).rgb, vec3(0.0));
    float luma = dot(atlas, vec3(0.2126, 0.7152, 0.0722));
    return vec3(1.0) - exp(-mix(vec3(luma), atlas, 0.75) * 900.0);
}

vec3 night_sky_radiance(vec3 ray_direction, vec3 sun_direction) {
    return night_airglow_radiance(ray_direction, sun_direction) +
           twilight_radiance(ray_direction, sun_direction) +
           procedural_star_radiance(ray_direction, sun_direction) +
           milky_way_radiance(ray_direction, sun_direction);
}

vec3 space_night_sky_radiance(vec3 ray_direction, vec3 sun_direction) {
    return space_procedural_star_radiance(ray_direction, sun_direction) +
           space_milky_way_radiance(ray_direction, sun_direction);
}

#endif // CUBEY_ATMOSPHERE_NIGHT_SKY_GLSL
