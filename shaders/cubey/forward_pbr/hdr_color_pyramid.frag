#version 450

layout(set = 0, binding = 0) uniform sampler2D source_texture;
layout(set = 0, binding = 1) uniform sampler2D opaque_depth;

layout(push_constant) uniform HdrColorPyramidFilterOptions {
    float copy_source;
    float bright_sample_resistance;
    float opaque_coverage;
} filter_options;

layout(location = 0) in vec2 frag_uv;
layout(location = 0) out vec4 out_color;

vec4 sample_radiance(vec2 uv) {
    return texture(source_texture, uv);
}

float luminance_weight(vec3 radiance) {
    // The first downsample keeps isolated HDR sun/cloud highlights from
    // bleeding across the rough refraction chain. Subsequent mips retain the
    // ordinary normalized spatial filter.
    float luminance = max(dot(radiance, vec3(0.2126, 0.7152, 0.0722)), 0.0);
    return 1.0 / (1.0 + luminance);
}

void main() {
    // Mip zero is a faithful full-resolution HDR copy. The spatial filter
    // only applies when constructing lower rough-refraction LODs.
    if (filter_options.copy_source > 0.0) {
        out_color = texture(source_texture, frag_uv);
        if (filter_options.opaque_coverage > 0.0) {
            // Forward depth clears to one. Fetch the matching full-resolution
            // texel without filtering depth across silhouettes.
            float depth = texelFetch(opaque_depth, ivec2(gl_FragCoord.xy), 0).r;
            float coverage = depth < 1.0 ? 1.0 : 0.0;
            out_color = vec4(out_color.rgb * coverage, coverage);
        }
        return;
    }

    vec2 texel = 1.0 / vec2(max(textureSize(source_texture, 0), ivec2(1)));
    const vec2 offsets[9] = vec2[](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(-1.0, 0.0), vec2(0.0, 1.0),
        vec2(0.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, -1.0), vec2(1.0, -1.0),
        vec2(-1.0, 1.0));
    const float spatial_weights[9] = float[](4.0, 2.0, 2.0, 2.0, 2.0, 1.0, 1.0, 1.0, 1.0);

    vec4 radiance_sum = vec4(0.0);
    float weight_sum = 0.0;
    for (int index = 0; index < 9; ++index) {
        vec4 radiance = sample_radiance(frag_uv + offsets[index] * texel);
        float weight = spatial_weights[index];
        if (filter_options.bright_sample_resistance > 0.0) {
            weight *= luminance_weight(radiance.rgb);
        }
        radiance_sum += radiance * weight;
        weight_sum += weight;
    }
    out_color = radiance_sum / max(weight_sum, 0.0001);
    if (filter_options.opaque_coverage <= 0.0)
        out_color.a = 1.0;
}
