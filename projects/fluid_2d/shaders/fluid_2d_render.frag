#version 450

layout(push_constant) uniform RenderParams {
    vec4 grid_time;
} params;

struct Cell {
    vec4 dye;
    vec4 velocity;
};

layout(set = 0, binding = 0, std430) readonly buffer RenderField {
    Cell cells[];
} render_field;

layout(location = 0) in vec2 frag_position;
layout(location = 0) out vec4 out_color;

uint cell_index(ivec2 coord, uint width, uint height) {
    ivec2 clamped = clamp(coord, ivec2(0), ivec2(int(width) - 1, int(height) - 1));
    return (uint(clamped.y) * width) + uint(clamped.x);
}

Cell sample_field(vec2 uv, uint width, uint height) {
    vec2 position = uv * vec2(width, height) - vec2(0.5);
    ivec2 base = ivec2(floor(position));
    vec2 fraction = fract(position);

    Cell a = render_field.cells[cell_index(base, width, height)];
    Cell b = render_field.cells[cell_index(base + ivec2(1, 0), width, height)];
    Cell c = render_field.cells[cell_index(base + ivec2(0, 1), width, height)];
    Cell d = render_field.cells[cell_index(base + ivec2(1, 1), width, height)];

    Cell result;
    result.dye = mix(mix(a.dye, b.dye, fraction.x), mix(c.dye, d.dye, fraction.x), fraction.y);
    result.velocity = mix(mix(a.velocity, b.velocity, fraction.x),
                          mix(c.velocity, d.velocity, fraction.x), fraction.y);
    return result;
}

void main() {
    vec2 uv = frag_position * 0.5 + 0.5;
    uint width = uint(params.grid_time.x);
    uint height = uint(params.grid_time.y);
    Cell cell = sample_field(uv, width, height);
    float speed = clamp(length(cell.velocity.xy) * 0.45, 0.0, 1.0);
    vec3 dye = clamp(cell.dye.rgb, vec3(0.0), vec3(1.0));
    vec3 velocity_tint = vec3(0.04, 0.10, 0.16) + vec3(0.05, 0.12, 0.20) * speed;
    out_color = vec4(dye + velocity_tint, 1.0);
}
