layout(set = 0, binding = 0, std140) uniform ReferenceFrame {
    mat4 view_projection;
    vec4 camera_position_time;
    vec4 camera_right;
    vec4 camera_up;
    vec4 camera_forward;
    vec4 domain_center_extent_surface;
    vec4 resolution_options;
    vec4 diagnostic_options;
} reference_frame;

layout(set = 0, binding = 1) uniform sampler2D reference_height_atlas;
layout(set = 0, binding = 2) uniform sampler2D iChannel0;
layout(set = 0, binding = 3) uniform sampler2D iChannel1;

#define iResolution vec3(reference_frame.resolution_options.xy, 1.0)
#define iTime reference_frame.camera_position_time.w
#define iMouse vec4(0.0)
