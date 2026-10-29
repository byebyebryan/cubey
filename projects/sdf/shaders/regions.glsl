#ifndef SDF_REGIONS_GLSL
#define SDF_REGIONS_GLSL
#ifdef SDF_ACCELERATION
layout(std430,set=0,binding=16) readonly buffer Regions {
    uvec4 regionHeader;vec4 modelLow;vec4 modelHigh;uvec4 regionCells[];
};
#else
layout(std430,set=0,binding=16) readonly buffer Regions {uvec4 regionHeader;uvec2 regionCells[];};
#endif
layout(std430,set=0,binding=17) readonly buffer References {uint regionRefs[];};
float regionDistance(vec3 p) {
    if(regionHeader.x==0u || any(greaterThan(abs(p),vec3(.5)))) {
#ifdef SDF_WORKLOAD
        workloadProgram(authoredHeader.x,true);
        workloadBoundaryExcess=max(workloadBoundaryExcess,max(max(abs(p.x),abs(p.y)),abs(p.z))-.5);
#endif
        return authoredDistance(p);
    }
    int dim=int(regionHeader.x);
    ivec3 cell=clamp(ivec3(floor((p+.5)*float(dim))),ivec3(0),ivec3(dim-1));
    uvec2 h=regionCells[(cell.z*dim+cell.y)*dim+cell.x].xy;
    if(h.y==0u) {
#ifdef SDF_WORKLOAD
        workloadProgram(authoredHeader.x,true);
#ifdef SDF_ACCELERATION
        if(regionCells[(cell.z*dim+cell.y)*dim+cell.x].z==1u) workloadAid[workloadPhase].z++;
#endif
#endif
        return authoredDistance(p);
    }
#ifdef SDF_WORKLOAD
    workloadProgram(h.y,false);
#endif
    float stack[32];int size=0;
    for(uint i=0u;i<h.y;++i) {
        uint ref=regionRefs[h.x+i];SceneInstruction node=authoredNodes[ref&0x7fffffffu];
        uint code=uint(node.center_code.w);float d;
        if(code<10u) d=AUTHORED_PRIMITIVE(p,node,ref&0x7fffffffu);
        else {
            float b=stack[--size],a=stack[--size];
            if(code==10u) d=min(a,b);
            else if(code==11u) d=max(a,-b);
            else if(code==13u) {
                float h=clamp(.5+.5*(a+b)/node.settings.y,0,1);
                d=mix(-b,a,h)+node.settings.y*h*(1-h);
            } else {
                float h=clamp(.5+.5*(b-a)/node.settings.y,0,1);
                d=mix(b,a,h)-node.settings.y*h*(1-h);
            }
        }
        stack[size++]=(ref&0x80000000u)!=0u?-d:d;
    }
    return max(stack[0],boxDistance(p,vec3(.499)));
}
#define SDF_SOURCE_DISTANCE regionDistance
#ifdef SDF_FAR_REGIONS
float regionBound(vec3 p,vec3 direction) {
#ifdef SDF_WORKLOAD
    workloadWalk[workloadPhase].y++;
    workloadAid[workloadPhase].x++;
#endif
    if(regionHeader.x==0u || any(greaterThan(abs(p),vec3(.5)))) return 0.0;
    int dim=int(regionHeader.x);
    ivec3 cell=clamp(ivec3(floor((p+.5)*float(dim))),ivec3(0),ivec3(dim-1));
    uvec4 h=regionCells[(cell.z*dim+cell.y)*dim+cell.x];
    float b=h.z==1u ? abs(uintBitsToFloat(h.w)) : 0.0;
#ifdef SDF_WORKLOAD
    if(b>1.0/(256.0*7.0*32.0)) {
        workloadWalk[workloadPhase].z++;
        workloadAid[workloadPhase].y++;
    }
#endif
    return b;
}
#define SDF_STEP_BOUND regionBound
#endif
#endif
