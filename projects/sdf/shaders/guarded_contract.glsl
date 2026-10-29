// Controlled 1-Lipschitz fields exercise the ACTUAL guarded tracing loop.
#include "field.glsl"
layout(local_size_x=1) in;
const uvec4 authoredHeader=uvec4(0),regionHeader=uvec4(64,0,0,0);
const vec4 modelLow=vec4(-.4995),modelHigh=vec4(.4995);
uint testIndex=0u;
float mockDistance(vec3 p);
float mockBound(vec3 p,vec3 direction) {
    return testIndex==6u && p.x<-.25 ? .1 : 0.0;
}
#define SDF_ACCELERATION
#define SDF_SOURCE_DISTANCE mockDistance
#define SDF_STEP_BOUND mockBound
#define trace referenceTrace
#include "source_trace.glsl"
#undef trace
#include "guarded_trace.glsl"
float mockDistance(vec3 p) {
    if(testIndex==0u) return 2.0*EPSILON; // requires >4096 steps to cross root
    if(testIndex==1u) {
        float first=max(-.1-p.x,p.x-(-.1+.00016));
        float radius=.3-32.0*1.1920928955078125e-7*(1.0+.4+.3);
        float later=-.4+float(SDF_RELAX_OMEGA)*radius;
        return min(first,max(later-p.x,p.x-(later+.02)));
    }
    if(testIndex==2u) return max(-.2-p.x,p.x-.2);
    if(testIndex==3u) return .2-p.x;
    if(testIndex==4u) return .3-p.x;
    if(testIndex==5u) return uintBitsToFloat(0x7fc00000u);
    return .1-p.x;
}
struct Output {vec4 hit;uvec4 stats;};
layout(std430,set=0,binding=0) writeonly buffer Results {Output results[];};
void main() {
    testIndex=gl_GlobalInvocationID.x;
    vec3 origin=vec3(testIndex==1u || testIndex==6u ? -.4 : 0.0,0,0);
    float limit=testIndex==3u || testIndex==4u ? .2 : 3.0;
    Hit h=trace(origin,vec3(1,0,0),limit);
    results[testIndex].hit=vec4(h.t,float(h.status),float(h.steps),0);
    results[testIndex].stats=guardedStats;
}
