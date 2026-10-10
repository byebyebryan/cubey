// Opt-in artist activity/flow phases, not measured turbulence or transported foam.
#ifndef CUBEY_FLUID25D_WATER_RAPIDS_GLSL
#define CUBEY_FLUID25D_WATER_RAPIDS_GLSL

#include "cubey/procedural/noise.glsl"

float fluid25d_rapid_activity(vec3 normal, vec2 velocity, float depth,
                            float height_scale, float wet_threshold) {
    float speed = length(velocity);
    if (speed<=0.00001 || normal.y<=0.0) return 0.0;
    // n=(-height_scale*gradient,1); undo display exaggeration. The y guard
    // bounds nearly vertical faces; this is a visual grade, not a foam law.
    float downhill = max(0.0,dot(normal.xz,velocity)/
                          (speed*max(normal.y,0.05)*max(height_scale,0.001)));
    float support = smoothstep(max(wet_threshold,0.002),
                               max(wet_threshold+0.001,0.08),depth);
    return smoothstep(0.08,1.4,speed)*smoothstep(0.2,1.0,downhill)*support;
}

// Resets occur only at zero layer weight. Eight-second phases repeat exactly
// across the existing 1024 s presentation-clock wrap. No history buffer.
vec4 fluid25d_rapid_phases(float clock, float dephase) {
    vec2 phase = fract(vec2(clock/8.0+dephase)+vec2(0.0,0.5));
    return vec4(phase,1.0-abs(phase*2.0-1.0));
}

// Lift horizontal velocity onto the physical surface, then exaggerate only
// cosmetic motion. No coordinates are rotated by this spatially varying field.
vec3 fluid25d_cascade_flow(vec3 normal, vec2 velocity, float height_scale) {
    if (length(velocity)<=0.00001 || normal.y<=0.0) return vec3(0);
    vec3 tangent=vec3(velocity.x,-dot(normal.xz,velocity)/
                     (max(normal.y,0.05)*max(height_scale,0.001)),velocity.y);
    float speed=length(tangent);
    return tangent/speed*clamp(speed*3.0,6.0,24.0);
}

// Artist impact proxy. A steady inclined plane does not produce landing foam;
// a wet steep upstream reach entering flatter moving water can. No history.
float fluid25d_cascade_support(float depth, float wet_threshold) {
    // Keep centimetre-scale hillside films on the accepted wet-ground path.
    return smoothstep(max(wet_threshold,0.08),max(wet_threshold+0.001,0.20),depth);
}

float fluid25d_cascade_replacement(float gain, float pattern) {
    // Zero pale floor, unchanged 94% bright peak. Does not alter water coverage.
    return gain*(0.94*pattern);
}
// Lighter general-stream branch. Fast flat water is eligible, but supported
// depth and speed still exclude rain films and stationary lake interiors.
float fluid25d_stream_foam_activity(float speed, float depth, float steep_activity,
                                   float wet_threshold) {
    return smoothstep(0.8,4.0,speed)*fluid25d_cascade_support(depth,wet_threshold)*
           (1.0-clamp(steep_activity,0.0,1.0));
}
// Shape coverage separately from lit opacity. Threshold sparse layers BEFORE
// crossfading, so two moderately bright noise values do not extinguish the
// patches at mid-phase. This is an artist mask, not transported foam density.
// A short transition near zero keeps the retained post-blend mask continuous.
float fluid25d_stream_foam_shape(vec2 samples, vec2 weights, float patchiness,
                                vec3 edges, vec2 envelope) {
    float dense=smoothstep(0.50-edges.x,0.62+edges.x,dot(samples,weights));
    float low=mix(0.50,0.65,patchiness);
    float high=mix(0.62,0.75,patchiness);
    vec2 patches=smoothstep(vec2(low)-edges.yz,vec2(high)+edges.yz,samples);
    return mix(dense,dot(patches*envelope,weights),smoothstep(0.0,0.2,patchiness));
}
float fluid25d_stream_foam_shape(vec2 samples, vec2 weights, float patchiness,
                                vec3 edges) {
    return fluid25d_stream_foam_shape(samples,weights,patchiness,edges,vec2(1));
}
// Unbounded world noise warps the repeating detail domain and supplies a broad
// coverage envelope. Fixed rotations, unequal scales: no 64 m tile reset, no
// rotation by a varying velocity. Call with each layer's ADVECTED position.
vec3 fluid25d_stream_foam_domain(vec2 p) {
    vec2 q=mat2(0.8,0.6,-0.6,0.8)*p;
    float broad=cubey_proc_value_noise_pcg_2d(q/173.0+vec2(11.7,29.4));
    float cross=cubey_proc_value_noise_pcg_2d(q/239.0+vec2(-8.2,17.3));
    vec2 uv=mat2(0.96,-0.28,0.28,0.96)*p/64.0+
            0.55*(vec2(broad,cross)-0.5);
    return vec3(uv,broad);
}
float fluid25d_landing_activity(float local_grade, float upstream_grade,
                               float speed, vec3 depths, float wet_threshold) {
    float support=fluid25d_cascade_support(min(depths.x,min(depths.y,depths.z)),wet_threshold);
    float flattening=smoothstep(0.15,0.7,upstream_grade-max(local_grade,0.0));
    return support*smoothstep(0.08,1.4,speed)*smoothstep(0.4,1.0,upstream_grade)*
           (1.0-smoothstep(0.25,0.75,max(local_grade,0.0)))*flattening*
           (local_grade>=-0.05 ? 1.0 : 0.0);
}

#endif
