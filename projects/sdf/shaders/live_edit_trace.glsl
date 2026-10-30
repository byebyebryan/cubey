#ifndef SDF_LIVE_EDIT_TRACE
#define SDF_LIVE_EDIT_TRACE
// Private frozen-base experiment. The complete canonical program is binding 3;
// base-region references address its immutable prefix. No uniform-cell label is
// interpreted as a signed distance value.
layout(std430,set=0,binding=21) readonly buffer LiveEditControls {
    uvec4 liveHeader; // base instructions, suffix instructions, 0 sampled / 1 source, reserved
    vec4 liveInfo;   // base arithmetic guard, reserved
};
#define Hit LiveCachedHit
#define trace liveUnusedCachedTrace
#define hitNormal liveUnusedCachedNormal
#define rayBox liveCachedRayBox
#define VOXEL LIVE_CACHED_VOXEL
#define EPSILON LIVE_CACHED_EPSILON
#include "trace.glsl"
#undef Hit
#undef trace
#undef hitNormal
#undef rayBox
#undef VOXEL
#undef EPSILON
#include "cached_cell_math.glsl"
#include "regions.glsl"
const float VOXEL=1.0/(256.0*7.0);
const float EPSILON=VOXEL/32.0;
const float LIVE_INFINITY=1e20;
struct Hit {float t;int status;uint steps;ivec3 cell;uint pointer;};
vec2 rayBox(vec3 o,vec3 d,vec3 lo,vec3 hi) {return liveCachedRayBox(o,d,lo,hi);}
#ifdef SDF_LIVE_COUNTS
uvec4 liveCounts=uvec4(0); // cache lookups, source-prefix fallbacks, brick reads, uniform reads
uvec2 liveInstructions=uvec2(0); // source prefix references, suffix instructions
#define LIVE_COUNT(i) liveCounts[i]++
#else
#define LIVE_COUNT(i)
#endif
float liveOperator(float a,float b,SceneInstruction n) {
    uint code=uint(n.center_code.w);
    if(code==10u)return min(a,b);
    if(code==11u)return max(a,-b);
    if(code==13u) {
        float h=clamp(.5+.5*(a+b)/n.settings.y,0.0,1.0);
        return mix(-b,a,h)+n.settings.y*h*(1.0-h);
    }
    float h=clamp(.5+.5*(b-a)/n.settings.y,0.0,1.0);
    return mix(b,a,h)-n.settings.y*h*(1.0-h);
}
// Source geometry is composed before workspace clipping. Using regionDistance
// here would insert the base's final clipping into a smooth expression.
float liveBaseRaw(vec3 p) {
    if(liveHeader.x==0u)return 1000.0;
    LIVE_COUNT(1);
    uvec2 references=uvec2(0u);
    if(regionHeader.x!=0u && all(lessThanEqual(abs(p),vec3(.5)))) {
        int dim=int(regionHeader.x);
        ivec3 c=clamp(ivec3(floor((p+.5)*float(dim))),ivec3(0),ivec3(dim-1));
        references=regionCells[(c.z*dim+c.y)*dim+c.x].xy;
    }
    bool full=references.y==0u;
    uint count=full?liveHeader.x:references.y;
#ifdef SDF_LIVE_COUNTS
    liveInstructions.x+=count;
#endif
    float values[32];int size=0;
    for(uint i=0u;i<count;++i) {
        uint ref=full?i:regionRefs[references.x+i];
        SceneInstruction n=authoredNodes[ref&0x7fffffffu];
        float d;
        if(n.center_code.w<10.0)d=authoredPrimitive(p,n);
        else {float b=values[--size],a=values[--size];d=liveOperator(a,b,n);}
        values[size++]=(ref&0x80000000u)!=0u?-d:d;
    }
    return values[0];
}
vec2 liveFold(vec3 p,vec2 base) {
    vec2 values[32];int size=liveHeader.x==0u?0:1;
    if(size!=0)values[0]=base;
#ifdef SDF_LIVE_COUNTS
    liveInstructions.y+=liveHeader.y;
#endif
    for(uint j=0u;j<liveHeader.y;++j) {
        SceneInstruction n=authoredNodes[liveHeader.x+j];
        if(n.center_code.w<10.0) {
            float d=authoredPrimitive(p,n);values[size++]=vec2(d);
        } else {
            vec2 b=values[--size],a=values[--size];
            // Union / smooth union are increasing in both arguments. Difference
            // is increasing in A and decreasing in its uncomplemented cutter B.
            bool cut=n.center_code.w==11.0 || n.center_code.w==13.0;
            values[size++]=vec2(liveOperator(a.x,cut?b.y:b.x,n),
                                liveOperator(a.y,cut?b.x:b.y,n));
        }
    }
    if(size==0)return vec2(1000.0);
    return max(values[0],vec2(boxDistance(p,vec3(.499))));
}
struct LiveBase {
    vec2 interval;
    float scalar;
    float lipschitz;
    float exitT;
    bool sampled;
    vec3 low;
    vec3 high;
};
// The legacy cached walk biases lookup by 1e-6 because it also advances by a
// fixed boundary epsilon. This walk has no such minimum step: selecting a cell
// before entering it can give an exit of zero at t=0 (notably shadow origins).
// Use the actual point, choosing the forward cell only at an exact boundary.
ivec3 liveForwardCell(vec3 coordinate,vec3 direction,int dimension) {
    vec3 index=floor(coordinate);
    index-=vec3(lessThan(direction,vec3(0)))*vec3(equal(coordinate,index));
    return clamp(ivec3(index),ivec3(0),ivec3(dimension-1));
}
uint liveLookup(vec3 p,vec3 direction,out ivec3 cell,out uint level) {
    uint header=0u;
    for(level=2u;level<=8u;level+=2u) {
        int dim=1<<int(level);
        cell=liveForwardCell((p+.5)*float(dim),direction,dim);
        header=nodes[LEVEL[level]+morton(cell)];
        if((header&SUBDIVIDED)==0u)break;
    }
    return header;
}
LiveBase liveBase(vec3 p,vec3 direction) {
    LIVE_COUNT(0);
    if(liveHeader.x==0u)return LiveBase(vec2(1000.0),1000.0,1.0,LIVE_INFINITY,false,vec3(-.5),vec3(.5));
    if(any(greaterThan(abs(p),vec3(.5))))
        return LiveBase(vec2(-LIVE_INFINITY,LIVE_INFINITY),0.0,1.0,LIVE_INFINITY,false,vec3(-.5),vec3(.5));
    ivec3 cell;uint level;uint header=liveLookup(p,direction,cell,level);
    float dim=float(1u<<min(level,8u));vec3 low=vec3(cell)/dim-.5;
    float exitT=max(rayBox(p,direction,low,low+1.0/dim).y,0.0);
    float guard=liveInfo.x+2e-6;
    if((header&SUBDIVIDED)==0u) {
        LIVE_COUNT(3);
        float face=min(min(min(p.x-low.x,low.x+1.0/dim-p.x),
                           min(p.y-low.y,low.y+1.0/dim-p.y)),
                       min(p.z-low.z,low.z+1.0/dim-p.z))-guard;
        vec2 interval=(header&FILLED)!=0u ? vec2(-LIVE_INFINITY,-face)
                                          : vec2(face,LIVE_INFINITY);
        // For source composition recover a valid raw-base interval from the
        // clipped cache: max(raw,clip) >= raw, but its lower bound can be lost.
        if(interval.x<=boxDistance(p,vec3(.499))+guard)
            interval.x=-LIVE_INFINITY;
        return LiveBase(interval,0.0,1.0,exitT,false,low,low+1.0/dim);
    }
    LIVE_COUNT(2);
    uint pointer=header&POINTER;
    vec3 local=clamp((p+.5)*256.0-vec3(cell),0.0,1.0)*7.0;
    ivec3 voxel=liveForwardCell(local,direction,7);
    ivec3 atlasPoint=brickOrigin(pointer,atlasAxis)+voxel;
    float values[8];
    for(int k=0;k<8;++k)
        values[k]=texelFetch(atlas,atlasPoint+ivec3(k&1,(k>>1)&1,(k>>2)&1),0).r;
    float g=cachedCellValue(values,local-vec3(voxel));
    float L=max(cachedCellLipschitz(values,VOXEL),1.0);
    vec3 voxelLow=low+vec3(voxel)*VOXEL;
    float voxelExit=max(rayBox(p,direction,voxelLow,voxelLow+VOXEL).y,0.0);
    float cornerBound=abs(g)+sqrt(3.0)/256.0;
    float error=VOXEL*sqrt(3.0)*.5+cornerBound/1024.0+guard+1e-5;
    if(liveHeader.z==0u && g-error>boxDistance(p,vec3(.499))+guard)
        return LiveBase(vec2(g),g,L,min(exitT,voxelExit),true,voxelLow,voxelLow+VOXEL);
    vec2 interval=vec2(g-error,g+error);
    if(interval.x<=boxDistance(p,vec3(.499))+guard)interval.x=-LIVE_INFINITY;
    return LiveBase(interval,g,1.0,LIVE_INFINITY,false,low,low+1.0/dim);
}
float liveCrossBoundary(vec3 origin,vec3 p,vec3 direction,float t,LiveBase base) {
    float best=LIVE_INFINITY;
    for(int axis=0;axis<3;++axis) {
        if(direction[axis]==0.0)continue;
        float face=direction[axis]>0.0?base.high[axis]:base.low[axis];
        // Grid lookup computes p+.5; cross one representable coordinate in
        // that space, then solve from the original ray. Advancing one ULP of
        // a tiny shadow-ray t does not necessarily move its spatial position.
        float shifted=face+.5;
        uint bits=floatBitsToUint(shifted);
        float target=shifted==0.0?face:
            uintBitsToFloat(direction[axis]>0.0?bits+1u:bits-1u)-.5;
        float candidate=(target-origin[axis])/direction[axis];
        if(candidate>=t)best=min(best,candidate);
    }
    return uintBitsToFloat(floatBitsToUint(best)+1u);
}
float livePointDistance(vec3 p) {
    LiveBase base=liveBase(p,vec3(0));
    vec2 interval=liveFold(p,base.interval);
    if(interval.x==interval.y)return interval.x;
    float value=liveBaseRaw(p);
    return liveFold(p,vec2(value)).x;
}
Hit trace(vec3 origin,vec3 direction,float limit) {
    Hit hit=Hit(0.0,0,0u,ivec3(0),0u);
    // Pending additions can occupy cells outside the frozen base's root bounds.
    vec2 root=rayBox(origin,direction,vec3(-.5),vec3(.5));
    float t=max(root.x,0.0),end=min(root.y,limit);
    if(t>end)return hit;
    for(uint i=0u;i<4096u && t<=end;++i) {
        ++hit.steps;
        vec3 p=origin+direction*t;
        LiveBase base=liveBase(p,direction);
        vec2 interval=liveFold(p,base.interval);
        float safe=interval.x>0.0?interval.x:(interval.y<0.0?-interval.y:0.0);
        float step=0.0;
        if(safe>EPSILON && !base.sampled)step=safe;
        else {
            float d;
            if(interval.x==interval.y)d=interval.x;
            else {
                float value=liveBaseRaw(p);
                d=liveFold(p,vec2(value)).x;
            }
            if(isnan(d) || isinf(d)) {hit.status=2;hit.t=t;return hit;}
            if(abs(d)<=EPSILON) {hit.status=1;hit.t=t;return hit;}
            step=abs(d)/base.lipschitz;
        }
        if(liveHeader.z==0u)step=min(step,base.exitT);
        float next=t+step;
        // Cross only the selected classified region/voxel boundary.
        if(liveHeader.z==0u && step>=base.exitT)
            next=liveCrossBoundary(origin,p,direction,t,base);
        if(isnan(next) || isinf(next) || next<=t) {hit.status=2;hit.t=t;return hit;}
        t=next;
    }
    if(t<=end) {hit.status=2;hit.t=t;}
    return hit;
}
vec3 hitNormal(vec3 p,Hit hit) {
    vec3 g;
    for(int axis=0;axis<3;++axis) {
        vec3 d=vec3(0);d[axis]=VOXEL/16.0;
        g[axis]=livePointDistance(p+d)-livePointDistance(p-d);
    }
    return length(g)>1e-12?normalize(g):vec3(0,1,0);
}
#endif
