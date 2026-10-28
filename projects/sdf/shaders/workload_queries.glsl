#include "workload.glsl"
#include "field.glsl"
#include "authored.glsl"
layout(push_constant) uniform Params {uvec4 settings;vec4 sunlight;} pc;
#define atlasAxis pc.settings.y
#include "selected_trace.glsl"
layout(local_size_x=64) in;
struct Query {vec4 origin;vec4 direction;};
struct Result {vec4 hit;vec4 normal;};
struct WorkResult {uvec4 totals[3];uvec4 low[3];uvec4 high[3];uvec4 walk[3];uvec4 hit;};
layout(std430,set=0,binding=2) readonly buffer Queries {Query queries[];};
layout(std430,set=0,binding=15) writeonly buffer Results {Result results[];};
layout(std430,set=0,binding=18) writeonly buffer WorkResults {WorkResult workResults[];};
void main() {
    uint i=gl_GlobalInvocationID.x;if(i>=pc.settings.x) return;
    Query q=queries[i];
    Hit h=trace(q.origin.xyz,q.direction.xyz,1e6);
    vec3 p=q.origin.xyz+h.t*q.direction.xyz,normal=vec3(0);
    uint surfaceCount=0u,shadowStatus=0u;
    if(h.status==1) {
        ivec3 cell=clamp(ivec3(floor((p+.5)*float(regionHeader.x))),ivec3(0),ivec3(regionHeader.x-1u));
        surfaceCount=regionCells[(cell.z*int(regionHeader.x)+cell.y)*int(regionHeader.x)+cell.x].y;
        workloadPhase=1u;normal=hitNormal(p,h);
        workloadPhase=2u;
        Hit shadow=trace(p+normal*VOXEL*2.0,pc.sunlight.xyz,5.0);
        shadowStatus=uint(shadow.status);
    }
    results[i]=Result(vec4(h.t,h.status,h.steps,h.status==1 ? authoredDistance(p) : 0),vec4(normal,workloadIntervalOvershoot));
    WorkResult w;
    for(uint phase=0u;phase<3u;++phase) {
        w.totals[phase]=workloadTotals[phase];w.low[phase]=workloadLow[phase];
        w.high[phase]=workloadHigh[phase];w.walk[phase]=workloadWalk[phase];
    }
    w.hit=uvec4(uint(h.status),surfaceCount,shadowStatus,floatBitsToUint(workloadBoundaryExcess));workResults[i]=w;
}
