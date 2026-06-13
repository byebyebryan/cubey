#version 450

layout(set = 0, binding = 0) uniform sampler2D current_cloud_texture;
layout(set = 0, binding = 1) uniform sampler2D current_cloud_metadata_texture;
layout(set = 0, binding = 2) uniform sampler2D history_cloud_texture;
layout(set = 0, binding = 3) uniform sampler2D history_cloud_metadata_texture;

layout(std140, set = 0, binding = 4) uniform CloudsTemporalFrame {
    vec4 current_camera_right_aspect;
    vec4 current_camera_up_tan_half_fovy;
    vec4 current_camera_forward_mode;
    vec4 current_camera_position_radius;
    vec4 previous_camera_right_aspect;
    vec4 previous_camera_up_tan_half_fovy;
    vec4 previous_camera_forward_mode;
    vec4 previous_camera_position_radius;
    vec4 current_weather;
    vec4 previous_weather;
    vec4 options;
} frame;

layout(location = 0) in vec2 frag_position;
layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_metadata;

void main() {
    vec2 uv = frag_position * 0.5 + 0.5;
    vec4 current = texture(current_cloud_texture, uv);
    vec4 current_metadata = texture(current_cloud_metadata_texture, uv);
    if (frame.options.y > 0.5) {
        out_color = current;
        out_metadata = current_metadata;
        return;
    }

    vec4 history = texture(history_cloud_texture, uv);
    vec4 history_metadata = texture(history_cloud_metadata_texture, uv);
    float current_weight = clamp(frame.options.x, 0.02, 1.0);
    vec3 color = mix(history.rgb, current.rgb, current_weight);
    float transmittance = mix(history.a, current.a, current_weight);
    out_color = vec4(max(color, vec3(0.0)), clamp(transmittance, 0.0, 1.0));
    out_metadata = mix(history_metadata, current_metadata, current_weight);
}
