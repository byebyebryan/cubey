#include "field.glsl"
#include "authored.glsl"
layout(push_constant) uniform Params {uint count;uint atlas_axis;} pc;
#define atlasAxis pc.atlas_axis
#ifdef SDF_SAMPLED
#include "trace.glsl"
#else
#include "selected_trace.glsl"
#endif
layout(local_size_x=64) in;
struct Query {vec4 origin;vec4 direction;};
struct Result {vec4 hit;vec4 normal;};
layout(std430,set=0,binding=2) readonly buffer Queries { Query queries[]; };
layout(std430,set=0,binding=15) writeonly buffer Results { Result results[]; };
void main() {
    uint i=gl_GlobalInvocationID.x;if(i>=pc.count) return;
    Query q=queries[i];
    if(q.origin.w>.5) {
#ifdef SDF_PRUNED
        float d=regionDistance(q.origin.xyz);
#else
        float d=authoredDistance(q.origin.xyz);
#endif
        results[i]=Result(vec4(d,0,0,0),vec4(0));return;
    }
    Hit h=trace(q.origin.xyz,q.direction.xyz,3.0);
    vec3 p=q.origin.xyz+h.t*q.direction.xyz;
    results[i]=Result(vec4(h.t,h.status,h.steps,h.status==1 ? authoredDistance(p) : 0),
                      vec4(h.status==1 ? hitNormal(p,h) : vec3(0),0));
}
