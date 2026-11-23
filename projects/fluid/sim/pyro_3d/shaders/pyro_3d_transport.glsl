vec4 load_density(ivec3 coord) {
    ivec3 max_coord = imageSize(material_to_sample) - ivec3(1);
    return imageLoad(material_to_sample, clamp(coord, ivec3(0), max_coord));
}

vec4 sample_density(vec3 uv) {
    vec3 size = vec3(imageSize(material_to_sample));
    vec3 position = clamp(uv, vec3(0.0), vec3(1.0)) * size - vec3(0.5);
    ivec3 base = ivec3(floor(position));
    vec3 fraction = fract(position);
    vec4 c000 = load_density(base);
    vec4 c100 = load_density(base + ivec3(1, 0, 0));
    vec4 c010 = load_density(base + ivec3(0, 1, 0));
    vec4 c110 = load_density(base + ivec3(1, 1, 0));
    vec4 c001 = load_density(base + ivec3(0, 0, 1));
    vec4 c101 = load_density(base + ivec3(1, 0, 1));
    vec4 c011 = load_density(base + ivec3(0, 1, 1));
    vec4 c111 = load_density(base + ivec3(1, 1, 1));
    vec4 z0 = mix(mix(c000, c100, fraction.x), mix(c010, c110, fraction.x), fraction.y);
    vec4 z1 = mix(mix(c001, c101, fraction.x), mix(c011, c111, fraction.x), fraction.y);
    return mix(z0, z1, fraction.z);
}

bool inside_obstacle(vec3 uv) {
    float radius = max(params.obstacle_options.y, 0.0);
    if (radius <= 0.0) {
        return false;
    }
    vec3 center = vec3(0.5, params.obstacle_options.x, 0.5);
    return length(uv - center) <= radius;
}
