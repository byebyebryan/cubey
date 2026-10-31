#ifndef SDF_CACHED_STROKE_TRACE
#define SDF_CACHED_STROKE_TRACE
#define trace strokeBaseTrace
#define hitNormal strokeBaseNormal
#include "trace.glsl"
#undef trace
#undef hitNormal
layout(std430,set=0,binding=21) readonly buffer StrokeControls {
    uvec4 strokeHeader;
    SceneInstruction strokeTools[8];
};
const uint STROKE_SURFACE=0xfffffff0u;
// Kept for existing diagnostic entrypoints; stroke diagnostics test the range.
const uint EDIT_SURFACE=STROKE_SURFACE;
vec2 strokeInterval(vec3 origin,vec3 direction,uint index) {
    SceneInstruction tool=strokeTools[index];
    vec3 offset=origin-tool.center_code.xyz;
    float radius=tool.parameters.x*tool.settings.x,along=dot(offset,direction);
    vec3 perpendicular=offset-direction*along;
    float discriminant=radius*radius-dot(perpendicular,perpendicular);
    float projectionError=8.0*1.1920929e-7*length(offset);
    float roundoff=2.0*radius*projectionError+projectionError*projectionError+
        8.0*1.1920929e-7*radius*radius;
    if(discriminant < -roundoff)return vec2(1,-1);
    float chord=sqrt(max(discriminant,0.0));return vec2(-along-chord,-along+chord);
}
bool strokeInside(vec3 p,vec3 direction) {
    if(strokeHeader.z==0u||any(greaterThan(abs(p),vec3(.5))))return false;
    ivec3 cell;uint level;uint h=lookup(p,direction,cell,level);
    return (h&SUBDIVIDED)==0u?(h&FILLED)!=0u:sampleBrick(p,cell,h&POINTER)<0.0;
}
Hit strokeHit(float t,uint steps,uint owner) {return Hit(t,1,steps,ivec3(0),STROKE_SURFACE+owner);}
Hit trace(vec3 origin,vec3 direction,float limit) {
    vec2 intervals[8];uvec2 owners[8];uint count=0u;
    vec2 workspace=rayBox(origin,direction,vec3(-.499),vec3(.499));
    bool subtract=strokeHeader.y!=0u;
    for(uint i=0u;i<strokeHeader.x;i++) {
        vec2 v=strokeInterval(origin,direction,i);
        if(!subtract)v=vec2(max(v.x,workspace.x),min(v.y,workspace.y));
        if(v.x>v.y||v.y<0.0||v.x>limit)continue;
        uint at=count;
        while(at>0u&&intervals[at-1u].x>v.x){intervals[at]=intervals[at-1u];owners[at]=owners[at-1u];--at;}
        intervals[at]=v;owners[at]=uvec2(i);++count;
    }
    uint merged=0u;
    for(uint i=0u;i<count;i++) {
        if(merged>0u&&intervals[i].x<=intervals[merged-1u].y) {
            if(intervals[i].y>intervals[merged-1u].y){intervals[merged-1u].y=intervals[i].y;owners[merged-1u].y=owners[i].y;}
        } else {intervals[merged]=intervals[i];owners[merged]=owners[i];++merged;}
    }
    float offset=0.0;uint steps=1u;
    for(uint pass=0u;pass<9u;pass++) {
        // One call site retains the cheap existing Cached walk.
        Hit base=strokeHeader.z==0u?Hit(0,0,0u,ivec3(0),0u):
            strokeBaseTrace(origin+direction*offset,direction,limit-offset);
        if(base.status!=0)base.t+=offset;base.steps+=steps;
        if(pass>=merged)return base;
        vec2 tool=intervals[pass];uvec2 owner=owners[pass];
        if(tool.x>=offset) {
            if(base.status!=0&&base.t<tool.x)return base;
            bool inside=strokeInside(origin+direction*offset,direction);
            if(subtract==inside)return strokeHit(tool.x,base.steps,owner.x);
        }
        if(tool.y>limit)return Hit(0,0,base.steps,ivec3(0),0u);
        if(tool.y==workspace.y)return subtract?Hit(0,0,base.steps,ivec3(0),0u):strokeHit(tool.y,base.steps,owner.y);
        float resume=tool.y+1e-6;
        bool inside=strokeInside(origin+direction*resume,direction);
        if(subtract==inside)return strokeHit(tool.y,base.steps,owner.y);
        if(pass+1u==merged&&(base.status==0||(base.status==1&&base.t>=resume)))return base;
        if(resume>limit)return Hit(0,0,base.steps,ivec3(0),0u);
        offset=resume;steps=base.steps;
    }
    return Hit(0,2,steps,ivec3(0),0u);
}
vec3 hitNormal(vec3 p,Hit hit) {
    if(hit.pointer<STROKE_SURFACE)return strokeBaseNormal(p,hit);
    uint index=hit.pointer-STROKE_SURFACE;
    vec3 delta=p-strokeTools[index].center_code.xyz;
    vec3 n=length(delta)>1e-12?normalize(delta):vec3(0,1,0);
    if(strokeHeader.y==0u && boxDistance(p,vec3(.499))>
        authoredPrimitive(p,strokeTools[index])) {
        vec3 a=abs(p);int axis=a.x>a.y?(a.x>a.z?0:2):(a.y>a.z?1:2);
        n=vec3(0);n[axis]=p[axis]<0.0?-1.0:1.0;
    }
    return strokeHeader.y!=0u?-n:n;
}
#endif
