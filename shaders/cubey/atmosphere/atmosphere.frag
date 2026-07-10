#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/atmosphere.glsl"
#include "cubey/color_space.glsl"
#include "cubey/procedural/noise.glsl"

const float ATMOSPHERE_SUN_INTENSITY = 22.0;
const float ATMOSPHERE_MIN_TWILIGHT_SOFTNESS = 0.022;

layout(set = 0, binding = 0) uniform AtmosphereFrame {
    vec4 camera_right_aspect;
    vec4 camera_up_tan_half_fovy;
    vec4 camera_forward_debug_view;
    vec4 camera_position_radius;
    vec4 radii_ground;
    vec4 rayleigh;
    vec4 mie;
    vec4 ozone;
    vec4 sun_direction_radius;
    vec4 atmosphere_options;
    vec4 night_options;
    vec4 celestial_options;
    vec4 moon_direction_radius;
    vec4 moon_options;
    vec4 milky_way_options;
    vec4 render_options;
    vec4 celestial_render_options;
} atmosphere;

layout(set = 0, binding = 1) uniform samplerCube night_sky_atlas;

layout(location = 0) in vec2 frag_ndc;
layout(location = 0) out vec4 out_color;

#include "atmosphere_common.glsl"
#include "atmosphere_night_sky.glsl"
#include "atmosphere_sun.glsl"
#include "atmosphere_ground.glsl"
#include "atmosphere_debug.glsl"

void main() {
    float tan_half_fovy = atmosphere.camera_up_tan_half_fovy.w;
    vec3 ray_direction = normalize(
        atmosphere.camera_forward_debug_view.xyz +
        atmosphere.camera_right_aspect.xyz * frag_ndc.x * atmosphere.camera_right_aspect.w *
            tan_half_fovy -
        atmosphere.camera_up_tan_half_fovy.xyz * frag_ndc.y * tan_half_fovy);
    vec3 ray_origin = atmosphere_ray_origin();
    vec3 planet_center = atmosphere_planet_center();
    int debug_view = int(atmosphere.camera_forward_debug_view.w + 0.5);
    bool render_celestial_content = atmosphere.render_options.y >= 0.5;
    bool render_sun_disk = render_celestial_content && atmosphere.celestial_render_options.x >= 0.5;
    bool render_night_sky =
        render_celestial_content && atmosphere.celestial_render_options.y >= 0.5;
    bool render_star_debug = debug_view == CUBEY_ATMOSPHERE_VIEW_STARS;

    if ((debug_view == CUBEY_ATMOSPHERE_VIEW_MOON ||
         debug_view == CUBEY_ATMOSPHERE_VIEW_MOON_SURFACE)) {
        out_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    if (debug_view == CUBEY_ATMOSPHERE_VIEW_MILKY_WAY) {
        vec3 color = render_milky_way_debug();
        out_color = vec4(color, 1.0);
        return;
    }

    vec3 sun_direction = normalize(atmosphere.sun_direction_radius.xyz);
    CubeyAtmosphereMedium medium = atmosphere_medium(planet_center);
    bool ignore_ground_occlusion = atmosphere.render_options.x >= 1.5;
    vec3 atmosphere_ray_direction =
        ignore_ground_occlusion
            ? sky_background_sample_direction(ray_direction, ray_origin, planet_center)
            : ray_direction;
    CubeyAtmosphereRaySegment segment =
        ignore_ground_occlusion
            ? cubey_atmosphere_classify_sky_background_ray(
                  medium, ray_origin, atmosphere_ray_direction, -1.0)
            : cubey_atmosphere_classify_ray(medium, ray_origin, atmosphere_ray_direction, -1.0);
    float celestial_horizon_visibility =
        (ignore_ground_occlusion && segment.camera_inside_atmosphere)
            ? smoothstep(-0.015, 0.040,
                         dot(ray_direction, atmosphere_camera_up(ray_origin, planet_center)))
            : 1.0;
    if (!segment.hit_atmosphere) {
        vec3 space_night =
            render_star_debug
                ? space_procedural_star_radiance(atmosphere_ray_direction, sun_direction)
                : space_night_sky_radiance(atmosphere_ray_direction, sun_direction);
        vec3 space_color =
            (render_sun_disk && !render_star_debug
                 ? sun_disk_luminance(ray_origin, atmosphere_ray_direction, planet_center) *
                       celestial_horizon_visibility
                 : vec3(0.0)) +
            (render_night_sky ? space_night * celestial_horizon_visibility : vec3(0.0));
        out_color = vec4(space_color, 1.0);
        return;
    }

    bool hit_ground = segment.hit_ground;
    bool sky_only = atmosphere.render_options.x >= 0.5;
    bool shade_ground = hit_ground && !sky_only;

    CubeyAtmosphereSample atmosphere_sample = integrate_atmosphere(
        ray_origin, atmosphere_ray_direction, segment.start, segment.end, planet_center);
    vec3 night_sky_radiance_value = render_star_debug
        ? (segment.camera_inside_atmosphere
               ? procedural_star_radiance(atmosphere_ray_direction, sun_direction)
               : space_procedural_star_radiance(atmosphere_ray_direction, sun_direction))
        : (segment.camera_inside_atmosphere
               ? night_sky_radiance(atmosphere_ray_direction, sun_direction)
               : space_night_sky_radiance(atmosphere_ray_direction, sun_direction));
    vec3 night_sky = (hit_ground || !render_night_sky)
        ? vec3(0.0)
        : night_sky_radiance_value * atmosphere_sample.transmittance *
              celestial_horizon_visibility;
    vec3 sun_disk = (hit_ground || !render_sun_disk) ? vec3(0.0) :
        sun_disk_luminance(ray_origin, atmosphere_ray_direction, planet_center) *
            celestial_horizon_visibility;
    vec3 color = atmosphere_sample.color + sun_disk + night_sky;
    if (shade_ground) {
        color += ground_radiance(ray_origin, atmosphere_ray_direction, planet_center,
                                 segment.ground_t) *
                 atmosphere_sample.transmittance;
    }

    if (debug_view == CUBEY_ATMOSPHERE_VIEW_RAYLEIGH) {
        color = atmosphere_sample.rayleigh;
    } else if (debug_view == CUBEY_ATMOSPHERE_VIEW_MIE) {
        color = atmosphere_sample.mie;
    } else if (debug_view == CUBEY_ATMOSPHERE_VIEW_TRANSMITTANCE) {
        color = atmosphere_sample.transmittance;
    } else if (debug_view == CUBEY_ATMOSPHERE_VIEW_OPTICAL_DEPTH) {
        color = debug_optical_depth(atmosphere_sample.optical_depth);
    } else if (debug_view == CUBEY_ATMOSPHERE_VIEW_SUN_DISK) {
        color = sun_disk;
    } else if (debug_view == CUBEY_ATMOSPHERE_VIEW_AERIAL_PERSPECTIVE) {
        color = render_aerial_perspective_debug(ray_origin, atmosphere_ray_direction,
                                                planet_center);
    } else if (debug_view == CUBEY_ATMOSPHERE_VIEW_NIGHT_SKY) {
        color = night_sky;
    } else if (debug_view == CUBEY_ATMOSPHERE_VIEW_STARS) {
        color = night_sky;
    }

    out_color = vec4(color, 1.0);
}
