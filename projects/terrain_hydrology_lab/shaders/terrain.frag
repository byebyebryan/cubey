#version 450

layout(push_constant) uniform TerrainPushConstants {
    mat4 view_projection;
    vec4 light_direction_extent;
    vec4 camera_position_fog;
} pc;

layout(location = 0) in vec3 frag_world_position;
layout(location = 1) in vec3 frag_color;
layout(location = 2) in vec3 frag_normal;

layout(location = 0) out vec4 out_color;

void main() {
    vec3 normal = normalize(frag_normal);
    vec3 light_direction = normalize(pc.light_direction_extent.xyz);
    float diffuse = max(dot(normal, light_direction), 0.0);
    float sky = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
    float rim = max(dot(normal, normalize(vec3(-0.52, 0.38, -0.40))), 0.0);
    vec3 color = frag_color * (0.28 + diffuse * 0.72 + sky * 0.18 + rim * 0.08);

    float extent = max(pc.light_direction_extent.w, 1.0);
    float distance_m = distance(pc.camera_position_fog.xyz, frag_world_position);
    float distance_fog = smoothstep(extent * 0.48, extent * 1.18, distance_m);
    vec3 view_direction = normalize(pc.camera_position_fog.xyz - frag_world_position);
    float grazing = smoothstep(-0.10, 0.20, -view_direction.y);
    float fog = clamp(distance_fog * (0.22 + grazing * 0.20), 0.0, 0.38);
    color = mix(color, vec3(0.52, 0.64, 0.72), fog);
    color = pow(clamp(color * 1.08, 0.0, 1.0), vec3(1.0 / 2.2));
    out_color = vec4(color, 1.0);
}
