#version 330 core
layout (location = 0) in vec3 coordinate;
uniform sampler2D bed;
uniform sampler2D depth;
uniform mat4 camera;
uniform int surface;
out vec3 world_position;
out float physical_height;

vec2 field_uv(vec2 xy) {
    return (xy + vec2(0.5)) / vec2(256.0, 96.0);
}

void main() {
    vec2 uv = field_uv(coordinate.xy);
    float elevation = texture(bed, uv).r;
    float water = texture(depth, uv).r;
    if (surface == 1) elevation += water;
    if (surface == 2) elevation = mix(2.55, elevation, coordinate.z);
    if (surface == 3) elevation += water * coordinate.z;
    physical_height = elevation;
    world_position = vec3(coordinate.xy, 6.5 + (elevation - 6.5) * 1.72);
    gl_Position = camera * vec4(world_position, 1.0);
}
