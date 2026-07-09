const float TERRAIN_REF_RECIPE_TERRAIN_ENGINE = 0.0;
const float TERRAIN_REF_RECIPE_SHADERTOY_MOUNTAIN = 1.0;
const float TERRAIN_REF_RECIPE_SHADERTOY_ALPINE = 2.0;
const float TERRAIN_REF_RECIPE_SHADERTOY_DUNES = 3.0;
const float TERRAIN_REF_RECIPE_SHADERTOY_LAKE_BASIN = 4.0;
const float TERRAIN_REF_RECIPE_SHADERTOY_BADLANDS = 5.0;
const float TERRAIN_REF_RECIPE_SHADERTOY_COAST_ISLAND = 6.0;

const float TERRAIN_REF_MATERIAL_RECIPE = 0.0;
const float TERRAIN_REF_MATERIAL_HEIGHT = 1.0;

const float SHADERTOY_ALPINE_WATER_HEIGHT_M = 260.0;
const float SHADERTOY_DUNES_WATER_HEIGHT_M = -1000.0;
const float SHADERTOY_LAKE_BASIN_WATER_HEIGHT_M = 165.0;
const float SHADERTOY_BADLANDS_WATER_HEIGHT_M = -1000.0;
const float SHADERTOY_COAST_ISLAND_WATER_HEIGHT_M = 100.0;

float shadertoy_biome_fract_positive(float value) {
    return value - floor(value);
}

float shadertoy_biome_hash(vec2 p, vec2 seed) {
    float dot_value = ((p.x + seed.x) * 127.113) + ((p.y + seed.y) * 311.731) + 41.17;
    return shadertoy_biome_fract_positive(sin(dot_value) * 43758.5453);
}

float shadertoy_biome_value_noise(vec2 p, vec2 seed) {
    vec2 i = floor(p);
    vec2 f = p - i;
    vec2 w = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = shadertoy_biome_hash(i, seed);
    float b = shadertoy_biome_hash(i + vec2(1.0, 0.0), seed);
    float c = shadertoy_biome_hash(i + vec2(0.0, 1.0), seed);
    float d = shadertoy_biome_hash(i + vec2(1.0, 1.0), seed);
    return mix(mix(a, b, w.x), mix(c, d, w.x), w.y);
}

vec2 shadertoy_biome_rotate(vec2 value, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return vec2((value.x * c) - (value.y * s), (value.x * s) + (value.y * c));
}

float shadertoy_biome_fbm(vec2 p, vec2 seed, int octaves, float gain) {
    float value = 0.0;
    float amplitude = 0.55;
    float total_amplitude = 0.0;
    for (int octave = 0; octave < 8; ++octave) {
        if (octave >= octaves) {
            break;
        }
        value += shadertoy_biome_value_noise(p,
            seed + vec2(float(octave) * 8.13, -float(octave) * 5.71)) * amplitude;
        total_amplitude += amplitude;
        p = shadertoy_biome_rotate((p * 2.03) + vec2(17.0, -11.0), 0.72);
        amplitude *= gain;
    }
    return total_amplitude > 0.0 ? value / total_amplitude : 0.0;
}

float shadertoy_biome_ridged_fbm(vec2 p, vec2 seed, int octaves) {
    float value = 0.0;
    float amplitude = 0.58;
    float total_amplitude = 0.0;
    for (int octave = 0; octave < 8; ++octave) {
        if (octave >= octaves) {
            break;
        }
        float n = shadertoy_biome_value_noise(p,
            seed + vec2(float(octave) * 3.91, float(octave) * 6.43));
        float ridge = 1.0 - abs((n * 2.0) - 1.0);
        value += pow(max(ridge, 0.0), 1.65) * amplitude;
        total_amplitude += amplitude;
        p = shadertoy_biome_rotate((p * 2.08) + vec2(9.0, 13.0), -0.63);
        amplitude *= 0.50;
    }
    return total_amplitude > 0.0 ? value / total_amplitude : 0.0;
}

float shadertoy_biome_triangle_wave(float value) {
    float f = shadertoy_biome_fract_positive(value);
    return 1.0 - abs((f * 2.0) - 1.0);
}

float shadertoy_biome_dune_profile(float value, float crest_position, float slip_width) {
    float phase = shadertoy_biome_fract_positive(value);
    float crest = clamp(crest_position, 0.32, 0.74);
    float lee_end = min(crest + clamp(slip_width, 0.06, 0.22), 0.98);
    float windward = smoothstep(0.02, crest, phase);
    float lee = 1.0 - smoothstep(crest, lee_end, phase);
    return pow(max(min(windward, lee), 0.0), 0.92);
}

float shadertoy_alpine_reference_height(vec2 world, vec2 seed) {
    vec2 p = (world * 0.00018) + (seed * vec2(0.119, 0.143)) + vec2(-3.0, 5.0);
    float macro = shadertoy_biome_fbm(p * 0.48, seed + vec2(4.0, -7.0), 5, 0.52);
    float mass = smoothstep(0.18, 0.82, macro);
    float shoulder = smoothstep(0.06, 0.58, macro);
    float warp = (shadertoy_biome_fbm(p * 1.10, seed + vec2(17.0, -23.0), 4, 0.52) -
        0.5) * 0.72;
    vec2 ridge_p = shadertoy_biome_rotate(vec2((p.x * 1.24) + warp, (p.y * 0.82) - warp),
        0.38);
    float ridges = shadertoy_biome_ridged_fbm(ridge_p, seed + vec2(-11.0, 19.0), 6);
    float crest = pow(max(ridges, 0.0), 1.32) * mass;
    float valley_source = shadertoy_biome_fbm(p * 0.92, seed + vec2(29.0, 31.0), 4, 0.52);
    float valley = pow(max(1.0 - valley_source, 0.0), 2.20) * shoulder;
    float local_detail = (shadertoy_biome_ridged_fbm(p * 4.2, seed + vec2(37.0, -41.0), 4) -
        0.42) * mass;
    float broad_height = 120.0 + (pow(mass, 1.35) * 1900.0) + (pow(shoulder, 2.0) * 520.0);
    float crest_height = crest * 2300.0;
    float valley_cut = valley * (360.0 + 520.0 * mass);
    return max(broad_height + crest_height - valley_cut + (local_detail * 170.0), 0.0);
}

float shadertoy_dunes_reference_height(vec2 world, vec2 seed) {
    vec2 p = (world * 0.00020) + (seed * vec2(0.071, 0.083));
    float wind_angle = 0.48 + (seed.x * 0.013);
    vec2 wind = vec2(cos(wind_angle), sin(wind_angle));
    vec2 cross_wind = vec2(-wind.y, wind.x);
    float along = dot(p, wind);
    float across = dot(p, cross_wind);
    float broad = shadertoy_biome_fbm(p * 0.20, seed + vec2(-13.0, 17.0), 5, 0.52);
    float dune_envelope = smoothstep(0.18, 0.70, broad);
    float bend = (shadertoy_biome_fbm(p * 0.44, seed + vec2(23.0, -29.0), 4, 0.52) -
        0.5) * 1.55;
    float phase_noise = shadertoy_biome_fbm(vec2(along * 0.50, across * 0.34),
        seed + vec2(31.0, -37.0), 4, 0.52) - 0.5;
    float crest_noise = shadertoy_biome_fbm(vec2(along * 0.34, across * 0.46),
        seed + vec2(-41.0, 43.0), 4, 0.52) - 0.5;
    float lobe_source = shadertoy_biome_fbm(vec2(along * 0.30, across * 0.46),
        seed + vec2(47.0, 53.0), 4, 0.52);
    float patch_breakup = smoothstep(0.18, 0.72, lobe_source);
    float lobe_envelope = smoothstep(0.24, 0.78,
        shadertoy_biome_fbm(vec2(along * 0.20, across * 0.30),
            seed + vec2(-83.0, 89.0), 4, 0.52));
    float primary_phase = (along * 0.64) + bend + sin((across * 0.68) + phase_noise * 1.6) * 0.22;
    float primary = shadertoy_biome_dune_profile(primary_phase, 0.60 + crest_noise * 0.14,
        0.12 + patch_breakup * 0.060);
    float secondary_phase = (along * 0.36) - (across * 0.12) - (bend * 0.32) +
        (phase_noise * 0.55);
    float secondary = shadertoy_biome_dune_profile(secondary_phase, 0.66 - crest_noise * 0.10,
        0.16);
    float low_swell = shadertoy_biome_fbm(p * 0.16, seed + vec2(59.0, -61.0), 4, 0.52) *
        84.0;
    float ripple = (shadertoy_biome_triangle_wave((across * 8.0) + (along * 0.42) +
        phase_noise * 1.3) - 0.5) * (1.0 + patch_breakup * 2.2);
    float macro_dunes = primary * dune_envelope * (0.58 + lobe_envelope * 0.62);
    float secondary_dunes = secondary * dune_envelope * patch_breakup;
    return 18.0 + (broad * 86.0) + low_swell +
        (macro_dunes * (290.0 + patch_breakup * 210.0)) +
        (secondary_dunes * (70.0 + (1.0 - broad) * 120.0)) + ripple;
}

float shadertoy_lake_basin_reference_height(vec2 world, vec2 seed) {
    vec2 p = (world * 0.00020) + (seed * vec2(0.093, 0.107));
    float macro = shadertoy_biome_fbm(p * 0.48, seed + vec2(41.0, -43.0), 5, 0.52);
    float hills = pow(max(macro, 0.0), 1.30) * 980.0;
    float warp_x = (shadertoy_biome_fbm(p * 0.92, seed + vec2(-47.0, 53.0), 4, 0.52) -
        0.5) * 1.35;
    float warp_y = (shadertoy_biome_fbm((p * 0.88) + vec2(9.0, -4.0),
        seed + vec2(59.0, -61.0), 4, 0.52) - 0.5) * 1.05;
    vec2 q = shadertoy_biome_rotate(vec2((p.x + warp_x) * 0.62, (p.y + warp_y) * 1.18),
        -0.34);
    float basin_distance = length(q);
    float basin = smoothstep(1.46, 0.20, basin_distance);
    float lowland = smoothstep(0.70, 0.18,
        shadertoy_biome_fbm(p * 0.70, seed + vec2(67.0, 71.0), 4, 0.52));
    float basin_rim = smoothstep(1.58, 1.02, basin_distance) *
        smoothstep(0.60, 1.02, basin_distance);
    float shoreline_shelf = smoothstep(0.50, 0.82, basin) *
        (1.0 - smoothstep(0.82, 1.0, basin));
    float ridge_detail = (shadertoy_biome_ridged_fbm(p * 1.85, seed + vec2(-73.0, 79.0), 4) -
        0.42) * (1.0 - basin * 0.68);
    float basin_cut = (basin * 760.0) + (lowland * basin * 260.0);
    return max(95.0 + hills + (basin_rim * 320.0) + (ridge_detail * 165.0) - basin_cut +
        (shoreline_shelf * 88.0), -55.0);
}

float shadertoy_badlands_reference_height(vec2 world, vec2 seed) {
    vec2 p = (world * 0.00022) + (seed * vec2(0.127, 0.101)) + vec2(-4.0, 2.0);
    float macro = shadertoy_biome_fbm(p * 0.28, seed + vec2(101.0, -103.0), 5, 0.52);
    float plateau = smoothstep(0.28, 0.72, macro);
    float shoulder = smoothstep(0.06, 0.62, macro);
    float warp = (shadertoy_biome_fbm(p * 0.72, seed + vec2(-107.0, 109.0), 4, 0.52) -
        0.5) * 1.25;
    vec2 wash_p = shadertoy_biome_rotate(vec2((p.x * 0.72) + warp, (p.y * 1.28) - warp),
        0.58);
    float wash_source = shadertoy_biome_ridged_fbm(wash_p, seed + vec2(113.0, -127.0), 6);
    float dry_washes = pow(max(wash_source, 0.0), 1.55) * shoulder;
    float tributary_source = shadertoy_biome_ridged_fbm(vec2((p.x * 1.42) - warp * 0.35,
        (p.y * 1.05) + warp * 0.45), seed + vec2(-131.0, 137.0), 5);
    float tributaries = pow(max(tributary_source, 0.0), 2.05) *
        smoothstep(0.18, 0.88, plateau);
    float mesa_breakup = shadertoy_biome_fbm(p * 0.92, seed + vec2(139.0, 149.0), 4, 0.52);
    float plateau_height = 90.0 + (pow(plateau, 0.88) * 1260.0) +
        (pow(shoulder, 2.15) * 360.0);
    float cut_depth = (dry_washes * (820.0 + plateau * 420.0)) + (tributaries * 320.0);
    float cliff_lift = smoothstep(0.50, 0.86, wash_source) * smoothstep(0.20, 0.82, plateau) *
        150.0;
    float strata = (shadertoy_biome_triangle_wave((plateau_height - cut_depth) * 0.012 +
        mesa_breakup * 1.15) - 0.5) * (24.0 + plateau * 34.0) *
        smoothstep(0.16, 0.82, dry_washes + tributaries);
    float rough_detail = (shadertoy_biome_ridged_fbm(p * 3.2, seed + vec2(-151.0, 157.0), 4) -
        0.42) * (24.0 + shoulder * 58.0);
    return max(plateau_height - cut_depth + cliff_lift + strata + rough_detail, 0.0);
}

float shadertoy_coast_island_reference_height(vec2 world, vec2 seed) {
    return shadertoy_lake_basin_reference_height(world, seed);
}

vec3 shadertoy_biome_reference_normal(vec2 world, vec2 seed, float vertical_scale,
    float recipe_id) {
    const float step_m = 1.0;
    float center = 0.0;
    float x0 = 0.0;
    float x1 = 0.0;
    float z0 = 0.0;
    float z1 = 0.0;
    if (abs(recipe_id - TERRAIN_REF_RECIPE_SHADERTOY_ALPINE) < 0.5) {
        center = shadertoy_alpine_reference_height(world, seed);
        x0 = shadertoy_alpine_reference_height(world - vec2(step_m, 0.0), seed);
        x1 = shadertoy_alpine_reference_height(world + vec2(step_m, 0.0), seed);
        z0 = shadertoy_alpine_reference_height(world - vec2(0.0, step_m), seed);
        z1 = shadertoy_alpine_reference_height(world + vec2(0.0, step_m), seed);
    } else if (abs(recipe_id - TERRAIN_REF_RECIPE_SHADERTOY_DUNES) < 0.5) {
        center = shadertoy_dunes_reference_height(world, seed);
        x0 = shadertoy_dunes_reference_height(world - vec2(step_m, 0.0), seed);
        x1 = shadertoy_dunes_reference_height(world + vec2(step_m, 0.0), seed);
        z0 = shadertoy_dunes_reference_height(world - vec2(0.0, step_m), seed);
        z1 = shadertoy_dunes_reference_height(world + vec2(0.0, step_m), seed);
    } else if (abs(recipe_id - TERRAIN_REF_RECIPE_SHADERTOY_BADLANDS) < 0.5) {
        center = shadertoy_badlands_reference_height(world, seed);
        x0 = shadertoy_badlands_reference_height(world - vec2(step_m, 0.0), seed);
        x1 = shadertoy_badlands_reference_height(world + vec2(step_m, 0.0), seed);
        z0 = shadertoy_badlands_reference_height(world - vec2(0.0, step_m), seed);
        z1 = shadertoy_badlands_reference_height(world + vec2(0.0, step_m), seed);
    } else if (abs(recipe_id - TERRAIN_REF_RECIPE_SHADERTOY_COAST_ISLAND) < 0.5) {
        center = shadertoy_coast_island_reference_height(world, seed);
        x0 = shadertoy_coast_island_reference_height(world - vec2(step_m, 0.0), seed);
        x1 = shadertoy_coast_island_reference_height(world + vec2(step_m, 0.0), seed);
        z0 = shadertoy_coast_island_reference_height(world - vec2(0.0, step_m), seed);
        z1 = shadertoy_coast_island_reference_height(world + vec2(0.0, step_m), seed);
    } else {
        center = shadertoy_lake_basin_reference_height(world, seed);
        x0 = shadertoy_lake_basin_reference_height(world - vec2(step_m, 0.0), seed);
        x1 = shadertoy_lake_basin_reference_height(world + vec2(step_m, 0.0), seed);
        z0 = shadertoy_lake_basin_reference_height(world - vec2(0.0, step_m), seed);
        z1 = shadertoy_lake_basin_reference_height(world + vec2(0.0, step_m), seed);
    }
    float dhdx = (x1 - x0) * vertical_scale / (2.0 * step_m);
    float dhdz = (z1 - z0) * vertical_scale / (2.0 * step_m);
    return normalize(vec3(-dhdx, 1.0 + center * 0.0, -dhdz));
}
