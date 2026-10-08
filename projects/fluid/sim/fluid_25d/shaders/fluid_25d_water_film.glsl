#ifndef FLUID_25D_WATER_FILM_GLSL
#define FLUID_25D_WATER_FILM_GLSL
// Visual-demo controls, not wetness history or a hydraulic/coverage threshold.
// h is sampled physical depth, never exaggerated gap or optical ray length.
float fluid25d_film_weight(float h, float begin_m, float end_m) {
    return 1.0-smoothstep(begin_m,end_m,h);
}
vec3 fluid25d_film_ground_emphasis(vec3 water, vec3 ground, float weight, float amount) {
    // Blend the complete HDR result. Attenuating reflection alone while leaving
    // (1-Fresnel) transmission would blacken grazing films. Alpha is untouched.
    return mix(water,ground,weight*amount);
}
#endif
