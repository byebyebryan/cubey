#ifndef CUBEY_WATER_3D_SURFACE_COMMON_GLSL
#define CUBEY_WATER_3D_SURFACE_COMMON_GLSL

#include "water_3d_contract.glsl"

#define WATER3D_SURFACE_VIEW_SURFACE 0u
#define WATER3D_SURFACE_VIEW_DEPTH 7u
#define WATER3D_SURFACE_VIEW_THICKNESS 8u
#define WATER3D_SURFACE_VIEW_NORMALS 9u
#define WATER3D_SURFACE_VIEW_FOAM 10u
#define WATER3D_SURFACE_VIEW_WHITEWATER 11u
#define WATER3D_SURFACE_DEPTH_SENTINEL 1000000.0
#define WATER3D_SURFACE_MAX_FILL_RADIUS 3
#define WATER3D_SURFACE_MAX_SMOOTH_RADIUS 24

#define WATER3D_SURFACE_PARAMS                                                                 \
    layout(push_constant) uniform SurfaceRenderParams {                                        \
        mat4 view_projection;                                                                  \
        vec4 camera_position_view;                                                             \
        vec4 camera_right_tan;                                                                 \
        vec4 camera_up_aspect;                                                                 \
        vec4 camera_forward_radius;                                                            \
        vec4 particle_options;                                                                 \
        vec4 surface_options;                                                                  \
        vec4 environment_options;                                                              \
        vec4 display_transform;                                                                \
    } surface_params

bool water_surface_has_depth(float depth) {
    return depth < (WATER3D_SURFACE_DEPTH_SENTINEL * 0.5);
}

#endif
