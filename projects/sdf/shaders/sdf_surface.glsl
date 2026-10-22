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
#ifdef SDF_AUTHORED
#include "authored.glsl"
#include "authored_material.glsl"
#endif
#include "lighting.glsl"
// Color-only guide for the next cutter's bounding box. Test both box faces so
// rear edges remain visible through empty space, but never through the solid.
float cutterOutline(vec3 origin,vec3 direction,float surfaceT) {
    if (lighting.cutter_style.x<=0.0) return 0.0;
    vec4 q=lighting.cutter.inverse_rotation;
    vec3 localOrigin=origin-lighting.cutter.center_operation.xyz;
    localOrigin+=2.0*cross(q.xyz,cross(q.xyz,localOrigin)+q.w*localOrigin);
    vec3 localDirection=direction+2.0*cross(q.xyz,cross(q.xyz,direction)+q.w*direction);
    vec3 extent=lighting.cutter.extent_rounding.xyz+lighting.cutter.extent_rounding.w;
    vec2 interval=rayBox(localOrigin,localDirection,-extent,extent);
    if (interval.x>interval.y || interval.y<0.0) return 0.0;
    float alpha=0.0;
    for (int i=0;i<2;++i) {
        float t=i==0 ? interval.x : interval.y;
        if (t<0.0 || t>=surfaceT-EPSILON*2.0) continue;
        vec3 world=origin+direction*t;
        if (any(greaterThan(abs(world),vec3(0.499)))) continue;
        vec3 edgeDistance=abs(abs(localOrigin+localDirection*t)-extent);
        // At a face one distance is zero; the second-smallest selects its edges.
        float edge=min(max(edgeDistance.x,edgeDistance.y),
                       min(max(edgeDistance.y,edgeDistance.z),max(edgeDistance.x,edgeDistance.z)));
        alpha=max(alpha,1.0-smoothstep(lighting.cutter_style.y*0.35,lighting.cutter_style.y,edge));
    }
    return alpha*lighting.cutter_style.x;
}
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
        if (camera.options.x>3.5 && camera.options.x<4.5) {
            uint code=morton(hit.cell)*1664525u+1013904223u;
            radiance=object ? vec3(uvec3(code,code>>8,code>>16)&255u)/255.0 : vec3(0.025);
        }
        if (camera.options.x>4.5) {
#ifdef SDF_AUTHORED
            if (object && authoredHeader.x>0u) {
                SceneSurface surface=authoredSurface(p);
                uint code=(surface.part+1u)*1664525u+1013904223u;
                radiance=camera.options.x<5.5 ? vec3(uvec3(code,code>>8,code>>16)&255u)/255.0 :
                    (camera.options.x<6.5 ? surface.material.base_roughness.xyz : vec3(surface.material.base_roughness.w));
            } else radiance=vec3(0.025);
#else
            radiance=vec3(0.025);
#endif
        }
        float viewZ=dot(p-camera.eye.xyz,camera.forward.xyz);
        gl_FragDepth=clamp(100.0/99.99-(1.0/99.99)/viewZ,0.0,1.0);
    } else if (camera.options.x<0.5) radiance=skyRadiance(direction);
    if (camera.options.x<0.5)
        radiance=mix(radiance,vec3(0.06,1.4,2.0),cutterOutline(camera.eye.xyz,direction,t));
    color=vec4(radiance,1);
}
