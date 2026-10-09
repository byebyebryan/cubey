#ifndef FLUID_25D_WATER_FILM_GLSL
#define FLUID_25D_WATER_FILM_GLSL
// Visual-demo controls, not wetness history or a hydraulic/coverage threshold.
// h is sampled physical depth, never exaggerated gap or optical ray length.
float fluid25d_film_weight(float h, float begin_m, float end_m) {
    return 1.0-smoothstep(begin_m,end_m,h);
}
// Artist opacity only: not concentrated flow, apparent depth or removed mass.
// No spatial or temporal noise; deep water and zero strength retain identity.
float fluid25d_shallow_coverage(float h, float strength, float end_m) {
    return mix(1.0,smoothstep(0.0,end_m,h),strength);
}
vec3 fluid25d_film_ground_emphasis(vec3 water, vec3 ground, float weight, float amount) {
    // Blend the complete HDR result. Attenuating reflection alone while leaving
    // (1-Fresnel) transmission would blacken grazing films. Alpha is untouched.
    return mix(water,ground,weight*amount);
}
#endif
