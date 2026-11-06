// Visible-normal sampling adapted from NVIDIA nvpro_core2,
// pbr_ggx_microfacet.h.slang at 8746c6d36bebe652516b9a1bf3cce46422413bf7.
// Copyright (c) 2022-2026 NVIDIA CORPORATION. SPDX-License-Identifier: Apache-2.0
// The joint visibility and BRDF evaluation use Cubey's existing correlated Smith
// model, not the donor's uncorrelated G1*G2 shortcut. See the project NOTICE.
#ifndef CUBEY_RT_MATERIAL_BSDF_GLSL
#define CUBEY_RT_MATERIAL_BSDF_GLSL

vec3 rtVisibleGgxNormal(vec3 k, float alpha, vec2 xi) {
    vec3 v = normalize(vec3(k.xy * alpha, k.z));
    vec3 t1 = v.z < .99999 ? normalize(cross(v, vec3(0, 0, 1))) : vec3(1, 0, 0);
    vec3 t2 = cross(t1, v);
    float a = 1 / (1 + v.z), r = sqrt(xi.x);
    float phi = xi.y < a ? xi.y / a * CUBEY_PBR_PI
                         : CUBEY_PBR_PI + (xi.y-a) / (1-a) * CUBEY_PBR_PI;
    float sp = sin(phi), p1 = r * cos(phi), p2 = r * sp * (xi.y < a ? 1 : v.z);
    // Algebraically identical to 1-p1^2-p2^2, retaining precision at the disk
    // rim before the small-alpha unstretch amplifies float cancellation.
    float residual = (1-xi.x) + (xi.y < a ? 0 : xi.x*sp*sp*dot(v.xy,v.xy));
    vec3 h = p1*t1 + p2*t2 + sqrt(max(0, residual))*v;
    return normalize(vec3(h.xy*alpha, max(0, h.z)));
}

float rtGgxDistribution(vec3 h, float alpha) {
    // Unclamped analytic NDF for the probability law. The shared BRDF's D floor
    // is an evaluation convention; using that floor in a PDF loses probability.
    float a2 = alpha*alpha;
    float denominator = dot(h.xy, h.xy) + a2*h.z*h.z;
    return a2 / (CUBEY_PBR_PI * denominator * denominator);
}

float rtGgxG1(vec3 v, float alpha) {
    if (v.z <= 0) return 0;
    return 2*v.z / (v.z + sqrt(v.z*v.z + alpha*alpha*dot(v.xy, v.xy)));
}

float rtGgxReflectionPdf(vec3 v, vec3 l, float roughness) {
    if (v.z <= 0 || l.z <= 0 || dot(v+l, v+l) <= 1e-20) return 0;
    vec3 h = normalize(v+l);
    if (h.z <= 0 || dot(v, h) <= 0) return 0;
    float alpha = roughness*roughness;
    return rtGgxDistribution(h, alpha)*rtGgxG1(v, alpha)/(4*v.z);
}

vec3 rtGgxReflectionEval(vec3 v, vec3 l, float roughness, vec3 f0) {
    if (v.z <= 0 || l.z <= 0 || dot(v+l, v+l) <= 1e-20) return vec3(0);
    vec3 h = normalize(v+l);
    float alpha2 = roughness*roughness*roughness*roughness;
    float denominator = dot(h.xy,h.xy) + alpha2*h.z*h.z;
    // Same D and denominator floor as cubey_pbr_distribution_ggx, expressed
    // without subtracting nearly equal floats around a sharp peak.
    float distribution = alpha2/max(CUBEY_PBR_PI*denominator*denominator,.00001);
    return cubey_pbr_fresnel_schlick(max(dot(v,h),0), f0) *
        distribution *
        cubey_pbr_visibility_smith_ggx_correlated(v.z, l.z, roughness);
}

mat3 rtDirectionFrame(vec3 normal) {
    vec3 tangent = normalize(cross(abs(normal.z) < .999 ? vec3(0,0,1) : vec3(0,1,0), normal));
    return mat3(tangent, cross(normal, tangent), normal);
}

float rtConePdf(vec3 direction, vec3 axis, float oneMinusCosine) {
    return dot(direction, axis) >= 1-oneMinusCosine
        ? 1/(2*CUBEY_PBR_PI*oneMinusCosine) : 0;
}

vec3 rtSampleCone(vec3 axis, float oneMinusCosine, vec2 xi) {
    float cosine = 1-xi.x*oneMinusCosine;
    float sine = sqrt(max(0, (1-cosine)*(1+cosine)));
    float phi = 2*CUBEY_PBR_PI*xi.y;
    return rtDirectionFrame(axis)*vec3(sine*cos(phi), sine*sin(phi), cosine);
}

// A rejected visible-normal direction is an absorption event. Its probability
// remains in the estimator; neither the sampler nor the mixture renormalizes it.
vec3 rtSampleReflection(vec3 v, float roughness, vec2 xi) {
    return reflect(-v, rtVisibleGgxNormal(v, roughness*roughness, xi));
}
#endif
