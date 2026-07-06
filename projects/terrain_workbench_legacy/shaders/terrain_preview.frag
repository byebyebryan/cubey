#version 450

layout(push_constant) uniform TerrainPreviewPushConstants {
    mat4 view_projection;
    vec4 light_direction_extent;
} pc;

layout(location = 0) in vec3 frag_world_position;
layout(location = 1) in vec3 frag_color;
layout(location = 2) in vec3 frag_normal;

layout(location = 0) out vec4 out_color;

void main() {
    vec3 normal = normalize(frag_normal);
    vec3 light_direction = normalize(pc.light_direction_extent.xyz);
    float diffuse = max(dot(normal, light_direction), 0.0);
    float sky = clamp(normal.y, 0.0, 1.0);
    float rim = max(dot(normal, normalize(vec3(-0.55, 0.35, -0.38))), 0.0);
    float lighting = 0.36 + diffuse * 0.58 + sky * 0.16 + rim * 0.10;
    vec3 color = frag_color * lighting;

    float extent = max(pc.light_direction_extent.w, 1.0);
    float fog = smoothstep(extent * 0.42, extent * 1.08, length(frag_world_position.xz));
    color = mix(color, vec3(0.57, 0.65, 0.68), fog * 0.18);
    color = pow(clamp(color * 1.18, 0.0, 1.0), vec3(1.0 / 2.2));
    out_color = vec4(color, 1.0);
}
