struct CubeyEnvironmentLighting {
    vec4 primary_light_direction_intensity;
    vec4 primary_light_color_exposure;
    vec4 ambient_color_intensity;
    vec4 sky_color_options;
};

vec3 cubey_env_primary_light_direction(CubeyEnvironmentLighting lighting) {
    vec3 direction = lighting.primary_light_direction_intensity.xyz;
    float length_squared = dot(direction, direction);
    return length_squared > 0.000001 ? direction * inversesqrt(length_squared)
                                     : vec3(0.0, 1.0, 0.0);
}

float cubey_env_primary_light_intensity(CubeyEnvironmentLighting lighting) {
    return max(lighting.primary_light_direction_intensity.w, 0.0);
}

vec3 cubey_env_primary_light_color(CubeyEnvironmentLighting lighting) {
    return max(lighting.primary_light_color_exposure.xyz, vec3(0.0));
}

vec3 cubey_env_primary_light(CubeyEnvironmentLighting lighting) {
    return cubey_env_primary_light_color(lighting) *
           cubey_env_primary_light_intensity(lighting);
}

vec3 cubey_env_ambient_light(CubeyEnvironmentLighting lighting) {
    return max(lighting.ambient_color_intensity.xyz, vec3(0.0)) *
           max(lighting.ambient_color_intensity.w, 0.0);
}

vec3 cubey_env_sky_color(CubeyEnvironmentLighting lighting) {
    return max(lighting.sky_color_options.xyz, cubey_env_ambient_light(lighting));
}

float cubey_env_exposure(CubeyEnvironmentLighting lighting) {
    return lighting.primary_light_color_exposure.w;
}
