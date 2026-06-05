#version 450

layout(location = 0) in vec2 frag_ndc;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform PlanetCelestialFrame {
    vec4 camera_right_aspect;
    vec4 camera_up_tan_half_fovy;
    vec4 camera_forward_enabled;
    vec4 sun_direction_radius;
    vec4 sun_color_intensity;
    vec4 sun_disk_glow;
} celestial;

void main() {
    if (celestial.camera_forward_enabled.w < 0.5 || celestial.sun_color_intensity.w <= 0.0) {
        out_color = vec4(0.0);
        return;
    }

    float tan_half_fovy = celestial.camera_up_tan_half_fovy.w;
    vec3 ray_direction = normalize(
        celestial.camera_forward_enabled.xyz +
        celestial.camera_right_aspect.xyz * frag_ndc.x * celestial.camera_right_aspect.w *
            tan_half_fovy -
        celestial.camera_up_tan_half_fovy.xyz * frag_ndc.y * tan_half_fovy);
    vec3 sun_direction = normalize(celestial.sun_direction_radius.xyz);
    float sun_angle = acos(clamp(dot(ray_direction, sun_direction), -1.0, 1.0));
    float sun_radius = max(celestial.sun_direction_radius.w, 0.0001);
    float edge_width = max(fwidth(sun_angle) * 1.5, sun_radius * 0.10);
    float disk = 1.0 - smoothstep(sun_radius - edge_width, sun_radius + edge_width, sun_angle);
    float near_halo = exp(-sun_angle / max(sun_radius * 7.0, 0.0001));
    float far_halo = exp(-sun_angle / max(sun_radius * 28.0, 0.0001));
    float radiance = disk * celestial.sun_disk_glow.x +
                     near_halo * celestial.sun_disk_glow.y * celestial.sun_disk_glow.z +
                     far_halo * celestial.sun_disk_glow.y * celestial.sun_disk_glow.w;
    out_color = vec4(celestial.sun_color_intensity.rgb *
                         celestial.sun_color_intensity.w * radiance,
                     0.0);
}
