#version 450

layout(std140, set = 0, binding = 0) uniform RigidMotionUniforms {
    mat4 current_jittered_view_projection;
    mat4 current_view_projection;
    mat4 previous_view_projection;
    vec4 extent;
} u;

layout(push_constant) uniform RigidMotionPushConstants {
    mat4 current_model;
    mat4 previous_model;
} push_constants;

layout(location = 0) in vec3 object_position;
layout(location = 0) out vec2 out_motion;

bool finite4(vec4 value) {
    return !any(isnan(value)) && !any(isinf(value));
}

void main() {
    vec4 object = vec4(object_position, 1.0);
    vec4 current_clip = u.current_view_projection * push_constants.current_model * object;
    vec4 previous_clip = u.previous_view_projection * push_constants.previous_model * object;
    out_motion = u.extent.xy + vec2(16.0);
    if (finite4(current_clip) && finite4(previous_clip) && current_clip.w > 1e-8 &&
        previous_clip.w > 1e-8 && previous_clip.z >= 0.0 && previous_clip.z <= previous_clip.w) {
        vec2 current_pixel = (current_clip.xy / current_clip.w * 0.5 + 0.5) * u.extent.xy;
        vec2 previous_pixel = (previous_clip.xy / previous_clip.w * 0.5 + 0.5) * u.extent.xy;
        out_motion = previous_pixel - current_pixel;
    }
}
