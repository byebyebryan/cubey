#version 450

layout(push_constant) uniform RenderParams {
    vec4 camera_position_steps;
    vec4 ray_right_tan;
    vec4 ray_up_aspect;
    vec4 ray_forward_debug;
    vec4 render_options;
} params;

layout(set = 0, binding = 0) uniform sampler3D density_volume;
layout(set = 0, binding = 1) uniform sampler3D velocity_volume;

layout(location = 0) in vec2 frag_position;
layout(location = 1) in vec2 frag_uv;
layout(location = 0) out vec4 out_color;

bool ray_box_intersection(vec3 origin, vec3 direction, out float near_t, out float far_t) {
    vec3 inv_direction = 1.0 / max(abs(direction), vec3(0.00001)) * sign(direction);
    vec3 t0 = (vec3(0.0) - origin) * inv_direction;
    vec3 t1 = (vec3(1.0) - origin) * inv_direction;
    vec3 t_min = min(t0, t1);
    vec3 t_max = max(t0, t1);
    near_t = max(max(t_min.x, t_min.y), t_min.z);
    far_t = min(min(t_max.x, t_max.y), t_max.z);
    return far_t > max(near_t, 0.0);
}

vec4 density_at(vec3 uv) {
    return texture(density_volume, clamp(uv, vec3(0.0), vec3(1.0)));
}

vec3 velocity_debug_color(vec3 velocity) {
    float speed = clamp(length(velocity) * 2.2, 0.0, 1.0);
    vec3 direction = normalize(velocity + vec3(0.00001)) * 0.5 + 0.5;
    return mix(vec3(speed), direction, 0.75);
}

void main() {
    int debug_view = int(params.ray_forward_debug.w + 0.5);
    if (debug_view == 1) {
        vec4 density = density_at(vec3(frag_uv, 0.5));
        out_color = vec4(clamp(density.rgb + vec3(density.a * 0.15), vec3(0.0), vec3(1.0)), 1.0);
        return;
    }
    if (debug_view == 2) {
        vec3 velocity = texture(velocity_volume, vec3(frag_uv, 0.5)).xyz;
        out_color = vec4(velocity_debug_color(velocity), 1.0);
        return;
    }

    vec3 origin = params.camera_position_steps.xyz;
    vec3 right = params.ray_right_tan.xyz;
    vec3 up = params.ray_up_aspect.xyz;
    vec3 forward = params.ray_forward_debug.xyz;
    float tan_half_fovy = params.ray_right_tan.w;
    float aspect = params.ray_up_aspect.w;
    vec3 direction = normalize(forward + right * frag_position.x * aspect * tan_half_fovy +
                               up * frag_position.y * tan_half_fovy);

    float near_t = 0.0;
    float far_t = 0.0;
    if (!ray_box_intersection(origin, direction, near_t, far_t)) {
        out_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    near_t = max(near_t, 0.0);
    int steps = max(int(params.camera_position_steps.w + 0.5), 1);
    float path_length = far_t - near_t;
    float step_length = path_length / float(steps);
    vec3 accumulated = vec3(0.0);
    float transmittance = 1.0;
    for (int i = 0; i < steps; ++i) {
        float t = near_t + (float(i) + 0.5) * step_length;
        vec3 position = origin + direction * t;
        vec4 density = density_at(position);
        float alpha = 1.0 - exp(-density.a * params.render_options.x * step_length);
        vec3 emission = density.rgb * params.render_options.y;
        accumulated += transmittance * emission * alpha;
        transmittance *= 1.0 - clamp(alpha, 0.0, 0.96);
        if (transmittance < 0.01) {
            break;
        }
    }

    vec3 background = vec3(0.006, 0.008, 0.012);
    vec3 color = accumulated + background * transmittance;
    out_color = vec4(clamp(color, vec3(0.0), vec3(1.0)), 1.0);
}
