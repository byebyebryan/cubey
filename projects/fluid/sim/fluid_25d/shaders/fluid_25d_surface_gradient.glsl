// Minimal GLSL adaptation of Mikkelsen's surface-gradient framework.
// Upstream pin and full MIT notice: third_party/surface_gradient/README.md.
// Copyright (c) 2020 mmikk. SPDX-License-Identifier: MIT
#ifndef CUBEY_FLUID25D_SURFACE_GRADIENT_GLSL
#define CUBEY_FLUID25D_SURFACE_GRADIENT_GLSL

float fluid25d_detail_fade(float footprint) {
    return 1.0-smoothstep(18.0,72.0,footprint);
}

vec2 fluid25d_normal_derivative(vec2 encoded_xy) {
    vec2 xy = encoded_xy*2.0-1.0;
    float z = sqrt(max(1.0-dot(xy,xy),0.0));
    float denominator = max(max(z,(1.0/128.0)*max(abs(xy.x),abs(xy.y))),1e-8);
    // No green-channel flip: the procedural texture encodes -dH/d(u,v),
    // and each projection increases its positive world-coordinate axes.
    return -xy/denominator;
}

vec3 fluid25d_triplanar_surface_gradient(vec3 normal, vec3 weights,
                                       vec2 dx, vec2 dy, vec2 dz) {
    // Projection UVs are ZY, XZ, XY, exactly as in the pinned reference.
    vec3 gradient = vec3(weights.z*dz.x+weights.y*dy.x,
                         weights.z*dz.y+weights.x*dx.y,
                         weights.x*dx.x+weights.y*dy.y);
    return gradient-dot(normal,gradient)*normal;
}

vec3 fluid25d_resolve_surface_normal(vec3 normal, vec3 gradient, float strength) {
    return normalize(normal-gradient*strength);
}
#endif
