#ifndef CUBEY_OCEAN_SHADING_GLSL
#define CUBEY_OCEAN_SHADING_GLSL

float ocean_luminance(vec3 color) {
    return dot(max(color, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));
}

vec3 ocean_sun_light_direction() {
    return normalize(ocean_features.sun_light_direction_intensity.xyz);
}

float ocean_sun_light_intensity() {
    return max(ocean_features.sun_light_direction_intensity.w, 0.0);
}

vec3 ocean_sun_light_color() {
    return max(ocean_features.sun_light_color.rgb, vec3(0.0));
}

vec3 ocean_moon_light_direction() {
    return normalize(ocean_features.moon_light_direction_intensity.xyz);
}

float ocean_moon_light_intensity() {
    return max(ocean_features.moon_light_direction_intensity.w, 0.0);
}

vec3 ocean_moon_light_color() {
    return max(ocean_features.moon_light_color.rgb, vec3(0.0));
}

float ocean_surface_shape_strength() {
    return max(ocean_features.feature_options.x, 0.0);
}

float ocean_surface_foam_strength() {
    return max(ocean_features.feature_options.y, 0.0);
}

float ocean_foam_history_strength() {
    return max(ocean_features.feature_options.z, 0.0);
}

float ocean_detail_anti_repeat_strength() {
    return clamp(ocean_features.feature_options.w, 0.0, 1.0);
}

float ocean_terrain_foam_strength() {
    return max(ocean_features.feature_options2.x, 0.0);
}

float ocean_shape_fade_distance_scale() {
    return max(ocean_features.feature_options2.y, 0.001);
}

bool ocean_reference_shadow_enabled() {
    return ocean_features.fade_options.y > 0.5;
}

float ocean_reference_shadow_strength() {
    return clamp(ocean_features.fade_options.z, 0.0, 0.95);
}

float ocean_self_shadow_strength() {
    return clamp(ocean_features.self_shadow_options.x, 0.0, 1.0);
}

float ocean_self_shadow_distance() {
    return max(ocean_features.self_shadow_options.y, 0.001);
}

float ocean_self_shadow_bias() {
    return max(ocean_features.self_shadow_options.z, 0.0);
}

int ocean_self_shadow_steps() {
    return int(clamp(floor(ocean_features.self_shadow_options.w + 0.5), 1.0,
                     float(OCEAN_SELF_SHADOW_MAX_STEPS)));
}

float ocean_normal_fade_distance_scale() {
    return max(ocean_features.feature_options2.z, 0.001);
}

float ocean_foam_fade_distance_scale() {
    return max(ocean_features.feature_options2.w, 0.001);
}

bool ocean_far_field_enabled() {
    return ocean_features.far_field_options.x > 0.5;
}

float ocean_far_field_start_m() {
    return max(ocean_features.far_field_options.y, 0.0);
}

float ocean_far_field_end_m() {
    return max(ocean_features.far_field_options.z, ocean_far_field_start_m() + 0.001);
}

float ocean_cloud_shadow_strength() {
    return clamp(ocean_features.cloud_lighting_options.x, 0.0, 1.0);
}

float ocean_cloud_reflection_strength() {
    return clamp(ocean_features.cloud_lighting_options.w, 0.0, 1.0);
}

const int OCEAN_CLOUD_REFLECTION_SOURCE_CACHED = 0;
const int OCEAN_CLOUD_REFLECTION_SOURCE_PLANAR = 1;

int ocean_cloud_reflection_source() {
    return int(clamp(floor(ocean_features.cloud_environment_options.x + 0.5), 0.0,
                     float(OCEAN_CLOUD_REFLECTION_SOURCE_PLANAR)));
}

float ocean_cloud_environment_blend() {
    return clamp(ocean_features.cloud_environment_options.y, 0.0, 1.0);
}

bool ocean_cloud_environment_valid() {
    return ocean_features.cloud_environment_options.z > 0.5;
}

float ocean_cloud_environment_max_lod() {
    return max(ocean_features.cloud_environment_options.w, 0.0);
}

float ocean_far_roughness_strength() {
    return max(ocean_features.far_field_options2.x, 0.0);
}

float ocean_far_glint_strength() {
    return max(ocean_features.far_field_options2.y, 0.0);
}

float ocean_far_field_factor(float dist) {
    if (!ocean_far_field_enabled()) {
        return 0.0;
    }
    return smoothstep(ocean_far_field_start_m(), ocean_far_field_end_m(), dist);
}

float ocean_far_detail_footprint_start_m() {
    return max(ocean_features.far_detail_options.x, 0.001);
}

float ocean_far_detail_footprint_end_m() {
    return max(ocean_features.far_detail_options.y, ocean_far_detail_footprint_start_m() + 0.001);
}

float ocean_far_detail_filter(float dist, float footprint_m) {
    float footprint_factor =
        smoothstep(ocean_far_detail_footprint_start_m(), ocean_far_detail_footprint_end_m(),
                   footprint_m);
    return ocean_far_field_factor(dist) * footprint_factor;
}

float ocean_far_reflection_variation_strength() {
    return max(ocean_features.far_detail_options.z, 0.0);
}

float ocean_sun_glitter_width() {
    return clamp(ocean_features.far_detail_options.w, 0.001, 0.5);
}

float ocean_cloud_shadow_transmittance(vec3 world_position) {
    if (ocean_features.cloud_lighting_options.y < 0.5) {
        return 1.0;
    }
    vec2 uv = vec2(dot(ocean_features.cloud_shadow_world_to_uv_x,
                       vec4(world_position, 1.0)),
                   dot(ocean_features.cloud_shadow_world_to_uv_y,
                       vec4(world_position, 1.0)));
    if (any(lessThanEqual(uv, vec2(0.0))) || any(greaterThanEqual(uv, vec2(1.0)))) {
        return 1.0;
    }
    float edge_distance = min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y));
    float footprint = smoothstep(0.0, 0.025, edge_distance);
    float transmittance = clamp(texture(cloud_shadow_transmittance_texture, uv).r, 0.0, 1.0);
    return mix(1.0, transmittance, footprint);
}

float ocean_cloud_shadow_factor(float transmittance) {
    return mix(1.0, clamp(transmittance, 0.0, 1.0), ocean_cloud_shadow_strength());
}

bool ocean_terrain_fields_enabled() {
    return terrain_ocean.ranges_flags.w > 0.5;
}

float ocean_horizon_fog_strength() {
    return max(ocean.mesh_options.w, 0.0);
}

float ocean_surface_water_datum_y() {
    return ocean_features.surface_frame_options.x;
}

float ocean_surface_planet_radius_m() {
    return max(ocean_features.surface_frame_options.y, 0.001);
}

float ocean_surface_camera_altitude_m() {
    return max(ocean_features.surface_frame_options.z, 0.0);
}

float ocean_surface_horizon_distance_m() {
    return max(ocean_features.surface_frame_options.w, 0.001);
}

float ocean_surface_curvature_start_m() {
    return max(ocean_features.surface_curve_options.y, 0.0);
}

float ocean_surface_curvature_end_m() {
    return max(ocean_features.surface_curve_options.z,
               ocean_surface_curvature_start_m() + 0.001);
}

float ocean_surface_curvature_strength() {
    return clamp(ocean_features.surface_curve_options.w, 0.0, 1.0);
}

vec3 ocean_sky_radiance(vec3 direction) {
    vec3 dir = normalize(direction);
    vec3 atmosphere =
        max(textureLod(atmosphere_sky_radiance_texture, dir, 0.0).rgb, vec3(0.0));
    return atmosphere;
}

float ocean_sun_light_scale() {
    return clamp(ocean_sun_light_intensity() / 2.25, 0.0, 1.25);
}

float ocean_moon_light_scale() {
    return clamp(ocean_moon_light_intensity() / 2.25, 0.0, 0.25);
}

bool ocean_reference_shadow_axis(float origin, float direction, float min_value, float max_value,
                                 inout float t_min, inout float t_max) {
    if (abs(direction) < 0.0001) {
        return origin >= min_value && origin <= max_value;
    }
    float near_t = (min_value - origin) / direction;
    float far_t = (max_value - origin) / direction;
    if (near_t > far_t) {
        float temp = near_t;
        near_t = far_t;
        far_t = temp;
    }
    t_min = max(t_min, near_t);
    t_max = min(t_max, far_t);
    return t_min <= t_max;
}

float ocean_reference_pillar_shadow(vec3 world_position, vec3 light_dir) {
    if (!ocean_reference_shadow_enabled() || ocean_reference_shadow_strength() <= 0.0 ||
        light_dir.y <= 0.002) {
        return 1.0;
    }

    vec2 offset = world_position.xz - OCEAN_REFERENCE_PILLAR_CENTER_XZ;
    vec2 light_xz = light_dir.xz;
    float origin_u = dot(offset, OCEAN_REFERENCE_PILLAR_AXIS_U);
    float origin_v = dot(offset, OCEAN_REFERENCE_PILLAR_AXIS_V);
    float direction_u = dot(light_xz, OCEAN_REFERENCE_PILLAR_AXIS_U);
    float direction_v = dot(light_xz, OCEAN_REFERENCE_PILLAR_AXIS_V);

    float t_min = 0.04;
    float t_max = 100000.0;
    bool hit =
        ocean_reference_shadow_axis(origin_u, direction_u, -OCEAN_REFERENCE_PILLAR_HALF_WIDTH,
                                    OCEAN_REFERENCE_PILLAR_HALF_WIDTH, t_min, t_max) &&
        ocean_reference_shadow_axis(origin_v, direction_v, -OCEAN_REFERENCE_PILLAR_HALF_WIDTH,
                                    OCEAN_REFERENCE_PILLAR_HALF_WIDTH, t_min, t_max) &&
        ocean_reference_shadow_axis(world_position.y, light_dir.y, OCEAN_REFERENCE_PILLAR_MIN_Y,
                                    OCEAN_REFERENCE_PILLAR_MAX_Y, t_min, t_max);
    return hit ? 1.0 - ocean_reference_shadow_strength() : 1.0;
}

float ocean_ambient_light_scale() {
    vec3 sky_up = ocean_sky_radiance(vec3(0.0, 1.0, 0.0));
    float direct_intensity = ocean_sun_light_intensity() + ocean_moon_light_intensity();
    return clamp(ocean_luminance(sky_up) * 1.8 + direct_intensity * 0.05,
                 0.015, 1.2);
}

vec2 terrain_ocean_field_uv(vec2 position) {
    return clamp((position - terrain_ocean.uv_transform.xy) * terrain_ocean.uv_transform.zw,
                 vec2(0.0), vec2(1.0));
}

vec4 sample_terrain_ocean_fields(vec2 position) {
    return texture(terrain_ocean_fields_texture, terrain_ocean_field_uv(position));
}

vec3 terrain_depth_color(float water_depth) {
    float value = clamp(water_depth / max(terrain_ocean.ranges_flags.x, 1.0), 0.0, 1.0);
    vec3 shallow = cubey_srgb_to_linear(vec3(0.34, 0.72, 0.68));
    vec3 deep = cubey_srgb_to_linear(vec3(0.02, 0.09, 0.24));
    return mix(shallow, deep, value);
}

vec3 terrain_shore_color(float shore_sdf) {
    float value = clamp(shore_sdf / max(terrain_ocean.ranges_flags.y, 1.0), -1.0, 1.0);
    vec3 water = cubey_srgb_to_linear(vec3(0.05, 0.18, 0.45));
    vec3 beach = cubey_srgb_to_linear(vec3(0.70, 0.58, 0.38));
    vec3 land = cubey_srgb_to_linear(vec3(0.20, 0.38, 0.20));
    return value < 0.0 ? mix(beach, water, -value) : mix(beach, land, value);
}

float ocean_material_distance_factor(float dist) {
    return smoothstep(250.0, 1800.0, dist);
}

float ocean_horizon_extinction_factor(vec3 view_dir, float dist) {
    float horizon_distance = max(ocean_surface_horizon_distance_m(), ocean.mesh_options.z);
    float distance_extinction =
        smoothstep(horizon_distance * 0.30, horizon_distance * 0.95, dist);
    float low_angle = 1.0 - smoothstep(0.035, 0.36, abs(view_dir.y));
    float angle_extinction =
        low_angle * smoothstep(horizon_distance * 0.18, horizon_distance * 0.72, dist);
    return clamp((distance_extinction * 0.86 + angle_extinction * 0.34) *
                     ocean_horizon_fog_strength(),
                 0.0, 0.96);
}

OceanAerialPerspective ocean_horizon_aerial_perspective(vec3 view_dir, float dist) {
    float extinction = ocean_horizon_extinction_factor(view_dir, dist);
    vec3 horizon_dir = normalize(vec3(-view_dir.x, 0.055, -view_dir.z));
    return OceanAerialPerspective(ocean_sky_radiance(horizon_dir), 1.0 - extinction);
}

vec3 ocean_apply_horizon_aerial_perspective(vec3 water, vec3 view_dir, float dist) {
    OceanAerialPerspective perspective = ocean_horizon_aerial_perspective(view_dir, dist);
    return water * perspective.transmittance +
           perspective.inscatter * (1.0 - perspective.transmittance);
}

vec3 ocean_above_horizon_reflection_direction(vec3 direction) {
    vec3 dir = normalize(direction);
    dir.y = max(dir.y, 0.0);
    return normalize(dir + vec3(0.0, 0.0001, 0.0));
}

float ocean_dielectric_fresnel(float ndotv) {
    float one_minus_v = 1.0 - clamp(ndotv, 0.0, 1.0);
    float one_minus_v2 = one_minus_v * one_minus_v;
    float one_minus_v5 = one_minus_v2 * one_minus_v2 * one_minus_v;
    return OCEAN_REFLECTANCE + (1.0 - OCEAN_REFLECTANCE) * one_minus_v5;
}

float ocean_specular_aa_roughness(vec3 normal, float roughness) {
    vec3 normal_dx = dFdx(normal);
    vec3 normal_dy = dFdy(normal);
    float normal_variance =
        0.25 * (dot(normal_dx, normal_dx) + dot(normal_dy, normal_dy));
    float kernel_roughness2 = min(2.0 * normal_variance, 0.18);
    return sqrt(clamp(roughness * roughness + kernel_roughness2, 0.0004, 1.0));
}

float ocean_ggx_reflected_radiance(vec3 normal, vec3 view_dir, vec3 light_dir,
                                   float roughness) {
    float ndotv = max(dot(normal, view_dir), 0.0001);
    float ndotl = max(dot(normal, light_dir), 0.0);
    if (ndotl <= 0.0) {
        return 0.0;
    }
    vec3 halfway = normalize(view_dir + light_dir);
    float ndoth = max(dot(normal, halfway), 0.0);
    float vdoth = max(dot(view_dir, halfway), 0.0);
    float alpha = max(roughness * roughness, 0.025);
    float alpha2 = alpha * alpha;
    float distribution_denominator = ndoth * ndoth * (alpha2 - 1.0) + 1.0;
    float distribution = alpha2 /
                         max(3.14159265 * distribution_denominator *
                                 distribution_denominator,
                             0.0001);
    float geometry_k = (roughness + 1.0) * (roughness + 1.0) * 0.125;
    float geometry_view = ndotv / max(ndotv * (1.0 - geometry_k) + geometry_k, 0.0001);
    float geometry_light = ndotl / max(ndotl * (1.0 - geometry_k) + geometry_k, 0.0001);
    float one_minus_h = 1.0 - vdoth;
    float one_minus_h2 = one_minus_h * one_minus_h;
    float fresnel = OCEAN_REFLECTANCE +
                    (1.0 - OCEAN_REFLECTANCE) * one_minus_h2 * one_minus_h2 * one_minus_h;
    float brdf = distribution * geometry_view * geometry_light * fresnel /
                 max(4.0 * ndotv * ndotl, 0.0001);
    return brdf * ndotl;
}

vec3 ocean_environment_reflection(vec3 direction, float roughness) {
    vec3 dir = ocean_above_horizon_reflection_direction(direction);
    float lod = clamp(roughness, 0.0, 1.0) * OCEAN_ATMOSPHERE_REFLECTION_MAX_LOD;
    vec3 previous =
        textureLod(atmosphere_reflection_texture, dir,
                   lod)
            .rgb;
    vec3 current = textureLod(atmosphere_reflection_current_texture, dir, lod).rgb;
    return mix(previous, current,
               clamp(ocean_features.atmosphere_environment_options.x, 0.0, 1.0));
}

struct OceanCloudReflectionSample {
    vec3 radiance;
    float transmittance;
    float visibility;
};

OceanCloudReflectionSample ocean_planar_cloud_reflection(vec3 direction, float roughness,
                                                         vec3 surface_up) {
    if (ocean_cloud_reflection_strength() <= 0.0 ||
        ocean_features.cloud_planar_options.x < 0.5) {
        return OceanCloudReflectionSample(vec3(0.0), 1.0, 0.0);
    }

    vec3 raw_dir = normalize(direction);
    vec3 dir = ocean_above_horizon_reflection_direction(raw_dir);
    vec3 right = ocean_features.cloud_planar_right_aspect.xyz;
    vec3 up = ocean_features.cloud_planar_up_tan_half_fovy.xyz;
    vec3 forward = ocean_features.cloud_planar_forward_lod.xyz;
    float forward_distance = dot(dir, forward);
    float aspect = max(ocean_features.cloud_planar_right_aspect.w, 0.0001);
    float tan_half_fovy = max(ocean_features.cloud_planar_up_tan_half_fovy.w, 0.0001);
    if (forward_distance <= 0.0001) {
        return OceanCloudReflectionSample(vec3(0.0), 1.0, 0.0);
    }

    vec2 ndc = vec2(dot(dir, right) / (forward_distance * aspect * tan_half_fovy),
                    -dot(dir, up) / (forward_distance * tan_half_fovy));
    vec2 uv = ndc * 0.5 + 0.5;
    if (any(lessThanEqual(uv, vec2(0.0))) || any(greaterThanEqual(uv, vec2(1.0)))) {
        return OceanCloudReflectionSample(vec3(0.0), 1.0, 0.0);
    }

    float edge_distance = min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y));
    float edge_visibility = smoothstep(0.0, 0.06, edge_distance);
    float sky_facet_visibility = smoothstep(-0.02, 0.005, raw_dir.y);
    float plane_alignment = clamp(dot(normalize(surface_up), vec3(0.0, 1.0, 0.0)), 0.0, 1.0);
    float planar_visibility = smoothstep(0.70, 0.85, plane_alignment);
    float max_lod = max(ocean_features.cloud_planar_forward_lod.w, 0.0);
    float lod = min(max_lod, roughness * roughness * max_lod + 0.25);
    vec4 cloud = textureLod(cloud_planar_reflection_texture, uv, lod);
    cloud.rgb = max(cloud.rgb, vec3(0.0));
    cloud.a = clamp(cloud.a, 0.0, 1.0);
    return OceanCloudReflectionSample(
        cloud.rgb, cloud.a, edge_visibility * sky_facet_visibility * planar_visibility);
}

vec3 ocean_cached_cloud_reflection(vec3 direction, float roughness) {
    vec3 dir = ocean_above_horizon_reflection_direction(direction);
    float max_lod = ocean_cloud_environment_max_lod();
    float filtered_roughness = clamp(roughness, 0.0, 1.0);
    float lod = min(max_lod, filtered_roughness * filtered_roughness * max_lod + 0.25);
    vec3 previous = textureLod(cloud_environment_previous_texture, dir, lod).rgb;
    vec3 current = textureLod(cloud_environment_current_texture, dir, lod).rgb;
    return mix(previous, current, ocean_cloud_environment_blend());
}


#endif
