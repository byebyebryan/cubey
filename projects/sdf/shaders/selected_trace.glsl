#ifdef SDF_PRUNED
#include "regions.glsl"
#endif
#ifdef SDF_HYBRID
// Retain the sampled lookup/interpolation implementation, without its hit rule.
#define Hit SampledHit
#define trace sampledTrace
#define hitNormal sampledNormal
#define rayBox sampledRayBox
#define VOXEL SAMPLED_VOXEL
#define EPSILON SAMPLED_EPSILON
#include "trace.glsl"
#undef Hit
#undef trace
#undef hitNormal
#undef rayBox
#undef VOXEL
#undef EPSILON
float hybridBound(vec3 p,vec3 direction) {
    ivec3 cell;uint level;
    uint header=lookup(p,direction,cell,level);
#ifdef SDF_WORKLOAD
    workloadWalk[workloadPhase].y++;
#endif
    // Uniform-cell labels are not an exact distance cache. Evaluate the source
    // there, including hidden features, rather than treating labels as samples.
    if((header&SUBDIVIDED)==0u) {
#ifdef SDF_WORKLOAD
        workloadWalk[workloadPhase].w++;
#endif
        return 0.0;
    }
    uint pointer=header&POINTER;
    float g=sampleBrick(p,cell,pointer);
    // A local brick of a 1-Lipschitz field spans at most sqrt(3)/256.
    // R16 relative rounding <=2^-10*max(|corner|), plus subnormal and
    // arithmetic allowance. This intentionally overestimates R16 error.
    float cornerBound=abs(g)+sqrt(3.0)/256.0;
    float E=sqrt(3.0)*.5/(256.0*7.0)+cornerBound/1024.0+max(1e-5,uintBitsToFloat(authoredHeader.w));
#ifdef SDF_ACCELERATION
    // Source arithmetic plus an independent lookup/interpolation allowance.
    // Direction-biased lookup may clamp p by <=sqrt(3)*1e-6 at a cell face.
    E=sqrt(3.0)*.5/(256.0*7.0)+cornerBound/1024.0+
        uintBitsToFloat(authoredHeader.w)+1e-5;
#endif
    vec3 low=vec3(cell)/256.0-.5;
    vec2 interval=sampledRayBox(p,direction,low,low+1.0/256.0);
#ifdef SDF_UNCLAMPED_BOUND
    // This point bound is global; the next iteration performs a fresh lookup.
    float pointBound=max(abs(g)-E,0.0);
#ifdef SDF_WORKLOAD
    if(pointBound>1.0/(256.0*7.0*32.0)) workloadWalk[workloadPhase].z++;
#endif
    return pointBound;
#else
#ifdef SDF_WORKLOAD
    float bound=min(max(abs(g)-E,0.0),max(interval.y,0.0));
    if(bound>1.0/(256.0*7.0*32.0)) workloadWalk[workloadPhase].z++;
    return bound;
#else
    return min(max(abs(g)-E,0.0),max(interval.y,0.0));
#endif
#endif
}
#define SDF_STEP_BOUND hybridBound
#endif
#ifdef SDF_GUARDED_STEPS
#define trace referenceTrace
#include "source_trace.glsl"
#undef trace
#include "guarded_trace.glsl"
#else
#include "source_trace.glsl"
#endif
