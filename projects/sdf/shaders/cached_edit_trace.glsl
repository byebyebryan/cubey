#ifndef SDF_CACHED_EDIT_TRACE
#define SDF_CACHED_EDIT_TRACE
#ifdef SDF_STROKE8
#include "cached_stroke_trace.glsl"
#else
// One hard Boolean with a convex sphere / unrounded box. The old cached tracer
// supplies every base boundary and base normal; no base CSG evaluator is used.
#define trace cachedEditBaseTrace
#define hitNormal cachedEditBaseNormal
#include "trace.glsl"
#undef trace
#undef hitNormal
layout(std430,set=0,binding=21) readonly buffer CachedEditControls {
    uvec4 editHeader;
    SceneInstruction editPrimitive;
};
const uint EDIT_SURFACE=0xffffffffu;
vec3 cachedEditRotate(vec3 p,vec4 q) {
    return p+2.0*cross(q.xyz,cross(q.xyz,p)+q.w*p);
}
vec3 cachedEditInterval(vec3 origin,vec3 direction) {
    vec2 interval;
    vec3 offset=origin-editPrimitive.center_code.xyz;
    if(editPrimitive.center_code.w==2.0) {
        float radius=editPrimitive.parameters.x*editPrimitive.settings.x;
        float along=dot(offset,direction);
        vec3 perpendicular=offset-direction*along;
        float discriminant=radius*radius-dot(perpendicular,perpendicular);
        float projectionError=8.0*1.1920929e-7*length(offset);
        float roundoff=2.0*radius*projectionError+projectionError*projectionError+
            8.0*1.1920929e-7*radius*radius;
        if(discriminant < -roundoff)return vec3(1,-1,0);
        float halfChord=sqrt(max(discriminant,0.0));
        interval=vec2(-along-halfChord,-along+halfChord);
    } else {
        vec3 o=cachedEditRotate(offset,editPrimitive.inverse_rotation);
        vec3 d=cachedEditRotate(direction,editPrimitive.inverse_rotation);
        vec3 extent=editPrimitive.parameters.xyz*editPrimitive.settings.x;
        interval=rayBox(o,d,-extent,extent);
    }
    // The tool and final Boolean are clipped to the same canonical workspace.
    vec2 workspace=rayBox(origin,direction,vec3(-.499),vec3(.499));
    // A hard difference composes max(clippedBase,-rawTool). Cropping the cutter
    // at the workspace entry would leave the old Cached hit band just outside
    // that face visible even when the cutter covers it.
    if(editHeader.y!=0u)return vec3(interval,workspace.y);
    return vec3(max(interval.x,workspace.x),min(interval.y,workspace.y),workspace.y);
}
bool cachedEditInside(vec3 p,vec3 direction) {
    if(editHeader.z==0u || any(greaterThan(abs(p),vec3(.5))))return false;
    ivec3 cell;uint level;uint header=lookup(p,direction,cell,level);
    return (header&SUBDIVIDED)==0u ? (header&FILLED)!=0u
        : sampleBrick(p,cell,header&POINTER)<0.0;
}
Hit cachedEditPendingHit(float t,uint steps) {
    return Hit(t,1,steps,ivec3(0),EDIT_SURFACE);
}
Hit cachedEditTraceBase(vec3 origin,vec3 direction,float limit) {
    if(editHeader.z==0u)return Hit(0,0,0u,ivec3(0),0u);
    return cachedEditBaseTrace(origin,direction,limit);
}
Hit trace(vec3 origin,vec3 direction,float limit) {
    vec3 tool=cachedEditInterval(origin,direction);
    bool intersects=editHeader.x!=0u && tool.x<=tool.y && tool.y>=0.0 && tool.x<=limit;
    bool subtract=editHeader.y!=0u;
    uint steps=1u;
    float offset=0.0;
    // Keep one syntactic call site for the existing nested cached walk. Most
    // rays return on the first pass; only a hidden base boundary needs a resume.
    for(uint pass=0u;pass<2u;++pass) {
        Hit base=cachedEditTraceBase(origin+direction*offset,direction,limit-offset);
        if(pass!=0u) {
            if(base.status!=0)base.t+=offset;
            base.steps+=steps;return base;
        }
        if(!intersects)return base;
        steps+=base.steps;
        if(tool.x>=0.0) {
            if(base.status==2 || (base.status==1 && base.t<tool.x))return base;
            // Inside the tool a union is solid and a subtraction is empty,
            // irrespective of any hidden base crossings.
            bool inside=cachedEditInside(origin,direction);
            if(subtract==inside)return cachedEditPendingHit(tool.x,steps);
        }
        if(tool.y>limit)return Hit(0,0,steps,ivec3(0),0u);
        if(tool.y==tool.z)return subtract?Hit(0,0,steps,ivec3(0),0u):cachedEditPendingHit(tool.y,steps);
        float resume=tool.y+1e-6; // Existing Cached boundary crossing policy.
        bool inside=cachedEditInside(origin+direction*resume,direction);
        if(subtract==inside)return cachedEditPendingHit(tool.y,steps);
        if(base.status==0 || (base.status==1 && base.t>=resume))return base;
        if(resume>limit)return Hit(0,0,steps,ivec3(0),0u);
        offset=resume;
    }
    return Hit(0,2,steps,ivec3(0),0u);
}
float cachedEditToolDistance(vec3 p) {
    return max(authoredPrimitive(p,editPrimitive),boxDistance(p,vec3(.499)));
}
vec3 hitNormal(vec3 p,Hit hit) {
    if(hit.pointer!=EDIT_SURFACE)return cachedEditBaseNormal(p,hit);
    vec3 gradient;
    for(int axis=0;axis<3;++axis) {
        vec3 d=vec3(0);d[axis]=VOXEL*.5;
        gradient[axis]=cachedEditToolDistance(p+d)-cachedEditToolDistance(p-d);
    }
    vec3 n=length(gradient)>1e-12?normalize(gradient):vec3(0,1,0);
    return editHeader.y!=0u?-n:n;
}
#endif
#endif
