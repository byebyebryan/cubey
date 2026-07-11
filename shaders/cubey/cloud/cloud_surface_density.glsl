#ifndef CUBEY_CLOUD_SURFACE_DENSITY_GLSL
#define CUBEY_CLOUD_SURFACE_DENSITY_GLSL

const vec4 CLOUD_STRATUS_GRADIENT = vec4(0.00, 0.10, 0.20, 0.30);
const vec4 CLOUD_STRATOCUMULUS_GRADIENT = vec4(0.02, 0.20, 0.48, 0.625);
const vec4 CLOUD_CUMULUS_GRADIENT = vec4(0.00, 0.1625, 0.88, 0.98);
const vec3 CLOUD_WIND_DIRECTION = normalize(vec3(0.5, 0.0, 0.1));
const float CLOUD_TOP_OFFSET = 750.0;

struct DensitySample {
    float density;
    float base_density;
    float detail_density;
    float authored_weather;
    float local_scatter;
    float local_clear;
    float local_structure;
    float local_edge_detail;
    float cloud_type;
    float weather_edge;
    float coverage_bias;
    float height_fraction;
};

float cloud_height_fraction(vec3 position) {
    return (length(position - cloud_sphere_center()) - cloud_inner_radius()) /
           max(cloud_outer_radius() - cloud_inner_radius(), 0.001);
}

float cloud_density_gradient(float height_fraction, float cloud_type) {
    float stratus = 1.0 - clamp(cloud_type * 2.0, 0.0, 1.0);
    float stratocumulus = 1.0 - abs(cloud_type - 0.5) * 2.0;
    float cumulus = clamp(cloud_type - 0.5, 0.0, 1.0) * 2.0;
    vec4 gradient = stratus * CLOUD_STRATUS_GRADIENT +
                    stratocumulus * CLOUD_STRATOCUMULUS_GRADIENT +
                    cumulus * CLOUD_CUMULUS_GRADIENT;
    return smoothstep(gradient.x, gradient.y, height_fraction) -
           smoothstep(gradient.z, gradient.w, height_fraction);
}

float cloud_shape_domain_m() {
    return max(params.density_options.y * 1000.0, 1.0);
}

float cloud_footprint_filter_strength() {
    return clamp(params.density_options.z, 0.0, 2.0);
}

float cloud_detail_filter_lod(float base_lod) {
    float strength = cloud_footprint_filter_strength();
    float extra_lod = max(base_lod - 0.35, 0.0) * 0.72 + strength * 0.22;
    return clamp(base_lod + extra_lod * strength, 0.0, 7.0);
}

vec2 cloud_terrain_ref_project_uv(vec3 position) {
    return position.xz / cloud_shape_domain_m() + vec2(0.5);
}

DensitySample cloud_sample_density_terrain_ref(vec3 position, bool expensive, float lod) {
    float height_fraction = cloud_height_fraction(position);
    if (height_fraction <= 0.0 || height_fraction >= 1.0) {
        return DensitySample(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                             0.0, height_fraction);
    }

    float crispiness = max(params.shape_options.x, 0.001);
    float curliness = max(params.shape_options.y, 0.001);
    vec3 animation = height_fraction * CLOUD_WIND_DIRECTION * CLOUD_TOP_OFFSET +
                     CLOUD_WIND_DIRECTION * params.weather.w;
    vec2 shape_uv = cloud_terrain_ref_project_uv(position);
    vec2 moving_shape_uv = cloud_terrain_ref_project_uv(position + animation);

    vec4 low_frequency =
        textureLod(base_noise_texture, vec3(shape_uv * crispiness, height_fraction), lod);
    float low_fbm = dot(low_frequency.gba, vec3(0.625, 0.25, 0.125));
    float base_cloud = cloud_remap(low_frequency.r, -(1.0 - low_fbm), 1.0, 0.0, 1.0);

    float cloud_type = 1.0;
    float density_profile = cloud_density_gradient(height_fraction, cloud_type);
    base_cloud *= density_profile / max(height_fraction, 0.0001);

    vec3 weather_sample = texture(weather_texture, moving_shape_uv).rgb;
    float authored_weather = clamp(weather_sample.r, 0.0, 1.0);
    float weather_edge = clamp(weather_sample.b, 0.0, 1.0);
    float coverage = authored_weather * clamp(params.weather.x, 0.0, 1.0);
    float base_with_coverage =
        cloud_remap(base_cloud, coverage, 1.0, 0.0, 1.0) * coverage;
    float detail_density = 0.0;

    if (expensive) {
        float detail_lod = cloud_detail_filter_lod(lod);
        vec3 detail =
            textureLod(detail_noise_texture,
                       vec3(moving_shape_uv * crispiness, height_fraction) * curliness,
                       detail_lod)
                .rgb;
        float high_fbm = dot(detail, vec3(0.625, 0.25, 0.125));
        float high_modifier =
            mix(high_fbm, 1.0 - high_fbm, clamp(height_fraction * 10.0, 0.0, 1.0));
        detail_density = high_modifier;
        base_with_coverage -= high_modifier * (1.0 - base_with_coverage);
        base_with_coverage =
            cloud_remap(base_with_coverage * 2.0, high_modifier * 0.2, 1.0, 0.0, 1.0);
    }

    float density = clamp(base_with_coverage, 0.0, 1.0);
    return DensitySample(density, clamp(base_cloud, 0.0, 1.0), detail_density,
                         authored_weather, authored_weather, 1.0, authored_weather,
                         detail_density, cloud_type, weather_edge, authored_weather,
                         height_fraction);
}

bool cloud_shell_segment(vec3 origin, vec3 direction, out float start_t, out float end_t) {
    float outer_near = 0.0;
    float outer_far = 0.0;
    if (!cloud_ray_sphere(origin, direction, cloud_outer_radius(), outer_near, outer_far)) {
        start_t = 0.0;
        end_t = 0.0;
        return false;
    }

    float inner_near = 0.0;
    float inner_far = 0.0;
    bool inner_hit =
        cloud_ray_sphere(origin, direction, cloud_inner_radius(), inner_near, inner_far);

    float camera_radius = length(origin - cloud_sphere_center());
    start_t = max(outer_near, 0.0);
    end_t = outer_far;

    if (camera_radius < cloud_inner_radius()) {
        if (!inner_hit || inner_far <= 0.0) {
            return false;
        }
        start_t = max(start_t, inner_far);
    } else if (camera_radius < cloud_outer_radius()) {
        start_t = 0.0;
        if (inner_hit && inner_near > 0.0) {
            end_t = min(end_t, inner_near);
        }
    } else if (inner_hit && inner_near > start_t) {
        end_t = min(end_t, inner_near);
    }

    float ground_near = 0.0;
    float ground_far = 0.0;
    if (cloud_ray_sphere(origin, direction, cloud_planet_radius(), ground_near, ground_far) &&
        ground_near > 0.0) {
        end_t = min(end_t, ground_near);
    }

    return end_t > start_t;
}

#endif
