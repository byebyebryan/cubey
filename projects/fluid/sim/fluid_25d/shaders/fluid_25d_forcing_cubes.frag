#version 450
#extension GL_GOOGLE_include_directive : require
#include "cubey/color_space.glsl"

layout(location = 0) in vec2 face_uv;
layout(location = 1) flat in vec3 face_normal;
layout(location = 2) flat in float cube_kind;
layout(location = 0) out vec4 out_color;

void main() {
    vec3 tint = cube_kind < 0.5 ? vec3(0.16, 0.88, 0.34) : vec3(1.0, 0.56, 0.08);
    vec2 edge_distance = min(face_uv, 1.0 - face_uv);
    vec2 edge_width = max(1.2 * fwidth(face_uv), vec2(0.0001));
    float edge = 1.0 - min(smoothstep(0.0, edge_width.x, edge_distance.x),
                           smoothstep(0.0, edge_width.y, edge_distance.y));
    float diffuse = max(dot(face_normal, normalize(vec3(-0.45, 0.82, 0.35))), 0.0);
    float alpha = mix(0.18, 0.55, edge);
    vec3 color = cubey_srgb_to_linear(tint) * (0.68 + 0.32 * diffuse);
    // Premultiplied source-over; no depth writes and no hydraulic bindings.
    out_color = vec4(color * alpha, alpha);
}
