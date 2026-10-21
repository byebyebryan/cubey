#version 450
#extension GL_GOOGLE_include_directive : require
#include "field.glsl"
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
layout(push_constant) uniform Camera {
    mat4 inverseVP;
    vec4 eye;
    vec4 sunlight;
    vec4 options; // debug mode, shadow mode, ground, raster occlusion test
    vec4 forward;
} camera;
#define atlasAxis uint(camera.eye.w)
#include "trace.glsl"
#include "lighting.glsl"
void main() {
    vec4 farPoint=camera.inverseVP*vec4(uv*2.0-1.0,1,1);
    vec3 direction=normalize(farPoint.xyz/farPoint.w-camera.eye.xyz);
    float groundT= camera.options.z>0.5 && direction.y < -1e-6 ?
        (lighting.quality.w-camera.eye.y)/direction.y : 1e6;
    if (groundT<0.0) groundT=1e6;
    Hit hit=trace(camera.eye.xyz,direction,groundT);
    vec3 radiance=vec3(0.02);
    gl_FragDepth=1.0;
    if (hit.status==2) { color=vec4(1,0,1,1); return; }
    bool object=hit.status==1;
    float t=object ? hit.t : groundT;
    if (t<1e5) {
        vec3 p=camera.eye.xyz+direction*t;
        vec3 normal=object ? hitNormal(p,hit) : vec3(0,1,0);
        bool exhausted=false;
        if (camera.options.x<0.5) radiance=surfaceRadiance(p,normal,-direction,object,exhausted);
        if (exhausted) { color=vec4(1,0,1,1); return; }
        if (camera.options.x>0.5 && camera.options.x<1.5) radiance=normal*0.5+0.5;
        if (camera.options.x>1.5 && camera.options.x<2.5) radiance=vec3(t/2.0);
        if (camera.options.x>2.5 && camera.options.x<3.5) radiance=mix(vec3(0.03,0.15,0.3),vec3(1,0.2,0),min(float(hit.steps)/160.0,1.0));
        if (camera.options.x>3.5) {
            uint code=morton(hit.cell)*1664525u+1013904223u;
            radiance=object ? vec3(uvec3(code,code>>8,code>>16)&255u)/255.0 : vec3(0.025);
        }
        float viewZ=dot(p-camera.eye.xyz,camera.forward.xyz);
        gl_FragDepth=clamp(100.0/99.99-(1.0/99.99)/viewZ,0.0,1.0);
    } else if (camera.options.x<0.5) radiance=skyRadiance(direction);
    color=vec4(radiance,1);
}
