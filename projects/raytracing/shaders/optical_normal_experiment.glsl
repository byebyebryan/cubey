// Guarded smooth interface used by the dragon demo and Q1 diagnostic probes.
// Appearance choice: Q1's continuity and flux limitations remain documented.
// eta = transmitted IOR / incident IOR. G and rawS face the incident side.
// The whole interface is selected before either positive-weight branch.
#ifndef CUBEY_RT_OPTICAL_NORMAL_EXPERIMENT_GLSL
#define CUBEY_RT_OPTICAL_NORMAL_EXPERIMENT_GLSL
const float RT_NORMAL_GUARD = 1.52587890625e-5; // 2^-16, frozen before image trials
const uint RT_NORMAL_POLICY_VERSION = 1u;
struct RtNormalInterface {
    vec3 normal, reflected, transmitted;
    float fresnel;
    bool tir;
    uint reason;
};
bool rtNormalFinite(vec3 v) { return !any(isnan(v)) && !any(isinf(v)); }
RtNormalInterface rtNormalAbout(vec3 incoming, vec3 n, float eta) {
    float f = rtDielectricFresnel(dot(-incoming, n), eta);
    bool tir = f >= 1;
    vec3 r = normalize(reflect(incoming, n));
    vec3 t = tir ? vec3(0) : refract(incoming, n, 1 / eta);
    if (!tir && dot(t, t) > 0) t = normalize(t);
    return RtNormalInterface(n, r, t, f, tir, 0u);
}
RtNormalInterface rtGuardedNormalInterface(vec3 incoming, vec3 G, vec3 rawS,
                                          float eta, bool candidate) {
    if (eta == 1) {
        // Do not renormalize the already admitted input or bend an index match.
        return RtNormalInterface(G, reflect(incoming, G), incoming, 0, false, 256u);
    }
    RtNormalInterface geometric = rtNormalAbout(incoming, G, eta);
    if (!candidate) { geometric.reason = 1u; return geometric; }
    uint reason = 0u;
    if (!rtNormalFinite(rawS)) reason = 2u;
    else if (!(dot(rawS, rawS) > 1e-20)) reason = 4u;
    if (reason != 0u) { geometric.reason = reason; return geometric; }
    vec3 S = normalize(rawS);
    if (!(dot(S, G) > RT_NORMAL_GUARD)) reason |= 8u;
    if (!(dot(-incoming, S) > RT_NORMAL_GUARD)) reason |= 16u;
    if (reason != 0u) { geometric.reason = reason; return geometric; }
    RtNormalInterface proposal = rtNormalAbout(incoming, S, eta);
    if (!rtNormalFinite(proposal.reflected) ||
        (!proposal.tir && (!rtNormalFinite(proposal.transmitted) ||
                         !(dot(proposal.transmitted, proposal.transmitted) > 0))))
        reason |= 128u;
    if (proposal.fresnel > 0 && !(dot(proposal.reflected, G) > RT_NORMAL_GUARD))
        reason |= 32u;
    if (proposal.fresnel < 1 && !(dot(proposal.transmitted, G) < -RT_NORMAL_GUARD))
        reason |= 64u;
    if (reason != 0u) { geometric.reason = reason; return geometric; }
    return proposal;
}
#endif
