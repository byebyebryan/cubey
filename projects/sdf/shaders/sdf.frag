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
void main() {
    vec4 farPoint=camera.inverseVP*vec4(uv*2.0-1.0,1,1);
    vec3 direction=normalize(farPoint.xyz/farPoint.w-camera.eye.xyz);
    float groundT= camera.options.z>0.5 && direction.y < -1e-6 ?
        (-0.36-camera.eye.y)/direction.y : 1e6;
    if (groundT<0.0) groundT=1e6;
    Hit hit=trace(camera.eye.xyz,direction,groundT);
    vec3 radiance=mix(vec3(0.035,0.05,0.075),vec3(0.17,0.23,0.31),
                      clamp(direction.y*0.65+0.5,0.0,1.0));
    gl_FragDepth=1.0;
    if (hit.status==2) { color=vec4(1,0,1,1); return; }
    bool object=hit.status==1;
    float t=object ? hit.t : groundT;
    if (t<1e5) {
        vec3 p=camera.eye.xyz+direction*t;
        vec3 normal=object ? hitNormal(p,hit) : vec3(0,1,0);
        float visibility=1.0;
        if (camera.options.y>0.5) {
            Hit shadow=trace(p+normal*VOXEL*2.0,camera.sunlight.xyz,5.0);
            visibility=shadow.status==0 ? 1.0 : 0.0;
        }
        vec3 base=object ? vec3(0.74,0.35,0.12) : vec3(0.12,0.15,0.19);
        radiance=base*(vec3(0.14,0.19,0.25)*(0.55+0.45*normal.y)+
            vec3(2.4,2.1,1.65)*max(dot(normal,camera.sunlight.xyz),0.0)*visibility);
        if (camera.options.x>0.5 && camera.options.x<1.5) radiance=normal*0.5+0.5;
        if (camera.options.x>1.5 && camera.options.x<2.5) radiance=vec3(t/2.0);
        if (camera.options.x>2.5) radiance=mix(vec3(0.03,0.15,0.3),vec3(1,0.2,0),min(float(hit.steps)/160.0,1.0));
        float viewZ=dot(p-camera.eye.xyz,camera.forward.xyz);
        gl_FragDepth=clamp(100.0/99.99-(1.0/99.99)/viewZ,0.0,1.0);
    }
    // Initial reference display transform; shared HDR post replaces this in M3.
    if (camera.options.x<0.5) radiance=radiance/(radiance+1.0);
    color=vec4(radiance,1);
}
