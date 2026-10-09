#ifndef FLUID25D_WATER_AGITATION_GLSL
#define FLUID25D_WATER_AGITATION_GLSL

// Three slope bands with normalized total variance. Subpixel slopes become
// BRDF variance instead of disappearing into a polished distant surface.
// No displacement, particles or hydraulic writes. Not measured turbulence.
vec3 fluid25d_agitation_bands(vec2 p, float clock, float footprint,
                            vec2 axis, float wavelength, float variance) {
    const vec2 patterns[3] = vec2[3](vec2(0.8,0.6),vec2(0.96,0.28),vec2(0.6,0.8));
    vec2 side = vec2(-axis.y,axis.x);
    const float scales[3] = float[3](1.0,0.47,0.22);
    const float weights[3] = float[3](1.0,0.5,0.25);
    // Integer temporal harmonics make the 1024-second wrapping continuous.
    const float harmonics[3] = float[3](256.0,373.0,509.0);
    float amplitude = sqrt(2.0*variance/1.3125);
    vec2 slope = vec2(0.0);
    float lost_variance = 0.0;
    for (int band=0;band<3;++band) {
        float lambda = wavelength*scales[band];
        float resolved = 1.0-smoothstep(lambda*0.125,lambda*0.5,footprint);
        float a = amplitude*weights[band];
        // Keep sample domains world-fixed. Rotating dot(p,axis) per fragment
        // with a spatially varying velocity creates arbitrarily high frequency
        // at bends, invalidating this footprint filter. Orient slopes instead.
        vec2 direction = axis*patterns[band].x+side*patterns[band].y;
        float phase = 6.28318530718*(dot(p,patterns[band])/lambda -
                                      clock*harmonics[band]/1024.0 + float(band)*0.37);
        // Skip sin for completely unresolved bands. Their energy is still kept.
        if (resolved>0.0)
            slope += direction*(a*resolved*sin(phase));
        lost_variance += 0.5*a*a*(1.0-resolved*resolved);
    }
    return vec3(slope,lost_variance);
}

#endif
