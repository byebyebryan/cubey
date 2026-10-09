// Normal-only visual detail; never displaces geometry or changes hydraulic data.
// Scale handoff follows Cubey Ocean's resolved-normal / unresolved-roughness
// policy and Bruneton et al., Real-time Realistic Ocean Lighting (2010).
#ifndef CUBEY_FLUID25D_WATER_DETAIL_GLSL
#define CUBEY_FLUID25D_WATER_DETAIL_GLSL

float fluid25d_wet_derivative(float center, vec2 neighbors, bvec2 wet, float spacing) {
    if (all(wet)) return (neighbors.y-neighbors.x)/(2.0*spacing);
    if (wet.x) return (center-neighbors.x)/spacing;
    if (wet.y) return (neighbors.y-center)/spacing;
    return 0.0;
}
vec3 fluid25d_wet_surface_normal(float center, vec4 neighbors, bvec4 wet,
                                float spacing, float height_scale) {
    return normalize(vec3(-fluid25d_wet_derivative(center,neighbors.xy,wet.xy,spacing)*height_scale,
                          1.0,
                          -fluid25d_wet_derivative(center,neighbors.zw,wet.zw,spacing)*height_scale));
}

// Three nonparallel bands, bounded analytic slopes. The temporal harmonics
// repeat exactly over the existing 1024 s decorative-clock wrap. Not bulk flow.
vec3 fluid25d_ripple_detail(vec2 p, float clock, float footprint,
                           float wavelength, float strength, vec3 phase_offset) {
    const vec2 directions[3] = vec2[3](vec2(0.8,0.6),vec2(0.96,0.28),vec2(0.6,0.8));
    const vec3 scale = vec3(1.0,0.47,0.22);
    const vec3 amplitude = vec3(1.0,0.50,0.25);
    const vec3 harmonics = vec3(128.0,173.0,211.0);
    vec2 slope = vec2(0.0);
    float lost_variance = 0.0;
    for (int band=0;band<3;++band) {
        float length_m = wavelength*scale[band];
        float resolved = 1.0-smoothstep(length_m*0.125,length_m*0.5,footprint);
        float a = strength*amplitude[band];
        float phase = 6.28318530718*(dot(p,directions[band])/length_m-
                                    clock*harmonics[band]/1024.0+phase_offset[band]);
        slope += directions[band]*a*resolved*cos(phase);
        lost_variance += 0.5*a*a*(1.0-resolved*resolved);
    }
    return vec3(slope,lost_variance);
}
#endif
