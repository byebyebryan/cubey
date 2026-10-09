#ifndef FLUID_25D_DAYLIGHT_GLSL
#define FLUID_25D_DAYLIGHT_GLSL
float fluid25d_sh_basis(int i, vec3 n) {
    float x=n.x, y=n.y, z=n.z;
    float basis[9] = float[9](0.282095,0.488603*y,0.488603*z,0.488603*x,
        1.092548*x*y,1.092548*y*z,0.315392*(3.0*z*z-1.0),
        1.092548*x*z,0.546274*(x*x-y*y));
    return basis[i];
}
// Normalized Lambert convolution: reconstructed response is E/pi, not E.
float fluid25d_sh_lambert(int i) { return i==0 ? 1.0 : (i<4 ? 2.0/3.0 : 0.25); }
vec2 fluid25d_receiver_plane_gradient(vec3 dx, vec3 dy) {
    float determinant=dx.x*dy.y-dx.y*dy.x;
    if (abs(determinant)<1e-12) return vec2(0);
    // Screen derivatives -> shadow UV depth gradient. Bound degeneracies at silhouettes.
    return clamp(vec2(dx.z*dy.y-dy.z*dx.y,dy.z*dx.x-dx.z*dy.x)/determinant,
                 vec2(-1),vec2(1));
}
float fluid25d_shadow_tent(float offset) { return max(0.0,2.0-abs(offset)); }
#endif
