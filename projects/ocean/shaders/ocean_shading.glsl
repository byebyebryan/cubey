#ifndef CUBEY_OCEAN_SHADING_GLSL
#define CUBEY_OCEAN_SHADING_GLSL

float ocean_luminance(vec3 color) {
    return dot(max(color, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));
}

vec3 ocean_primary_light_direction() {
    return normalize(ocean.sun_direction.xyz);
}

float ocean_primary_light_intensity() {
    float dynamic_intensity = max(ocean.sun_direction.w, 0.0);
    float light_strength =
        clamp(ocean_features.material_options.x, 0.0, 1.0) *
        clamp(ocean_features.material_options.w, 0.0, 1.0);
    return mix(1.0, dynamic_intensity, light_strength);
}

vec3 ocean_primary_light_color() {
    float daylight = smoothstep(0.18, 0.72, ocean_primary_light_intensity());
    vec3 moon = cubey_srgb_to_linear(vec3(0.58, 0.62, 0.74));
    vec3 sun = cubey_srgb_to_linear(vec3(1.0, 0.86, 0.58));
    return mix(moon, sun, daylight);
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

float ocean_atmosphere_material_strength() {
    return clamp(ocean_features.material_options.x, 0.0, 1.0);
}

float ocean_atmosphere_sky_strength() {
    return ocean_atmosphere_material_strength() * clamp(ocean_features.material_options.y, 0.0, 1.0);
}

float ocean_atmosphere_reflection_strength() {
    return ocean_atmosphere_material_strength() * clamp(ocean_features.material_options.z, 0.0, 1.0);
}

float ocean_atmosphere_light_strength() {
    return ocean_atmosphere_material_strength() * clamp(ocean_features.material_options.w, 0.0, 1.0);
}

float ocean_foam_lighting_strength() {
    return clamp(ocean_features.fade_options.x, 0.0, 1.0);
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
    return clamp(ocean_features.far_field_options.w, 0.0, 1.0);
}

float ocean_far_roughness_strength() {
    return max(ocean_features.far_field_options2.x, 0.0);
}

float ocean_far_glint_strength() {
    return max(ocean_features.far_field_options2.y, 0.0);
}

float ocean_cloud_shadow_scale_m() {
    return max(ocean_features.far_field_options2.z, 1.0);
}

float ocean_cloud_shadow_speed_mps() {
    return ocean_features.far_field_options2.w;
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

float ocean_cloud_shadow_factor(vec2 position, vec3 light_dir) {
    float strength = ocean_cloud_shadow_strength();
    if (strength <= 0.0 || light_dir.y <= 0.0) {
        return 1.0;
    }

    float scale = ocean_cloud_shadow_scale_m();
    float time = ocean.camera_time.w;
    vec2 wind = normalize(vec2(0.94, 0.34));
    vec2 uv = (position + wind * time * ocean_cloud_shadow_speed_mps()) / scale;
    float broad = cubey_proc_value_noise_pcg_2d(uv + vec2(17.0, -9.0));
    vec2 rotated = vec2(position.x * 0.9171208 - position.y * 0.3986093,
                        position.x * 0.3986093 + position.y * 0.9171208);
    float mid = cubey_proc_value_noise_pcg_2d(rotated / (scale * 0.42) + vec2(-3.0, 11.0) -
                                              wind.yx * time * 0.009);
    float cover = smoothstep(0.40, 0.74, broad * 0.72 + mid * 0.28);
    float sun_visibility = smoothstep(0.02, 0.36, light_dir.y);
    return clamp(1.0 - cover * strength * sun_visibility, 0.18, 1.0);
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
    vec3 static_horizon = cubey_srgb_to_linear(vec3(0.38, 0.52, 0.70));
    vec3 static_zenith = cubey_srgb_to_linear(vec3(0.08, 0.18, 0.32));
    vec3 static_sky = mix(static_horizon, static_zenith, smoothstep(0.0, 0.82, dir.y));
    return mix(static_sky, atmosphere, ocean_atmosphere_sky_strength());
}

float ocean_direct_light_scale() {
    return clamp(ocean_primary_light_intensity() / 2.25, 0.0, 1.25);
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
    return clamp(ocean_luminance(sky_up) * 1.8 + ocean_primary_light_intensity() * 0.05,
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

vec3 ocean_environment_reflection(vec3 direction, float roughness) {
    vec3 dir = normalize(direction);
    vec3 reflection =
        textureLod(atmosphere_reflection_texture, dir,
                   clamp(roughness, 0.0, 1.0) * OCEAN_ATMOSPHERE_REFLECTION_MAX_LOD)
            .rgb;
    return mix(ocean_sky_radiance(dir), reflection, ocean_atmosphere_reflection_strength());
}


#endif
