#ifndef CUBEY_ATMOSPHERE_GLSL
#define CUBEY_ATMOSPHERE_GLSL

const float CUBEY_ATMOSPHERE_PI = 3.14159265359;
const int CUBEY_ATMOSPHERE_VIEW_SAMPLE_COUNT = 16;
const int CUBEY_ATMOSPHERE_LIGHT_SAMPLE_COUNT = 8;
const int CUBEY_ATMOSPHERE_VIEW_FINAL = 0;
const int CUBEY_ATMOSPHERE_VIEW_RAYLEIGH = 1;
const int CUBEY_ATMOSPHERE_VIEW_MIE = 2;
const int CUBEY_ATMOSPHERE_VIEW_TRANSMITTANCE = 3;
const int CUBEY_ATMOSPHERE_VIEW_OPTICAL_DEPTH = 4;
const int CUBEY_ATMOSPHERE_VIEW_SUN_DISK = 5;
const int CUBEY_ATMOSPHERE_VIEW_AERIAL_PERSPECTIVE = 6;
const int CUBEY_ATMOSPHERE_VIEW_NIGHT_SKY = 7;
const int CUBEY_ATMOSPHERE_VIEW_MILKY_WAY = 8;
const int CUBEY_ATMOSPHERE_VIEW_MOON = 9;
const int CUBEY_ATMOSPHERE_VIEW_MOON_SURFACE = 10;
const int CUBEY_ATMOSPHERE_VIEW_STARS = 11;

struct CubeyAtmosphereMedium {
    vec3 planet_center;
    float bottom_radius;
    float top_radius;
    vec3 rayleigh_scattering;
    float rayleigh_scale_height;
    float mie_scattering;
    float mie_extinction;
    float mie_scale_height;
    float mie_anisotropy;
    vec3 ozone_absorption;
    float ozone_center_altitude;
    float ozone_half_width;
    vec3 sun_direction;
    float sun_angular_radius;
    vec3 sun_radiance;
    float min_twilight_softness;
};

struct CubeyAtmosphereOpticalDepth {
    float rayleigh;
    float mie;
    float ozone;
};

struct CubeyAtmosphereSample {
    vec3 color;
    vec3 rayleigh;
    vec3 mie;
    vec3 transmittance;
    CubeyAtmosphereOpticalDepth optical_depth;
    float ray_length;
};

struct CubeyAtmosphereRaySegment {
    float start;
    float end;
    float ground_t;
    bool hit_atmosphere;
    bool hit_ground;
    bool camera_inside_atmosphere;
};

vec2 cubey_atmosphere_ray_sphere_intersection(vec3 ray_origin, vec3 ray_direction,
                                              vec3 sphere_center, float sphere_radius) {
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

vec3 cubey_atmosphere_transmittance_from_depth(vec3 rayleigh_scattering, float mie_extinction,
                                               vec3 ozone_absorption, float rayleigh_depth,
                                               float mie_depth, float ozone_depth) {
    vec3 rayleigh_extinction = rayleigh_scattering * rayleigh_depth;
    vec3 mie_extinction_rgb = vec3(mie_extinction * mie_depth);
    vec3 ozone_extinction = ozone_absorption * ozone_depth;
    return exp(-(rayleigh_extinction + mie_extinction_rgb + ozone_extinction));
}

float cubey_atmosphere_rayleigh_phase(float cos_theta) {
    return (3.0 / (16.0 * CUBEY_ATMOSPHERE_PI)) * (1.0 + cos_theta * cos_theta);
}

float cubey_atmosphere_mie_phase(float cos_theta, float anisotropy) {
    float g = clamp(anisotropy, 0.0, 0.98);
    float g2 = g * g;
    float denom = pow(max(1.0 + g2 - 2.0 * g * cos_theta, 0.0001), 1.5);
    return (3.0 / (8.0 * CUBEY_ATMOSPHERE_PI)) *
           ((1.0 - g2) * (1.0 + cos_theta * cos_theta)) /
           max((2.0 + g2) * denom, 0.0001);
}

float cubey_atmosphere_altitude_at(CubeyAtmosphereMedium medium, vec3 position) {
    return max(length(position - medium.planet_center) - medium.bottom_radius, 0.0);
}

float cubey_atmosphere_ozone_density(CubeyAtmosphereMedium medium, float altitude) {
    return max(1.0 - abs(altitude - medium.ozone_center_altitude) /
                         max(medium.ozone_half_width, 0.0001), 0.0);
}

CubeyAtmosphereOpticalDepth cubey_atmosphere_optical_depth_zero() {
    return CubeyAtmosphereOpticalDepth(0.0, 0.0, 0.0);
}

CubeyAtmosphereOpticalDepth cubey_atmosphere_optical_depth_add(
    CubeyAtmosphereOpticalDepth lhs, CubeyAtmosphereOpticalDepth rhs) {
    return CubeyAtmosphereOpticalDepth(lhs.rayleigh + rhs.rayleigh, lhs.mie + rhs.mie,
                                       lhs.ozone + rhs.ozone);
}

CubeyAtmosphereOpticalDepth cubey_atmosphere_optical_depth_scale(
    CubeyAtmosphereOpticalDepth value, float scale) {
    return CubeyAtmosphereOpticalDepth(value.rayleigh * scale, value.mie * scale,
                                       value.ozone * scale);
}

CubeyAtmosphereOpticalDepth cubey_atmosphere_sample_density(CubeyAtmosphereMedium medium,
                                                            vec3 position) {
    float altitude = cubey_atmosphere_altitude_at(medium, position);
    return CubeyAtmosphereOpticalDepth(
        exp(-altitude / max(medium.rayleigh_scale_height, 0.0001)),
        exp(-altitude / max(medium.mie_scale_height, 0.0001)),
        cubey_atmosphere_ozone_density(medium, altitude));
}

vec3 cubey_atmosphere_depth_transmittance(CubeyAtmosphereMedium medium,
                                          CubeyAtmosphereOpticalDepth depth) {
    return cubey_atmosphere_transmittance_from_depth(
        medium.rayleigh_scattering, medium.mie_extinction, medium.ozone_absorption,
        depth.rayleigh, depth.mie, depth.ozone);
}

CubeyAtmosphereOpticalDepth cubey_atmosphere_integrate_optical_depth(
    CubeyAtmosphereMedium medium, vec3 origin, vec3 direction, float ray_start, float ray_end,
    int sample_count) {
    CubeyAtmosphereOpticalDepth optical_depth = cubey_atmosphere_optical_depth_zero();
    float ray_length = max(ray_end - ray_start, 0.0);
    if (ray_length <= 0.0) {
        return optical_depth;
    }

    int clamped_sample_count = clamp(sample_count, 1, CUBEY_ATMOSPHERE_LIGHT_SAMPLE_COUNT);
    float step_length = ray_length / float(clamped_sample_count);
    for (int i = 0; i < CUBEY_ATMOSPHERE_LIGHT_SAMPLE_COUNT; ++i) {
        if (i >= clamped_sample_count) {
            break;
        }
        float t = ray_start + (float(i) + 0.5) * step_length;
        CubeyAtmosphereOpticalDepth density =
            cubey_atmosphere_sample_density(medium, origin + direction * t);
        optical_depth = cubey_atmosphere_optical_depth_add(
            optical_depth, cubey_atmosphere_optical_depth_scale(density, step_length));
    }
    return optical_depth;
}

float cubey_atmosphere_limb_visibility(vec3 sample_position, vec3 light_direction,
                                       vec3 planet_center, float ground_radius,
                                       float angular_radius, float min_softness) {
    vec3 to_planet_center = planet_center - sample_position;
    float center_distance = length(to_planet_center);
    float planet_angular_radius =
        asin(clamp(ground_radius / max(center_distance, 0.0001), 0.0, 1.0));
    float light_center_angle =
        acos(clamp(dot(light_direction, normalize(to_planet_center)), -1.0, 1.0));
    float limb_clearance = light_center_angle - planet_angular_radius;
    float softness = max(angular_radius * 4.0, min_softness);
    return smoothstep(-softness, softness, limb_clearance);
}

float cubey_atmosphere_sun_visibility(CubeyAtmosphereMedium medium, vec3 sample_position) {
    return cubey_atmosphere_limb_visibility(sample_position, medium.sun_direction,
                                            medium.planet_center, medium.bottom_radius,
                                            medium.sun_angular_radius,
                                            medium.min_twilight_softness);
}

vec3 cubey_atmosphere_sun_transmittance(CubeyAtmosphereMedium medium, vec3 position) {
    vec3 start = position + medium.sun_direction * max(medium.bottom_radius * 0.00002, 0.008);
    vec2 ground_hit = cubey_atmosphere_ray_sphere_intersection(start, medium.sun_direction,
                                                               medium.planet_center,
                                                               medium.bottom_radius);
    if (ground_hit.x > 0.0 || ground_hit.y > 0.0) {
        return vec3(0.0);
    }

    vec2 atmosphere_hit = cubey_atmosphere_ray_sphere_intersection(
        start, medium.sun_direction, medium.planet_center, medium.top_radius);
    if (atmosphere_hit.y <= 0.0) {
        return vec3(1.0);
    }
    CubeyAtmosphereOpticalDepth depth = cubey_atmosphere_integrate_optical_depth(
        medium, start, medium.sun_direction, 0.0, atmosphere_hit.y,
        CUBEY_ATMOSPHERE_LIGHT_SAMPLE_COUNT);
    return cubey_atmosphere_depth_transmittance(medium, depth);
}

float cubey_atmosphere_nearest_positive_ground_hit(CubeyAtmosphereMedium medium, vec3 origin,
                                                   vec3 direction) {
    vec2 ground_hit = cubey_atmosphere_ray_sphere_intersection(origin, direction,
                                                               medium.planet_center,
                                                               medium.bottom_radius);
    if (ground_hit.x > 0.0) {
        return ground_hit.x;
    }
    if (ground_hit.y > 0.0) {
        return ground_hit.y;
    }
    return -1.0;
}

CubeyAtmosphereRaySegment cubey_atmosphere_classify_ray(CubeyAtmosphereMedium medium, vec3 origin,
                                                        vec3 direction, float max_distance) {
    float camera_radius = length(origin - medium.planet_center);
    bool camera_inside_atmosphere = camera_radius < medium.top_radius;
    vec2 atmosphere_hit = cubey_atmosphere_ray_sphere_intersection(
        origin, direction, medium.planet_center, medium.top_radius);
    if (!camera_inside_atmosphere && atmosphere_hit.y <= 0.0) {
        return CubeyAtmosphereRaySegment(0.0, 0.0, -1.0, false, false,
                                         camera_inside_atmosphere);
    }

    float ray_start = camera_inside_atmosphere ? 0.0 : max(atmosphere_hit.x, 0.0);
    float ray_end = atmosphere_hit.y;
    if (max_distance >= 0.0) {
        ray_end = min(ray_end, max_distance);
    }

    float ground_t = cubey_atmosphere_nearest_positive_ground_hit(medium, origin, direction);
    bool hit_ground = ground_t > 0.0 && ground_t < ray_end;
    if (hit_ground) {
        ray_end = ground_t;
    }

    bool hit_atmosphere = ray_end > ray_start;
    return CubeyAtmosphereRaySegment(ray_start, ray_end, ground_t, hit_atmosphere, hit_ground,
                                     camera_inside_atmosphere);
}

CubeyAtmosphereRaySegment cubey_atmosphere_classify_sky_background_ray(
    CubeyAtmosphereMedium medium, vec3 origin, vec3 direction, float max_distance) {
    float camera_radius = length(origin - medium.planet_center);
    bool camera_inside_atmosphere = camera_radius < medium.top_radius;
    vec2 atmosphere_hit = cubey_atmosphere_ray_sphere_intersection(
        origin, direction, medium.planet_center, medium.top_radius);
    if (!camera_inside_atmosphere && atmosphere_hit.y <= 0.0) {
        return CubeyAtmosphereRaySegment(0.0, 0.0, -1.0, false, false,
                                         camera_inside_atmosphere);
    }

    float ray_start = camera_inside_atmosphere ? 0.0 : max(atmosphere_hit.x, 0.0);
    float ray_end = atmosphere_hit.y;
    if (max_distance >= 0.0) {
        ray_end = min(ray_end, max_distance);
    }

    bool hit_atmosphere = ray_end > ray_start;
    return CubeyAtmosphereRaySegment(ray_start, ray_end, -1.0, hit_atmosphere, false,
                                     camera_inside_atmosphere);
}

CubeyAtmosphereSample cubey_atmosphere_integrate_ray(CubeyAtmosphereMedium medium, vec3 origin,
                                                     vec3 direction, float ray_start,
                                                     float ray_end) {
    CubeyAtmosphereSample result;
    result.color = vec3(0.0);
    result.rayleigh = vec3(0.0);
    result.mie = vec3(0.0);
    result.transmittance = vec3(1.0);
    result.optical_depth = cubey_atmosphere_optical_depth_zero();
    result.ray_length = max(ray_end - ray_start, 0.0);
    if (result.ray_length <= 0.0) {
        return result;
    }

    float cos_theta = dot(direction, medium.sun_direction);
    float rayleigh_phase_value = cubey_atmosphere_rayleigh_phase(cos_theta);
    float mie_phase_value = cubey_atmosphere_mie_phase(cos_theta, medium.mie_anisotropy);
    float step_length = result.ray_length / float(CUBEY_ATMOSPHERE_VIEW_SAMPLE_COUNT);

    for (int i = 0; i < CUBEY_ATMOSPHERE_VIEW_SAMPLE_COUNT; ++i) {
        float t = ray_start + (float(i) + 0.5) * step_length;
        vec3 sample_position = origin + direction * t;
        CubeyAtmosphereOpticalDepth density =
            cubey_atmosphere_sample_density(medium, sample_position);
        result.optical_depth = cubey_atmosphere_optical_depth_add(
            result.optical_depth, cubey_atmosphere_optical_depth_scale(density, step_length));

        vec2 sun_atmosphere_hit = cubey_atmosphere_ray_sphere_intersection(
            sample_position, medium.sun_direction, medium.planet_center, medium.top_radius);
        float sun_ray_length = max(sun_atmosphere_hit.y, 0.0);
        float solar_visibility = cubey_atmosphere_sun_visibility(medium, sample_position);
        if (solar_visibility <= 0.0001) {
            continue;
        }

        CubeyAtmosphereOpticalDepth light_depth = cubey_atmosphere_integrate_optical_depth(
            medium, sample_position, medium.sun_direction, 0.0, sun_ray_length,
            CUBEY_ATMOSPHERE_LIGHT_SAMPLE_COUNT);
        CubeyAtmosphereOpticalDepth total_depth =
            cubey_atmosphere_optical_depth_add(result.optical_depth, light_depth);
        vec3 transmittance = cubey_atmosphere_depth_transmittance(medium, total_depth);
        vec3 rayleigh_scattering =
            density.rayleigh * medium.rayleigh_scattering * rayleigh_phase_value;
        vec3 mie_scattering = density.mie * vec3(medium.mie_scattering) * mie_phase_value;

        result.rayleigh += transmittance * rayleigh_scattering * step_length * solar_visibility;
        result.mie += transmittance * mie_scattering * step_length * solar_visibility;
    }

    result.rayleigh *= medium.sun_radiance;
    result.mie *= medium.sun_radiance;
    result.color = result.rayleigh + result.mie;
    result.transmittance = cubey_atmosphere_depth_transmittance(medium, result.optical_depth);
    return result;
}

CubeyAtmosphereSample cubey_atmosphere_integrate_view(CubeyAtmosphereMedium medium, vec3 origin,
                                                      vec3 direction, float max_distance) {
    CubeyAtmosphereRaySegment segment =
        cubey_atmosphere_classify_ray(medium, origin, direction, max_distance);
    if (!segment.hit_atmosphere) {
        CubeyAtmosphereSample empty;
        empty.color = vec3(0.0);
        empty.rayleigh = vec3(0.0);
        empty.mie = vec3(0.0);
        empty.transmittance = vec3(1.0);
        empty.optical_depth = cubey_atmosphere_optical_depth_zero();
        empty.ray_length = 0.0;
        return empty;
    }
    return cubey_atmosphere_integrate_ray(medium, origin, direction, segment.start, segment.end);
}

#endif
