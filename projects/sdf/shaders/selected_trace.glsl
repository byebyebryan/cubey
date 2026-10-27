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
    // Uniform-cell labels are not an exact distance cache. Evaluate the source
    // there, including hidden features, rather than treating labels as samples.
    if((header&SUBDIVIDED)==0u) return 0.0;
    uint pointer=header&POINTER;
    float g=sampleBrick(p,cell,pointer);
    // A local brick of a 1-Lipschitz field spans at most sqrt(3)/256.
    // R16 relative rounding <=2^-10*max(|corner|), plus subnormal and
    // arithmetic allowance. This intentionally overestimates R16 error.
    float cornerBound=abs(g)+sqrt(3.0)/256.0;
    float E=sqrt(3.0)*.5/(256.0*7.0)+cornerBound/1024.0+max(1e-5,uintBitsToFloat(authoredHeader.w));
    vec3 low=vec3(cell)/256.0-.5;
    vec2 interval=sampledRayBox(p,direction,low,low+1.0/256.0);
    return min(max(abs(g)-E,0.0),max(interval.y,0.0));
}
#define SDF_STEP_BOUND hybridBound
#endif
#include "source_trace.glsl"
