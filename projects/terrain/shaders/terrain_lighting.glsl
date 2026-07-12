#ifndef CUBEY_TERRAIN_LIGHTING_GLSL
#define CUBEY_TERRAIN_LIGHTING_GLSL

#include "cubey/pbr.glsl"

vec3 terrain_lighting_direct(vec3 base_color, float roughness, vec3 normal,
                             vec3 view_direction, vec3 light_direction,
                             vec3 light_radiance, float direct_visibility) {
    float ndotv = max(dot(normal, view_direction), 0.0);
    float ndotl = max(dot(normal, light_direction), 0.0);
    if (ndotv <= 0.0 || ndotl <= 0.0) {
        return vec3(0.0);
    }

    vec3 half_vector = view_direction + light_direction;
    float half_length_squared = dot(half_vector, half_vector);
    if (half_length_squared <= 1e-8) {
        return cubey_pbr_lambert_diffuse(base_color) * light_radiance * ndotl *
            clamp(direct_visibility, 0.0, 1.0);
    }
    vec3 half_direction = half_vector * inversesqrt(half_length_squared);
    float ndoth = max(dot(normal, half_direction), 0.0);
    float vdoth = max(dot(view_direction, half_direction), 0.0);
    vec3 f0 = vec3(0.04);
    vec3 fresnel = cubey_pbr_fresnel_schlick(vdoth, f0);
    float distribution = cubey_pbr_distribution_ggx(ndoth, roughness);
    float visibility = cubey_pbr_visibility_smith_ggx_correlated(
        ndotv, ndotl, roughness);
    vec3 specular = distribution * visibility * fresnel;
    vec3 diffuse = cubey_pbr_lambert_diffuse(base_color) * (vec3(1.0) - fresnel);
    return (diffuse + specular) * light_radiance * ndotl *
        clamp(direct_visibility, 0.0, 1.0);
}

vec3 terrain_lighting_ambient(vec3 base_color, vec3 diffuse_irradiance) {
    return base_color * max(diffuse_irradiance, vec3(0.0));
}

#endif // CUBEY_TERRAIN_LIGHTING_GLSL
