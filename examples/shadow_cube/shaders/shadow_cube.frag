#version 450

layout(set = 0, binding = 0) uniform sampler2D shadow_map;

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec4 frag_shadow_position;
layout(location = 0) out vec4 out_color;

const vec3 kLightDirection = normalize(vec3(0.45, 0.82, 0.35));
const vec3 kAmbientRadiance = vec3(0.045);
const vec3 kKeyLightRadiance = vec3(0.72, 0.66, 0.54);

float shadow_visibility(vec4 shadow_position, vec3 normal) {
    vec3 shadow_ndc = shadow_position.xyz / shadow_position.w;
    vec2 uv = shadow_ndc.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || shadow_ndc.z < 0.0 ||
        shadow_ndc.z > 1.0) {
        return 1.0;
    }

    float closest_depth = texture(shadow_map, uv).r;
    float normal_light = max(dot(normal, kLightDirection), 0.0);
    float bias = max(0.0015 * (1.0 - normal_light), 0.0007);
    return shadow_ndc.z - bias > closest_depth ? 0.18 : 1.0;
}

void main() {
    vec3 normal = normalize(frag_normal);
    float diffuse = max(dot(normal, kLightDirection), 0.0);
    float visibility = shadow_visibility(frag_shadow_position, normal);
    vec3 lit = frag_color * (kAmbientRadiance + kKeyLightRadiance * diffuse * visibility);
    out_color = vec4(lit, 1.0);
}
