#version 450
#extension GL_GOOGLE_include_directive : require

layout(push_constant) uniform OceanParams {
    mat4 view_projection;
    vec4 camera_time;
    vec4 mesh_options;
    vec4 wave_options;
    vec4 detail_options;
    vec4 shading_options;
    vec4 display_transform;
    vec4 disturbance;
    vec4 debug_options;
} ocean;

layout(location = 0) out vec3 frag_world_position;
layout(location = 1) out vec3 frag_normal;
layout(location = 2) out vec4 frag_wave;

const float PI = 3.14159265359;

struct WaveSample {
    float height;
    vec2 gradient;
    vec2 displacement;
    float crest;
};

vec2 direction_from_angle(float angle) {
    return normalize(vec2(cos(angle), sin(angle)));
}

float short_wave_lod(float wavelength, float camera_distance) {
    float fade_begin = wavelength * 24.0;
    float fade_end = wavelength * 86.0;
    return 1.0 - smoothstep(fade_begin, fade_end, camera_distance);
}

void add_wave(inout WaveSample sample_value, vec2 position, float camera_distance, float angle,
              float wavelength, float amplitude, float speed_scale, float steepness) {
    vec2 direction = direction_from_angle(angle);
    float k = (2.0 * PI) / max(wavelength, 0.001);
    float phase_speed = sqrt(9.81 * k) * ocean.wave_options.z * speed_scale;
    float phase = (k * dot(direction, position)) - (phase_speed * ocean.camera_time.w);
    float wave_lod = short_wave_lod(wavelength, camera_distance);
    float s = sin(phase);
    float c = cos(phase);
    float a = amplitude * wave_lod;
    sample_value.height += a * s;
    sample_value.gradient += direction * (a * k * c);
    sample_value.displacement += direction * (ocean.wave_options.w * steepness * a * c);
    sample_value.crest += smoothstep(ocean.detail_options.w, ocean.detail_options.w + 0.42,
                                     abs(c) * steepness * ocean.wave_options.w * wave_lod);
}

void add_disturbance(inout WaveSample sample_value, vec2 position) {
    float radius = max(ocean.disturbance.z, 0.001);
    float strength = max(ocean.disturbance.w, 0.0);
    vec2 delta = position - ocean.disturbance.xy;
    float distance_to_center = length(delta);
    float envelope = exp(-distance_to_center / radius);
    float phase = (distance_to_center * 0.42) - (ocean.camera_time.w * 5.8);
    float ripple = sin(phase) * envelope * strength;
    sample_value.height += ripple;
    if (distance_to_center > 0.001) {
        vec2 radial = delta / distance_to_center;
        sample_value.gradient += radial * cos(phase) * envelope * strength * 0.42;
    }
    sample_value.crest += smoothstep(0.04, 0.28, abs(ripple));
}

float shoreline_mask(vec2 position) {
    float influence = clamp(ocean.shading_options.z, 0.0, 1.0);
    vec2 shoal_position = (position - vec2(-520.0, -240.0)) / vec2(1.85, 0.75);
    float shoal = 1.0 - smoothstep(140.0, 640.0, length(shoal_position));
    return shoal * influence;
}

WaveSample sample_ocean(vec2 position, float camera_distance) {
    float amplitude = max(ocean.wave_options.x, 0.0);
    float wind = ocean.wave_options.y;
    float swell_scale = max(ocean.detail_options.x, 0.05);

    WaveSample sample_value;
    sample_value.height = 0.0;
    sample_value.gradient = vec2(0.0);
    sample_value.displacement = vec2(0.0);
    sample_value.crest = 0.0;

    add_wave(sample_value, position, camera_distance, wind, 420.0 * swell_scale,
             amplitude * 5.4, 0.48, 0.38);
    add_wave(sample_value, position, camera_distance, wind + 0.38, 210.0 * swell_scale,
             amplitude * 2.1, 0.72, 0.55);
    add_wave(sample_value, position, camera_distance, wind - 0.74, 92.0 * swell_scale,
             amplitude * 0.84, 1.12, 0.72);
    add_wave(sample_value, position, camera_distance, wind + 1.43, 41.0 * swell_scale,
             amplitude * 0.30, 1.65, 0.88);
    add_wave(sample_value, position, camera_distance, wind - 1.91, 17.0 * swell_scale,
             amplitude * 0.10, 2.35, 0.92);
    add_disturbance(sample_value, position);
    return sample_value;
}

vec2 triangle_corner(uint vertex_in_cell) {
    if (vertex_in_cell == 0u) {
        return vec2(0.0, 0.0);
    }
    if (vertex_in_cell == 1u) {
        return vec2(1.0, 0.0);
    }
    if (vertex_in_cell == 2u) {
        return vec2(0.0, 1.0);
    }
    if (vertex_in_cell == 3u) {
        return vec2(0.0, 1.0);
    }
    if (vertex_in_cell == 4u) {
        return vec2(1.0, 0.0);
    }
    return vec2(1.0, 1.0);
}

void main() {
    uint cells = max(uint(ocean.mesh_options.x + 0.5), 1u);
    uint vertex_in_cell = uint(gl_VertexIndex) % 6u;
    uint cell_index = uint(gl_VertexIndex) / 6u;
    uint cell_x = cell_index % cells;
    uint cell_z = cell_index / cells;

    vec2 uv = (vec2(cell_x, cell_z) + triangle_corner(vertex_in_cell)) / float(cells);
    vec2 signed_uv = (uv * 2.0) - 1.0;
    vec2 projected_uv = sign(signed_uv) * pow(abs(signed_uv), vec2(1.72));

    vec2 camera_xz = ocean.camera_time.xz;
    float snap = max(ocean.mesh_options.z, 0.001);
    vec2 snapped_center = floor(camera_xz / snap) * snap;
    vec2 position_xz = snapped_center + (projected_uv * ocean.mesh_options.y);
    float camera_distance = length(position_xz - camera_xz);

    WaveSample wave = sample_ocean(position_xz, camera_distance);
    position_xz += wave.displacement;
    float shore = shoreline_mask(position_xz);
    float depth = mix(82.0, 1.4, shore);
    float shore_foam = smoothstep(0.10, 0.72, shore) * (0.45 + 0.55 * sin(ocean.camera_time.w));

    vec3 normal = normalize(vec3(-wave.gradient.x * ocean.detail_options.y, 1.0,
                                 -wave.gradient.y * ocean.detail_options.y));
    vec3 world_position = vec3(position_xz.x, wave.height, position_xz.y);

    frag_world_position = world_position;
    frag_normal = normal;
    frag_wave = vec4(wave.height, clamp((wave.crest * 0.20) + shore_foam, 0.0, 1.0),
                     camera_distance, depth);
    gl_Position = ocean.view_projection * vec4(world_position, 1.0);
}
