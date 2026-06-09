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

struct PlanetAtmosphereScatterSample {
    vec3 radiance;
    vec3 transmittance;
    float optical_depth;
    float ray_length;
};

const float kPlanetAtmosphereLargeDistance = 1.0e20;
const float kPlanetAtmospherePi = 3.14159265358979323846;
const vec3 kPlanetAtmosphereBetaRayleigh = vec3(5.8e-6, 13.5e-6, 33.1e-6) * 1.8;
const vec3 kPlanetAtmosphereBetaMieScattering = vec3(2.1e-5);
const vec3 kPlanetAtmosphereBetaMieExtinction = vec3(2.8e-5);
const float kPlanetAtmosphereRayleighScaleHeightM = 8000.0;
const float kPlanetAtmosphereMieScaleHeightM = 1200.0;
const float kPlanetAtmosphereMieG = 0.76;

vec2 planet_atmosphere_sphere_intersection(vec3 origin, vec3 direction, float radius) {
    float b = dot(origin, direction);
    float c = dot(origin, origin) - radius * radius;
    float h = b * b - c;
    if (h < 0.0) {
        return vec2(kPlanetAtmosphereLargeDistance, -kPlanetAtmosphereLargeDistance);
    }
    h = sqrt(h);
    return vec2(-b - h, -b + h);
}

float planet_atmosphere_density_rayleigh(float altitude_m) {
    return exp(-max(altitude_m, 0.0) / kPlanetAtmosphereRayleighScaleHeightM);
}

float planet_atmosphere_density_mie(float altitude_m) {
    return exp(-max(altitude_m, 0.0) / kPlanetAtmosphereMieScaleHeightM);
}

float planet_atmosphere_phase_rayleigh(float mu) {
    return (3.0 / (16.0 * kPlanetAtmospherePi)) * (1.0 + mu * mu);
}

float planet_atmosphere_phase_mie(float mu) {
    float g = kPlanetAtmosphereMieG;
    float denom = max(1.0 + g * g - 2.0 * g * mu, 0.001);
    return (3.0 / (8.0 * kPlanetAtmospherePi)) *
           ((1.0 - g * g) * (1.0 + mu * mu)) /
           ((2.0 + g * g) * pow(denom, 1.5));
}

vec2 planet_atmosphere_optical_depth(vec3 origin, vec3 direction, float max_distance,
                                     float planet_radius, float atmosphere_radius) {
    const int kDepthSteps = 4;
    vec2 atmosphere_hit =
        planet_atmosphere_sphere_intersection(origin, direction, atmosphere_radius);
    float t0 = max(atmosphere_hit.x, 0.0);
    float t1 = min(atmosphere_hit.y, max_distance);
    if (t1 <= t0) {
        return vec2(0.0);
    }

    float step_length = (t1 - t0) / float(kDepthSteps);
    vec2 optical_depth = vec2(0.0);
    for (int i = 0; i < kDepthSteps; ++i) {
        float t = t0 + (float(i) + 0.5) * step_length;
        vec3 sample_position = origin + direction * t;
        float altitude = length(sample_position) - planet_radius;
        optical_depth.x += planet_atmosphere_density_rayleigh(altitude) * step_length;
        optical_depth.y += planet_atmosphere_density_mie(altitude) * step_length;
    }
    return optical_depth;
}

vec3 planet_atmosphere_sun_transmittance(vec3 position, vec3 sun_direction,
                                         float planet_radius, float atmosphere_radius) {
    vec3 start = position + sun_direction * max(planet_radius * 0.00002, 8.0);
    vec2 ground_hit = planet_atmosphere_sphere_intersection(start, sun_direction, planet_radius);
    if (ground_hit.x > 0.0 && ground_hit.x < kPlanetAtmosphereLargeDistance * 0.5) {
        return vec3(0.0);
    }

    vec2 atmosphere_hit =
        planet_atmosphere_sphere_intersection(start, sun_direction, atmosphere_radius);
    if (atmosphere_hit.y <= 0.0) {
        return vec3(1.0);
    }
    vec2 depth = planet_atmosphere_optical_depth(start, sun_direction, atmosphere_hit.y,
                                                planet_radius, atmosphere_radius);
    vec3 tau = kPlanetAtmosphereBetaRayleigh * depth.x +
               kPlanetAtmosphereBetaMieExtinction * depth.y;
    return exp(-tau);
}

PlanetAtmosphereScatterSample planet_atmosphere_integrate_ray(
    vec3 camera_position, vec3 ray_direction, float max_ray_distance, float planet_radius,
    float atmosphere_radius, vec3 sun_direction, vec3 sun_color, float sun_intensity) {
    const int kViewSteps = 8;
    PlanetAtmosphereScatterSample result;
    result.radiance = vec3(0.0);
    result.transmittance = vec3(1.0);
    result.optical_depth = 0.0;
    result.ray_length = 0.0;

    vec2 atmosphere_hit =
        planet_atmosphere_sphere_intersection(camera_position, ray_direction, atmosphere_radius);
    float t0 = max(atmosphere_hit.x, 0.0);
    float t1 = atmosphere_hit.y;
    if (t1 <= t0) {
        return result;
    }

    vec2 ground_hit =
        planet_atmosphere_sphere_intersection(camera_position, ray_direction, planet_radius);
    if (ground_hit.x > 0.0) {
        t1 = min(t1, ground_hit.x);
    }
    if (max_ray_distance >= 0.0) {
        t1 = min(t1, max_ray_distance);
    }
    if (t1 <= t0) {
        return result;
    }

    float mu = clamp(dot(ray_direction, sun_direction), -1.0, 1.0);
    float phase_rayleigh = planet_atmosphere_phase_rayleigh(mu);
    float phase_mie = planet_atmosphere_phase_mie(mu);
    float step_length = (t1 - t0) / float(kViewSteps);
    vec2 view_depth = vec2(0.0);
    vec3 scatter_rayleigh = vec3(0.0);
    vec3 scatter_mie = vec3(0.0);

    for (int i = 0; i < kViewSteps; ++i) {
        float t = t0 + (float(i) + 0.5) * step_length;
        vec3 sample_position = camera_position + ray_direction * t;
        float altitude = length(sample_position) - planet_radius;
        float density_rayleigh = planet_atmosphere_density_rayleigh(altitude);
        float density_mie = planet_atmosphere_density_mie(altitude);
        view_depth += vec2(density_rayleigh, density_mie) * step_length;

        vec3 sun_transmittance =
            planet_atmosphere_sun_transmittance(sample_position, sun_direction, planet_radius,
                                                atmosphere_radius);
        vec3 view_tau = kPlanetAtmosphereBetaRayleigh * view_depth.x +
                        kPlanetAtmosphereBetaMieExtinction * view_depth.y;
        vec3 view_transmittance = exp(-view_tau);
        scatter_rayleigh += density_rayleigh * sun_transmittance * view_transmittance *
                            step_length;
        scatter_mie += density_mie * sun_transmittance * view_transmittance * step_length;
    }

    vec3 tau =
        kPlanetAtmosphereBetaRayleigh * view_depth.x + kPlanetAtmosphereBetaMieExtinction *
                                                       view_depth.y;
    result.transmittance = exp(-tau);
    result.optical_depth = dot(view_depth, vec2(0.5));
    result.ray_length = t1 - t0;
    vec3 radiance =
        kPlanetAtmosphereBetaRayleigh * phase_rayleigh * scatter_rayleigh +
        kPlanetAtmosphereBetaMieScattering * phase_mie * scatter_mie;
    result.radiance = radiance * sun_color * max(sun_intensity, 0.0) * 22.0;
    return result;
}

#endif
