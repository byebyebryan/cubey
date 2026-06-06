#ifndef CUBEY_PLANET_ATMOSPHERE_GLSL
#define CUBEY_PLANET_ATMOSPHERE_GLSL

struct PlanetAtmosphereTerms {
    float ray_up;
    float sun_elevation;
    float toward_sun;
    float above_horizon;
    float upper_sky;
    float horizon_shell;
    float horizon;
    float daylight;
    float twilight;
    float atmosphere_visibility;
    float star_visibility;
};

float planet_atmosphere_toward_sun(vec3 ray_direction, vec3 camera_up, vec3 sun_direction) {
    float ray_up = dot(ray_direction, camera_up);
    float sun_elevation = dot(sun_direction, camera_up);
    vec3 sun_tangent = sun_direction - camera_up * sun_elevation;
    vec3 view_tangent = ray_direction - camera_up * ray_up;
    float toward_sun = 0.0;
    if (length(sun_tangent) > 0.0001 && length(view_tangent) > 0.0001) {
        toward_sun = pow(max(dot(normalize(view_tangent), normalize(sun_tangent)), 0.0), 2.0);
    }
    return toward_sun;
}

PlanetAtmosphereTerms planet_atmosphere_terms(vec3 ray_direction, vec3 camera_up,
                                              vec3 sun_direction) {
    PlanetAtmosphereTerms terms;
    terms.ray_up = dot(ray_direction, camera_up);
    terms.sun_elevation = dot(sun_direction, camera_up);
    terms.toward_sun = planet_atmosphere_toward_sun(ray_direction, camera_up, sun_direction);
    terms.above_horizon = smoothstep(-0.055, 0.075, terms.ray_up);
    terms.upper_sky = smoothstep(0.02, 0.68, terms.ray_up);
    terms.horizon_shell = exp(-abs(terms.ray_up) / 0.13);
    terms.horizon = terms.horizon_shell * terms.above_horizon;
    terms.daylight = smoothstep(-0.08, 0.24, terms.sun_elevation);
    terms.twilight =
        (1.0 - smoothstep(0.08, 0.42, abs(terms.sun_elevation))) *
        smoothstep(-0.28, 0.06, terms.sun_elevation);
    terms.atmosphere_visibility =
        clamp(max(terms.daylight, terms.twilight * 0.72), 0.0, 1.0);
    terms.star_visibility = 1.0 - smoothstep(-0.08, 0.18, terms.sun_elevation);
    return terms;
}

vec3 planet_atmosphere_scatter_color(float sun_elevation, float toward_sun, float horizon) {
    vec3 day_haze = mix(vec3(0.055, 0.105, 0.205), vec3(0.20, 0.36, 0.68),
                        clamp(sun_elevation * 1.2 + 0.35, 0.0, 1.0));
    vec3 twilight = vec3(1.00, 0.42, 0.15);
    float twilight_window =
        (1.0 - smoothstep(0.10, 0.52, abs(sun_elevation))) *
        smoothstep(-0.28, 0.06, sun_elevation);
    float warm = twilight_window * horizon * (0.32 + 0.68 * toward_sun);
    return mix(day_haze, twilight, clamp(warm, 0.0, 1.0));
}

vec3 planet_atmosphere_scatter_color(PlanetAtmosphereTerms terms) {
    return planet_atmosphere_scatter_color(terms.sun_elevation, terms.toward_sun, terms.horizon);
}

#endif
