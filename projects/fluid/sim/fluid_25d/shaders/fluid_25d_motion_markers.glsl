#ifndef FLUID_25D_MOTION_MARKERS_GLSL
#define FLUID_25D_MOTION_MARKERS_GLSL

const uint MARKER_TRAIL_POINTS = 6u;
struct MotionMarker {
    vec4 position_age_active;
    vec4 previous_xy_reserved;
    vec4 history[6];
};

#endif
