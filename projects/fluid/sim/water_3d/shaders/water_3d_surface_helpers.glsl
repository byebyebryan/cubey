#ifndef CUBEY_WATER_3D_SURFACE_HELPERS_GLSL
#define CUBEY_WATER_3D_SURFACE_HELPERS_GLSL

uint water_surface_render_view() {
    return uint(surface_params.camera_position_view.w + 0.5);
}

float water_surface_particle_radius() {
    return surface_params.camera_forward_radius.w;
}

uint water_surface_particle_count() {
    return uint(surface_params.particle_options.x + 0.5);
}

vec3 water_surface_camera_position() {
    return surface_params.camera_position_view.xyz;
}

vec3 water_surface_camera_right() {
    return surface_params.camera_right_tan.xyz;
}

vec3 water_surface_camera_up() {
    return surface_params.camera_up_aspect.xyz;
}

vec3 water_surface_camera_forward() {
    return surface_params.camera_forward_radius.xyz;
}

vec3 water_surface_view_ray(vec2 uv) {
    vec2 screen = (uv * 2.0) - 1.0;
    float tan_half_fovy = surface_params.camera_right_tan.w;
    float aspect = surface_params.camera_up_aspect.w;
    return normalize(water_surface_camera_forward() +
                     water_surface_camera_right() * (screen.x * tan_half_fovy * aspect) +
                     water_surface_camera_up() * (screen.y * tan_half_fovy));
}

vec3 water_surface_world_position(vec2 uv, float linear_depth) {
    return water_surface_camera_position() + water_surface_view_ray(uv) * linear_depth;
}

#endif
