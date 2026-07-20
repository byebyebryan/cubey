#version 450
#extension GL_GOOGLE_include_directive : require

#include "reference_frame.glsl"

layout(location = 0) out vec4 out_color;

float selectedHeight(vec2 uv) {
    const vec4 fields = texture(reference_height_atlas, uv);
    return reference_frame.domain_center_extent_surface.w > 0.5 ? fields.g : fields.r;
}

float filteredHeight(vec2 uv, float radius_world) {
    const float radius_uv = radius_world / reference_frame.domain_center_extent_surface.z;
    const vec2 diagonal = vec2(0.70710678118);
    const vec2 directions[8] = vec2[](
        vec2(1.0, 0.0), diagonal, vec2(0.0, 1.0), vec2(-diagonal.x, diagonal.y),
        vec2(-1.0, 0.0), -diagonal, vec2(0.0, -1.0), vec2(diagonal.x, -diagonal.y));
    float sum = selectedHeight(uv) * 4.0;
    float weight = 4.0;
    for (int index = 0; index < 8; ++index) {
        sum += selectedHeight(uv + directions[index] * radius_uv * 0.5) * 2.0;
        sum += selectedHeight(uv + directions[index] * radius_uv);
        weight += 3.0;
    }
    return sum / weight;
}

void main() {
    const vec2 uv = gl_FragCoord.xy / reference_frame.resolution_options.xy;
    const float diagnostic_mode = reference_frame.diagnostic_options.x;
    vec3 color;
    if (diagnostic_mode < 1.5) {
        const float height = selectedHeight(uv);
        const float normalized_height = clamp(
            (height - reference_frame.diagnostic_options.y) /
                (reference_frame.diagnostic_options.z - reference_frame.diagnostic_options.y),
            0.0, 1.0);
        color = mix(vec3(0.025, 0.04, 0.065), vec3(0.96, 0.94, 0.90), normalized_height);
    } else if (diagnostic_mode < 2.5) {
        const vec2 texel = 1.0 / vec2(textureSize(reference_height_atlas, 0));
        const float world_step = reference_frame.domain_center_extent_surface.z * texel.x;
        const float dx =
            (selectedHeight(uv + vec2(texel.x, 0.0)) -
             selectedHeight(uv - vec2(texel.x, 0.0))) /
            (2.0 * world_step);
        const float dz =
            (selectedHeight(uv + vec2(0.0, texel.y)) -
             selectedHeight(uv - vec2(0.0, texel.y))) /
            (2.0 * world_step);
        const float normalized_slope = clamp(
            length(vec2(dx, dz)) / reference_frame.diagnostic_options.w, 0.0, 1.0);
        color = mix(vec3(0.015, 0.025, 0.04), vec3(1.0, 0.45, 0.08), normalized_slope);
    } else {
        const float height = selectedHeight(uv);
        const float envelope = filteredHeight(uv, 32.0);
        const float wider_envelope = filteredHeight(uv, 96.0);
        if (diagnostic_mode < 3.5) {
            const float normalized_height = clamp(
                (envelope - reference_frame.diagnostic_options.y) /
                    (reference_frame.diagnostic_options.z -
                     reference_frame.diagnostic_options.y),
                0.0, 1.0);
            color = mix(vec3(0.025, 0.04, 0.065), vec3(0.96, 0.94, 0.90),
                        normalized_height);
        } else if (diagnostic_mode < 4.5) {
            const float residual = clamp((height - envelope) / 55.0, -1.0, 1.0);
            color = residual < 0.0
                ? mix(vec3(0.92), vec3(0.08, 0.30, 0.65), -residual)
                : mix(vec3(0.92), vec3(0.92, 0.30, 0.06), residual);
        } else {
            const float positive_uplift = max(envelope - wider_envelope, 0.0);
            const float normalized_uplift = clamp(positive_uplift / 55.0, 0.0, 1.0);
            color = mix(vec3(0.015, 0.025, 0.04), vec3(1.0, 0.62, 0.12),
                        normalized_uplift);
        }
    }
    out_color = vec4(color, 1.0);
}
