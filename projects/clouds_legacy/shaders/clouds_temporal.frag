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

vec3 view_direction_from_basis(vec2 position, vec4 right_aspect, vec4 up_tan_half_fovy,
                               vec4 forward_mode) {
    vec3 right = right_aspect.xyz;
    vec3 up = up_tan_half_fovy.xyz;
    vec3 forward = forward_mode.xyz;
    float aspect = right_aspect.w;
    float tan_half_fovy = up_tan_half_fovy.w;
    return normalize(forward + right * position.x * aspect * tan_half_fovy -
                     up * position.y * tan_half_fovy);
}

bool project_world_to_previous_uv(vec3 world_position, out vec2 previous_uv,
                                  out float previous_depth) {
    vec3 to_previous = world_position - frame.previous_camera_position_radius.xyz;
    vec3 previous_right = normalize(frame.previous_camera_right_aspect.xyz);
    vec3 previous_up = normalize(frame.previous_camera_up_tan_half_fovy.xyz);
    vec3 previous_forward = normalize(frame.previous_camera_forward_mode.xyz);
    previous_depth = dot(to_previous, previous_forward);
    float aspect = max(frame.previous_camera_right_aspect.w, 0.001);
    float tan_half_fovy = max(frame.previous_camera_up_tan_half_fovy.w, 0.001);
    if (previous_depth <= 0.001) {
        previous_uv = vec2(-1.0);
        return false;
    }

    vec2 previous_position =
        vec2(dot(to_previous, previous_right) / (previous_depth * aspect * tan_half_fovy),
             -dot(to_previous, previous_up) / (previous_depth * tan_half_fovy));
    previous_uv = previous_position * 0.5 + 0.5;
    return all(greaterThanEqual(previous_uv, vec2(0.0))) &&
           all(lessThanEqual(previous_uv, vec2(1.0)));
}

vec4 neighborhood_clamped_history(vec2 uv, vec4 history) {
    vec2 texel = 1.0 / vec2(textureSize(current_cloud_texture, 0));
    vec4 min_value = vec4(1.0e6);
    vec4 max_value = vec4(-1.0e6);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 sample_uv = clamp(uv + vec2(float(x), float(y)) * texel, vec2(0.0), vec2(1.0));
            vec4 sample_value = texture(current_cloud_texture, sample_uv);
            min_value = min(min_value, sample_value);
            max_value = max(max_value, sample_value);
        }
    }

    vec4 slack = vec4(0.06, 0.06, 0.06, 0.04);
    return clamp(history, min_value - slack, max_value + slack);
}

float temporal_history_valid(vec2 previous_uv, float previous_depth, vec4 current_metadata,
                             vec4 history_metadata, bool projected) {
    if (!projected || frame.options.y > 0.5) {
        return 0.0;
    }
    if (current_metadata.a <= 0.02 || history_metadata.a <= 0.02) {
        return 0.0;
    }
    float depth_delta = abs(history_metadata.x - current_metadata.x);
    float depth_limit = max(3.0, current_metadata.x * 0.10);
    if (depth_delta > depth_limit || abs(history_metadata.x - previous_depth) > depth_limit) {
        return 0.0;
    }
    if (abs(history_metadata.y - current_metadata.y) > 0.35) {
        return 0.0;
    }
    float confidence = min(current_metadata.a, history_metadata.a);
    float edge_fade = min(min(previous_uv.x, previous_uv.y), min(1.0 - previous_uv.x,
                                                                 1.0 - previous_uv.y));
    return confidence * smoothstep(0.0, 0.035, edge_fade);
}

void main() {
    vec2 uv = frag_position * 0.5 + 0.5;
    vec4 current = texture(current_cloud_texture, uv);
    vec4 current_metadata = texture(current_cloud_metadata_texture, uv);
    if (frame.options.y > 0.5) {
        out_color = current;
        out_metadata = current_metadata;
        return;
    }

    vec3 current_direction =
        view_direction_from_basis(frag_position, frame.current_camera_right_aspect,
                                  frame.current_camera_up_tan_half_fovy,
                                  frame.current_camera_forward_mode);
    float current_distance = max(current_metadata.x, 0.0);
    vec3 current_world =
        frame.current_camera_position_radius.xyz + current_direction * current_distance;
    vec2 previous_uv;
    float previous_depth;
    bool projected = project_world_to_previous_uv(current_world, previous_uv, previous_depth);
    vec2 history_uv = projected ? previous_uv : uv;
    vec4 history = texture(history_cloud_texture, history_uv);
    vec4 history_metadata = texture(history_cloud_metadata_texture, history_uv);
    float history_valid =
        temporal_history_valid(previous_uv, previous_depth, current_metadata, history_metadata,
                               projected);
    history = neighborhood_clamped_history(uv, history);

    float current_weight = mix(1.0, clamp(frame.options.x, 0.02, 1.0), history_valid);
    vec3 color = mix(history.rgb, current.rgb, current_weight);
    float transmittance = mix(history.a, current.a, current_weight);
    out_color = vec4(max(color, vec3(0.0)), clamp(transmittance, 0.0, 1.0));
    out_metadata = mix(history_metadata, current_metadata, current_weight);
    out_metadata.w = max(out_metadata.w, current_metadata.w);
}
