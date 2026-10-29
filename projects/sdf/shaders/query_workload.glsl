#include "workload.glsl"
#include "field.glsl"
layout(push_constant) uniform Camera {
    mat4 inverseVP;vec4 eye;vec4 sunlight;vec4 options;vec4 forward;
} camera;
#define atlasAxis uint(camera.eye.w)
#include "authored.glsl"
#include "selected_trace.glsl"
#include "authored_material.glsl"
#include "lighting.glsl"
#include "query_workload_layout.glsl"
layout(std430,set=0,binding=18) buffer QueryWorkload {
    uvec4 queryDimensions;QueryWork queryWork[];
};
layout(local_size_x=64) in;
layout(set=0,binding=20) uniform sampler2D queryCamera;
void main() {
    uint id=gl_GlobalInvocationID.x;
    if(id>=queryDimensions.w) return;
    uint cols=(queryDimensions.x+7u)/8u;
    uvec2 tile=uvec2(id%cols,id/cols);
    uvec2 pixel=min(tile*uvec2(8u,4u)+uvec2(4u,2u),queryDimensions.xy-1u);
    vec2 uv=texelFetch(queryCamera,ivec2(pixel),0).xy;
    vec4 farPoint=camera.inverseVP*vec4(uv*2.0-1.0,1,1);
    vec3 direction=normalize(farPoint.xyz/farPoint.w-camera.eye.xyz);
    float groundT=camera.options.z>.5 && direction.y < -1e-6 ?
        (lighting.quality.w-camera.eye.y)/direction.y : 1e6;
    if(groundT<0.0) groundT=1e6;
    Hit hit=trace(camera.eye.xyz,direction,groundT);
#ifdef SDF_GUARDED_STEPS
    guardedPrimary=false;
#endif
    queryEndRun();
    vec3 normal=vec3(0),radiance=vec3(0);
    bool exhausted=hit.status==2;
    float t=hit.status==1 ? hit.t : groundT;
    if(!exhausted && t<1e5) {
        vec3 p=camera.eye.xyz+direction*t;
        workloadPhase=1u;
        normal=hit.status==1 ? hitNormal(p,hit) : vec3(0,1,0);
        workloadPhase=2u;
        float footprint=t*camera.forward.w/max(abs(dot(normal,-direction)),.2);
        radiance=surfaceRadiance(p,normal,-direction,hit.status==1,footprint,exhausted);
    }
    QueryWork result;
    for(uint phase=0u;phase<3u;phase++) {
        result.totals[phase]=workloadTotals[phase];result.walk[phase]=workloadWalk[phase];
    }
    result.runs[0]=queryRuns[0];result.runs[1]=queryRuns[1];result.stats=queryStats;
    result.hit=vec4(hit.t,float(hit.status),float(hit.steps),exhausted ? 1.0 : 0.0);
    result.normal=vec4(normal,0);result.radiance=vec4(radiance,0);
    result.shadingNormal=vec4(queryShadingNormal,0);
    result.identity=uvec4(pixel,id,0x51575231u);
    result.reserved[0]=uvec4(0);result.reserved[1]=uvec4(0);
#ifdef SDF_GUARDED_STEPS
    result.reserved[0]=guardedStats;
#endif
    queryWork[id]=result;
}
