// Selected authored-source tracer. Kept separate from the frozen sampled path.
#ifndef SDF_SOURCE_TRACE_GLSL
#define SDF_SOURCE_TRACE_GLSL
const float VOXEL = 1.0 / (256.0*7.0);
const float EPSILON = VOXEL/32.0;
vec2 rayBox(vec3 o,vec3 d,vec3 low,vec3 high) {
    vec3 inv=1.0/mix(d,vec3(1e-20),lessThan(abs(d),vec3(1e-20)));
    vec3 a=(low-o)*inv,b=(high-o)*inv;
    return vec2(max(min(a,b).x,max(min(a,b).y,min(a,b).z)),
                min(max(a,b).x,min(max(a,b).y,max(a,b).z)));
}
struct Hit { float t; int status; uint steps; ivec3 cell; uint pointer; };
#ifndef SDF_SOURCE_DISTANCE
#define SDF_SOURCE_DISTANCE authoredDistance
#endif
Hit trace(vec3 origin,vec3 direction,float limit) {
    Hit hit=Hit(0,0,0u,ivec3(0),0u);
#ifdef SDF_ACCELERATION
    if(regionHeader.y!=0u) return hit;
    vec2 root=rayBox(origin,direction,modelLow.xyz,modelHigh.xyz);
#elif defined(SDF_TIGHT_ROOT)
    // Keep an empty margin around the .499 authored clip. Using the exact clip
    // interval loses an inside exit when a cached step rounds past its endpoint.
    // This padded box still keeps initial points safely inside the .5 region domain.
    vec2 root=rayBox(origin,direction,vec3(-.4995),vec3(.4995));
#else
    vec2 root=rayBox(origin,direction,vec3(-.5),vec3(.5));
#endif
    float t=max(root.x,0.0),end=min(root.y,limit);
    if(t>end) {
#if defined(SDF_WORKLOAD) && defined(SDF_ACCELERATION)
        workloadAid[workloadPhase].w++;
#endif
        return hit;
    }
    for(uint i=0u;i<4096u && t<=end;++i) {
        ++hit.steps;
#ifdef SDF_WORKLOAD
        workloadWalk[workloadPhase].x++;
#endif
        vec3 p=origin+direction*t;
#ifdef SDF_STEP_BOUND
        float cached=SDF_STEP_BOUND(p,direction);
        if(cached>EPSILON) {
#ifdef SDF_QUERY_WORKLOAD
            if(workloadPhase==0u) queryEndRun();
#endif
            t+=cached;continue;
        }
#endif
        float d=SDF_SOURCE_DISTANCE(p);
        if(abs(d)<=EPSILON) { hit.t=t;hit.status=1;return hit; }
        // All supported primitives/operators are 1-Lipschitz. abs(d) also
        // bounds the distance to the exit for a ray starting inside a solid.
        float next=t+abs(d);
        if(next==t) { hit.t=t;hit.status=2;return hit; }
        t=next;
    }
#ifdef SDF_WORKLOAD
    if(workloadPhase==0u) workloadIntervalOvershoot=max(t-end,0.0);
#endif
    if(t<=end) {hit.t=t;hit.status=2;}
    return hit;
}
vec3 hitNormal(vec3 p,Hit hit) {
    vec3 g;
    for(int axis=0;axis<3;++axis) {
        vec3 delta=vec3(0);delta[axis]=VOXEL/16.0;
        g[axis]=SDF_SOURCE_DISTANCE(p+delta)-SDF_SOURCE_DISTANCE(p-delta);
    }
    return length(g)>1e-12 ? normalize(g) : vec3(0,1,0);
}
#endif
