#version 450

layout(set = 0, binding = 0) uniform sampler2D current_cloud_texture;
layout(set = 0, binding = 1) uniform sampler2D history_cloud_texture;

layout(push_constant) uniform CloudsTemporalParams {
    vec4 options;
} params;

layout(location = 0) in vec2 frag_position;
layout(location = 0) out vec4 out_color;

void main() {
    vec2 uv = frag_position * 0.5 + 0.5;
    vec4 current = texture(current_cloud_texture, uv);
    if (params.options.y > 0.5) {
        out_color = current;
        return;
    }

    vec4 history = texture(history_cloud_texture, uv);
    float current_weight = clamp(params.options.x, 0.02, 1.0);
    vec3 color = mix(history.rgb, current.rgb, current_weight);
    float transmittance = mix(history.a, current.a, current_weight);
    out_color = vec4(max(color, vec3(0.0)), clamp(transmittance, 0.0, 1.0));
}
