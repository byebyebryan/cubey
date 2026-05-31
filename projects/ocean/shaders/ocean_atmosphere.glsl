#ifndef CUBEY_OCEAN_ATMOSPHERE_GLSL
#define CUBEY_OCEAN_ATMOSPHERE_GLSL

const float OCEAN_ATMOSPHERE_PI = 3.14159265359;
const int OCEAN_ATMOSPHERE_VIEW_SAMPLE_COUNT = 8;
const int OCEAN_ATMOSPHERE_LIGHT_SAMPLE_COUNT = 4;
const float OCEAN_ATMOSPHERE_SUN_INTENSITY = 14.0;
const float OCEAN_ATMOSPHERE_MIN_SKY_Y = 0.060;
const float OCEAN_ATMOSPHERE_BOTTOM_RADIUS_KM = 6371.0;
const float OCEAN_ATMOSPHERE_TOP_RADIUS_KM = 6471.0;
const float OCEAN_ATMOSPHERE_CAMERA_ALTITUDE_KM = 0.15;
const float OCEAN_ATMOSPHERE_SUN_RADIUS = 0.004675;

struct OceanAtmosphereOpticalDepth {
    float rayleigh;
    float mie;
    float ozone;
};

struct OceanAtmosphereSample {
    vec3 color;
    vec3 transmittance;
    OceanAtmosphereOpticalDepth optical_depth;
};

vec3 ocean_sun_direction() {
    return normalize(vec3(-0.32, 0.34, -0.88));
}

vec3 ocean_sky_lookup_direction(vec3 direction) {
    vec3 ray_direction = normalize(direction);
    ray_direction.y = max(ray_direction.y, OCEAN_ATMOSPHERE_MIN_SKY_Y);
    return normalize(ray_direction);
}

vec3 ocean_atmosphere_grade(vec3 color) {
    return color * vec3(0.82, 0.94, 1.12);
}

vec3 ocean_rayleigh_scattering() {
    return vec3(0.005802, 0.013558, 0.033100);
}

float ocean_rayleigh_scale_height_km() {
    return 8.0;
}

float ocean_mie_scattering() {
    return 0.00165;
}

float ocean_mie_extinction() {
    return 0.00220;
}

float ocean_mie_scale_height_km() {
    return 1.2;
}

float ocean_mie_anisotropy() {
    return 0.80;
}

vec3 ocean_ozone_absorption() {
    return vec3(0.000650, 0.001881, 0.000085);
}

vec2 ocean_ray_sphere_intersection(vec3 ray_origin, vec3 ray_direction, vec3 sphere_center,
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

OceanAtmosphereOpticalDepth ocean_optical_depth_zero() {
    return OceanAtmosphereOpticalDepth(0.0, 0.0, 0.0);
}

OceanAtmosphereOpticalDepth ocean_optical_depth_add(OceanAtmosphereOpticalDepth lhs,
                                                    OceanAtmosphereOpticalDepth rhs) {
    return OceanAtmosphereOpticalDepth(lhs.rayleigh + rhs.rayleigh, lhs.mie + rhs.mie,
                                       lhs.ozone + rhs.ozone);
}

OceanAtmosphereOpticalDepth ocean_optical_depth_scale(OceanAtmosphereOpticalDepth value,
                                                      float scale) {
    return OceanAtmosphereOpticalDepth(value.rayleigh * scale, value.mie * scale,
                                       value.ozone * scale);
}

float ocean_altitude_at(vec3 position, vec3 planet_center) {
    return max(length(position - planet_center) - OCEAN_ATMOSPHERE_BOTTOM_RADIUS_KM, 0.0);
}

float ocean_ozone_density(float altitude_km) {
    return max(1.0 - abs(altitude_km - 25.0) / 15.0, 0.0);
}

OceanAtmosphereOpticalDepth ocean_sample_density(vec3 position, vec3 planet_center) {
    float altitude = ocean_altitude_at(position, planet_center);
    return OceanAtmosphereOpticalDepth(
        exp(-altitude / ocean_rayleigh_scale_height_km()),
        exp(-altitude / ocean_mie_scale_height_km()),
        ocean_ozone_density(altitude)
    );
}

vec3 ocean_transmittance_from_depth(OceanAtmosphereOpticalDepth depth) {
    vec3 rayleigh_extinction = ocean_rayleigh_scattering() * depth.rayleigh;
    vec3 mie_extinction = vec3(ocean_mie_extinction() * depth.mie);
    vec3 ozone_extinction = ocean_ozone_absorption() * depth.ozone;
    return exp(-(rayleigh_extinction + mie_extinction + ozone_extinction));
}

OceanAtmosphereOpticalDepth ocean_integrate_optical_depth(
    vec3 origin, vec3 direction, float ray_length, vec3 planet_center, int sample_count) {
    if (ray_length <= 0.0) {
        return ocean_optical_depth_zero();
    }
    int clamped_sample_count = clamp(sample_count, 1, OCEAN_ATMOSPHERE_LIGHT_SAMPLE_COUNT);
    OceanAtmosphereOpticalDepth optical_depth = ocean_optical_depth_zero();
    float step_length = ray_length / float(clamped_sample_count);
    for (int i = 0; i < OCEAN_ATMOSPHERE_LIGHT_SAMPLE_COUNT; ++i) {
        if (i >= clamped_sample_count) {
            break;
        }
        float t = (float(i) + 0.5) * step_length;
        optical_depth = ocean_optical_depth_add(
            optical_depth,
            ocean_optical_depth_scale(ocean_sample_density(origin + direction * t, planet_center),
                                      step_length));
    }
    return optical_depth;
}

float ocean_rayleigh_phase(float cos_theta) {
    return (3.0 / (16.0 * OCEAN_ATMOSPHERE_PI)) * (1.0 + cos_theta * cos_theta);
}

float ocean_mie_phase(float cos_theta) {
    float g = clamp(ocean_mie_anisotropy(), 0.0, 0.98);
    float g2 = g * g;
    float denom = pow(max(1.0 + g2 - 2.0 * g * cos_theta, 0.0001), 1.5);
    return (3.0 / (8.0 * OCEAN_ATMOSPHERE_PI)) *
           ((1.0 - g2) * (1.0 + cos_theta * cos_theta)) /
           max((2.0 + g2) * denom, 0.0001);
}

float ocean_sun_visibility(vec3 sample_position, vec3 sun_direction, vec3 planet_center) {
    vec3 to_planet_center = planet_center - sample_position;
    float center_distance = length(to_planet_center);
    float planet_angular_radius =
        asin(clamp(OCEAN_ATMOSPHERE_BOTTOM_RADIUS_KM / max(center_distance, 0.0001), 0.0, 1.0));
    float sun_center_angle =
        acos(clamp(dot(sun_direction, normalize(to_planet_center)), -1.0, 1.0));
    float limb_clearance = sun_center_angle - planet_angular_radius;
    float softness = max(OCEAN_ATMOSPHERE_SUN_RADIUS * 4.0, 0.022);
    return smoothstep(-softness, softness, limb_clearance);
}

OceanAtmosphereSample ocean_integrate_atmosphere(vec3 ray_origin, vec3 ray_direction,
                                                 float ray_start, float ray_end,
                                                 vec3 planet_center) {
    OceanAtmosphereSample result;
    result.color = vec3(0.0);
    result.transmittance = vec3(1.0);
    result.optical_depth = ocean_optical_depth_zero();

    float ray_length = max(ray_end - ray_start, 0.0);
    if (ray_length <= 0.0) {
        return result;
    }

    vec3 sun_direction = ocean_sun_direction();
    float cos_theta = dot(ray_direction, sun_direction);
    float rayleigh_phase_value = ocean_rayleigh_phase(cos_theta);
    float mie_phase_value = ocean_mie_phase(cos_theta);
    float step_length = ray_length / float(OCEAN_ATMOSPHERE_VIEW_SAMPLE_COUNT);

    vec3 rayleigh = vec3(0.0);
    vec3 mie = vec3(0.0);
    for (int i = 0; i < OCEAN_ATMOSPHERE_VIEW_SAMPLE_COUNT; ++i) {
        float t = ray_start + (float(i) + 0.5) * step_length;
        vec3 sample_position = ray_origin + ray_direction * t;
        OceanAtmosphereOpticalDepth density = ocean_sample_density(sample_position, planet_center);
        result.optical_depth = ocean_optical_depth_add(
            result.optical_depth, ocean_optical_depth_scale(density, step_length));

        vec2 sun_atmosphere_hit = ocean_ray_sphere_intersection(
            sample_position, sun_direction, planet_center, OCEAN_ATMOSPHERE_TOP_RADIUS_KM);
        float sun_ray_length = max(sun_atmosphere_hit.y, 0.0);
        float solar_visibility = ocean_sun_visibility(sample_position, sun_direction, planet_center);
        if (solar_visibility <= 0.0001) {
            continue;
        }

        OceanAtmosphereOpticalDepth light_depth = ocean_integrate_optical_depth(
            sample_position, sun_direction, sun_ray_length, planet_center,
            OCEAN_ATMOSPHERE_LIGHT_SAMPLE_COUNT);
        OceanAtmosphereOpticalDepth total_depth =
            ocean_optical_depth_add(result.optical_depth, light_depth);
        vec3 transmittance = ocean_transmittance_from_depth(total_depth);
        rayleigh += transmittance * density.rayleigh * ocean_rayleigh_scattering() *
                    rayleigh_phase_value * step_length * solar_visibility;
        mie += transmittance * density.mie * vec3(ocean_mie_scattering()) *
               mie_phase_value * step_length * solar_visibility;
    }

    result.color = (rayleigh + mie) * OCEAN_ATMOSPHERE_SUN_INTENSITY;
    result.transmittance = ocean_transmittance_from_depth(result.optical_depth);
    return result;
}

float ocean_nearest_positive_ground_hit(vec3 ray_origin, vec3 ray_direction, vec3 planet_center) {
    vec2 ground_hit = ocean_ray_sphere_intersection(ray_origin, ray_direction, planet_center,
                                                    OCEAN_ATMOSPHERE_BOTTOM_RADIUS_KM);
    if (ground_hit.x > 0.0) {
        return ground_hit.x;
    }
    if (ground_hit.y > 0.0) {
        return ground_hit.y;
    }
    return -1.0;
}

vec3 ocean_sun_disk_luminance(vec3 ray_direction, vec3 planet_center) {
    vec3 sun_direction = ocean_sun_direction();
    float sun_cos = dot(ray_direction, sun_direction);
    float sun_cutoff = cos(OCEAN_ATMOSPHERE_SUN_RADIUS);
    float disk = smoothstep(sun_cutoff, min(1.0, sun_cutoff + 0.00045), sun_cos);
    if (disk <= 0.0) {
        return vec3(0.0);
    }
    vec2 atmosphere_hit = ocean_ray_sphere_intersection(vec3(0.0), ray_direction, planet_center,
                                                        OCEAN_ATMOSPHERE_TOP_RADIUS_KM);
    float ray_end = max(atmosphere_hit.y, 0.0);
    OceanAtmosphereOpticalDepth depth = ocean_integrate_optical_depth(
        vec3(0.0), ray_direction, ray_end, planet_center, OCEAN_ATMOSPHERE_LIGHT_SAMPLE_COUNT);
    return ocean_atmosphere_grade(ocean_transmittance_from_depth(depth) * disk *
                                  OCEAN_ATMOSPHERE_SUN_INTENSITY);
}

vec3 ocean_sky_color(vec3 direction) {
    vec3 ray_direction = ocean_sky_lookup_direction(direction);
    vec3 planet_center =
        vec3(0.0, -OCEAN_ATMOSPHERE_BOTTOM_RADIUS_KM - OCEAN_ATMOSPHERE_CAMERA_ALTITUDE_KM, 0.0);
    vec2 atmosphere_hit = ocean_ray_sphere_intersection(vec3(0.0), ray_direction, planet_center,
                                                        OCEAN_ATMOSPHERE_TOP_RADIUS_KM);
    if (atmosphere_hit.y <= 0.0) {
        return ocean_sun_disk_luminance(ray_direction, planet_center);
    }

    float ray_start = max(atmosphere_hit.x, 0.0);
    float ray_end = atmosphere_hit.y;
    float ground_t = ocean_nearest_positive_ground_hit(vec3(0.0), ray_direction, planet_center);
    bool hit_ground = ground_t > 0.0 && ground_t < ray_end;
    if (hit_ground) {
        ray_end = ground_t;
    }

    OceanAtmosphereSample atmosphere_sample =
        ocean_integrate_atmosphere(vec3(0.0), ray_direction, ray_start, ray_end, planet_center);
    vec3 sun_disk = hit_ground ? vec3(0.0) : ocean_sun_disk_luminance(ray_direction, planet_center);
    return ocean_atmosphere_grade(atmosphere_sample.color) + sun_disk;
}

#endif
