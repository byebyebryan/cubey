// Controlled trace status exercises the actual opaque surfaceRadiance function.
// No source field, material evaluator or iteration budget is changed in the app.
layout(local_size_x=1) in;
#include "field.glsl"
const float VOXEL=1.0/1792.0;
struct Hit { float t; int status; };
uint traceCalls=0u;
uint wrongOrigin=0u;
uint testIndex=0u;
vec3 geometricNormal;
const vec3 testPoint=vec3(.1,.2,.3);
Hit trace(vec3 p,vec3 direction,float limit) {
    ++traceCalls;
    if(testIndex<5u && any(notEqual(p,testPoint+geometricNormal*VOXEL*2.0))) ++wrongOrigin;
    return Hit(0.0,2);
}
struct TestCamera { vec4 sunlight; vec4 options; };
TestCamera camera;
#include "lighting.glsl"
struct Output { vec4 radiance; uvec4 observations; };
layout(std430,set=0,binding=0) writeonly buffer Results { Output results[]; };
void main() {
    testIndex=gl_GlobalInvocationID.x;
    geometricNormal=testIndex==0u ? vec3(0,0,-1) : testIndex==1u ? vec3(0,0,1) :
        testIndex==2u ? vec3(1,0,0) : testIndex==3u ? vec3(1,0,1e-8) :
        testIndex==4u ? vec3(1,0,-1e-8) : testIndex==5u ? vec3(uintBitsToFloat(0x7fc00000u),0,0) :
        vec3(uintBitsToFloat(0x7f800000u),0,0);
    camera.sunlight=vec4(0,0,1,0);
    camera.options=vec4(0,1,0,0);
    bool exhausted=false;
    vec3 color=surfaceRadiance(testPoint,geometricNormal,vec3(0,0,1),false,0,exhausted);
    results[testIndex].radiance=vec4(color,1);
    results[testIndex].observations=uvec4(traceCalls,exhausted ? 1u : 0u,wrongOrigin,0);
}
