#version 450

layout(set = 0, binding = 0) uniform PlanetOrbitUniforms {
    mat4 view_projection;
    vec4 camera_position;
    vec4 sun_direction_intensity;
    vec4 surface_options;
} frame;

layout(set = 0, binding = 1) uniform samplerCube surface_fields;

layout(location = 0) in vec3 frag_position;
layout(location = 1) in vec3 frag_normal;

layout(location = 0) out vec4 out_color;

float saturate(float value) {
    return clamp(value, 0.0, 1.0);
}

float smooth_threshold(float edge0, float edge1, float value) {
    float t = saturate((value - edge0) / (edge1 - edge0));
    return t * t * (3.0 - 2.0 * t);
}

float cloud_pattern(vec3 direction, float roughness) {
    vec3 p = direction * 9.0;
    float broad = sin(p.x * 0.91 + sin(p.z * 0.67)) * 0.5 + 0.5;
    float cells = sin(p.y * 1.31 + p.x * 0.43 + p.z * 0.71) * 0.5 + 0.5;
    return smooth_threshold(0.64, 0.90, broad * 0.64 + cells * 0.26 + roughness * 0.10);
}

void main() {
    vec3 normal = normalize(frag_normal);
    vec4 fields = texture(surface_fields, normal);
    float elevation = fields.r;
    float land = fields.g;
    float ice = fields.b;
    float roughness = fields.a;

    int debug_view = int(frame.surface_options.y + 0.5);
    if (debug_view == 1) {
        out_color = vec4(vec3(land), 1.0);
        return;
    }
    if (debug_view == 2) {
        out_color = vec4(vec3(elevation), 1.0);
        return;
    }
    if (debug_view == 3) {
        out_color = vec4(vec3(ice), 1.0);
        return;
    }
    if (debug_view == 4) {
        out_color = vec4(vec3(roughness), 1.0);
        return;
    }

    vec3 ocean = mix(vec3(0.008, 0.065, 0.16), vec3(0.025, 0.24, 0.40), elevation);
    vec3 lowland = mix(vec3(0.07, 0.23, 0.055), vec3(0.39, 0.31, 0.10), elevation);
    vec3 highland = mix(lowland, vec3(0.47, 0.36, 0.28), smooth_threshold(0.57, 0.82, elevation));
    vec3 base_color = mix(ocean, highland, land);
    base_color = mix(base_color, vec3(0.82, 0.86, 0.88), ice);
    if (debug_view == 5) {
        out_color = vec4(base_color, 1.0);
        return;
    }

    vec3 light_direction = normalize(frame.sun_direction_intensity.xyz);
    vec3 view_direction = normalize(frame.camera_position.xyz - frag_position);
    float ndotl = max(dot(normal, light_direction), 0.0);
    float terminator = smooth_threshold(-0.12, 0.18, dot(normal, light_direction));
    float ocean_mask = 1.0 - land;
    vec3 half_vector = normalize(light_direction + view_direction);
    float glint = pow(max(dot(normal, half_vector), 0.0), mix(24.0, 180.0, 1.0 - roughness));
    vec3 lit = base_color * (0.16 + 0.84 * ndotl);
    lit += ocean_mask * glint * ndotl * vec3(0.08, 0.13, 0.18);

    float cloud_coverage = frame.surface_options.x;
    if (cloud_coverage > 0.0) {
        float cloud = cloud_pattern(normal, roughness) * cloud_coverage;
        float cloud_lighting = 0.15 + 0.85 * ndotl;
        lit = mix(lit, vec3(0.88, 0.90, 0.92) * cloud_lighting, cloud * 0.18);
    }

    vec3 night = mix(base_color * vec3(0.012, 0.018, 0.035), vec3(0.003, 0.005, 0.010), ocean_mask);
    vec3 color = mix(night, lit, terminator);
    float atmosphere_rim = pow(1.0 - max(dot(normal, view_direction), 0.0), 4.2);
    color += vec3(0.06, 0.20, 0.42) * atmosphere_rim * (0.16 + 0.84 * terminator);
    out_color = vec4(clamp(color * 1.25, vec3(0.0), vec3(1.0)), 1.0);
}
