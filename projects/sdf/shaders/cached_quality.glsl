#ifdef SDF_CACHED_NORMAL
#include "regions.glsl"
vec3 cachedSourceNormal(vec3 p) {
    vec3 g;
    for(int i=0;i<3;++i) {
        vec3 delta=vec3(0);delta[i]=VOXEL/16.0;
        g[i]=regionDistance(p+delta)-regionDistance(p-delta);
    }
    return length(g)>1e-12 ? normalize(g) : vec3(0,1,0);
}
#endif
#ifdef SDF_CACHED_REFINE
#include "cached_cell_math.glsl"
Hit refineCached(vec3 origin,vec3 direction,float limit,Hit coarse) {
    if(coarse.status!=1)return coarse;
    vec2 root=rayBox(origin,direction,vec3(-.5),vec3(.5));
    float t=coarse.t,end=min(root.y,limit);
    bool inside=false;
    if(root.x<=0.0) {
        ivec3 c;uint l;uint h=lookup(origin,direction,c,l);
        inside=(h&FILLED)!=0u || ((h&SUBDIVIDED)!=0u && sampleBrick(origin,c,h&POINTER)<-EPSILON);
    }
    uint work=0u;
    for(uint i=0u;i<64u && t<=end;++i) {
        work=i+1u;
        vec3 p=origin+direction*t;ivec3 cell;uint level;
        uint header=lookup(p,direction,cell,level);
        float dim=float(1u<<min(level,8u));vec3 low=vec3(cell)/dim-.5;
        float leave=rayBox(p,direction,low,low+1.0/dim).y;
        if((header&SUBDIVIDED)==0u) {
            if((header&FILLED)!=0u && !inside) {
                coarse.t=t;coarse.cell=cell;coarse.pointer=0u;coarse.steps+=i+1u;return coarse;
            }
            t=uintBitsToFloat(floatBitsToUint(t+max(leave,0.0))+1u);continue;
        }
        uint pointer=header&POINTER;
        vec3 local=clamp((p+.5)*256.0-vec3(cell),0.0,1.0)*7.0;
        ivec3 voxel=min(ivec3(floor(local)),ivec3(6));
        ivec3 base=brickOrigin(pointer,atlasAxis)+voxel;
        float values[8];
        for(int k=0;k<8;++k)values[k]=texelFetch(atlas,base+ivec3(k&1,(k>>1)&1,(k>>2)&1),0).r;
        float g=cachedCellValue(values,local-vec3(voxel));
        if(isnan(g) || isinf(g)) {coarse.status=2;coarse.steps+=work;return coarse;}
        if(abs(g)<=VOXEL*float(SDF_CACHED_EPSILON)) {
            coarse.t=t;coarse.cell=cell;coarse.pointer=pointer;coarse.steps+=i+1u;return coarse;
        }
        // A sign crossing beyond the hit band is unresolved, not a new hit.
        if((inside ? -g : g)<0.0) {coarse.steps+=work;return coarse;}
        float guard=16.0*1.1920928955078125e-7*(1.0+length(p)+abs(g));
        float radius=max(abs(g)-guard,0.0)/cachedCellLipschitz(values,VOXEL);
        vec3 voxelLow=low+vec3(voxel)*VOXEL;
        float cellExit=max(rayBox(p,direction,voxelLow,voxelLow+VOXEL).y,0.0);
        float next=t+min(radius,cellExit);
        if(radius>=cellExit)next=uintBitsToFloat(floatBitsToUint(next)+1u);
        if(isnan(next) || isinf(next)) {coarse.status=2;coarse.steps+=work;return coarse;}
        if(next<=t) {coarse.steps+=work;return coarse;}
        t=next;
    }
    if(t>end) {coarse.status=0;coarse.t=0.0;}
    // Exhausting the optional quality budget retains the original approximation.
    coarse.steps+=work;return coarse;
}
#endif
