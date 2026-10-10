#ifndef CUBEY_FLUID25D_WHITEWATER_MOTION_GLSL
#define CUBEY_FLUID25D_WHITEWATER_MOTION_GLSL
#include "cubey/procedural/random.glsl"

float fluid25d_whitewater_age(float presentation_seconds, uint seed) {
    return fract(mod(presentation_seconds,1024.0)*0.5+cubey_proc_hash01_u32(seed+2u));
}
vec2 fluid25d_whitewater_offset(vec2 velocity, float age, float speed, float cell_m) {
    return velocity/max(1.0,length(velocity)/12.0)*(age*2.0*speed)/cell_m;
}
float fluid25d_whitewater_life(float age) {
    return smoothstep(0.0,0.12,age)*(1.0-smoothstep(0.65,1.0,age));
}
vec2 fluid25d_whitewater_current_flow(vec2 current, vec2 filtered) {
    return length(current)<=0.00001 || dot(current,filtered)<=0.0 ? current : filtered;
}
#endif
