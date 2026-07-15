#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) out vec4 out_value;

layout(set = 0, binding = 0) uniform sampler2D iChannel0;
layout(set = 0, binding = 1) uniform sampler2D iChannel1;

layout(push_constant) uniform SourcePushConstants {
    vec4 resolution_time;
    vec4 mouse;
} source_push;

#define iResolution source_push.resolution_time.xyz
#define iTime source_push.resolution_time.w
#define iMouse source_push.mouse

#include "mountains.glsl"

void main() {
    vec4 ignored_color;
    mainImage(ignored_color, iResolution.xy * 0.5);

    vec3 camera_target = CameraPath(0.1);
    camera_target.y = cameraPos.y - smoothstep(60.0, 300.0, cameraPos.y) * 150.0;
    const float roll = 0.15 * sin(iTime * 0.2);
    const vec3 forward = normalize(camera_target - cameraPos);
    const vec3 roll_up = vec3(sin(roll), cos(roll), 0.0);
    const vec3 right = normalize(cross(forward, roll_up));
    const vec3 up = normalize(cross(right, forward));

    const int output_index = int(gl_FragCoord.x);
    if (output_index == 0) {
        out_value = vec4(cameraPos, 1.0);
    } else if (output_index == 1) {
        out_value = vec4(right, 0.0);
    } else if (output_index == 2) {
        out_value = vec4(up, 0.0);
    } else {
        out_value = vec4(forward, 0.0);
    }
}
