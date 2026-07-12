layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(OCEAN_FIELD_FORMAT, set = 0, binding = 0) readonly uniform image2D source_image;
layout(OCEAN_FIELD_FORMAT, set = 0, binding = 1) writeonly uniform image2D moment_image;

layout(push_constant) uniform SurfaceMomentFilterParams {
    // x: source is the unpacked field, y: 0 normal gradient / 1 foam coverage
    vec4 filter_options;
} params;

vec4 source_moment_sample(ivec2 coord) {
    vec4 value = imageLoad(source_image, coord);
    if (params.filter_options.x <= 0.5) {
        return value;
    }
    if (params.filter_options.y < 0.5) {
        return vec4(value.xy, dot(value.xy, value.xy), 0.0);
    }
    return vec4(value.xy, value.x * value.x, value.y * value.y);
}

void main() {
    ivec2 id = ivec2(gl_GlobalInvocationID.xy);
    ivec2 dest_dims = imageSize(moment_image);
    if (id.x >= dest_dims.x || id.y >= dest_dims.y) {
        return;
    }

    ivec2 source_dims = imageSize(source_image);
    ivec2 base = id * 2;
    ivec2 a = clamp(base + ivec2(0, 0), ivec2(0), source_dims - ivec2(1));
    ivec2 b = clamp(base + ivec2(1, 0), ivec2(0), source_dims - ivec2(1));
    ivec2 c = clamp(base + ivec2(0, 1), ivec2(0), source_dims - ivec2(1));
    ivec2 d = clamp(base + ivec2(1, 1), ivec2(0), source_dims - ivec2(1));
    vec4 moment = (source_moment_sample(a) + source_moment_sample(b) +
                   source_moment_sample(c) + source_moment_sample(d)) * 0.25;
    if (params.filter_options.y > 0.5) {
        moment = clamp(moment, vec4(0.0), vec4(1.0));
    }
    imageStore(moment_image, id, moment);
}
