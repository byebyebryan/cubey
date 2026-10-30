#include "field.glsl"
layout(push_constant) uniform Camera {
    mat4 inverseVP;vec4 eye;vec4 sunlight;vec4 options;vec4 forward;
} camera;
#define atlasAxis uint(camera.eye.w)
#ifdef SDF_SAMPLED
#include "trace.glsl"
#ifdef SDF_CACHED_QUALITY
#include "authored.glsl"
#include "cached_quality.glsl"
#endif
#else
#include "authored.glsl"
#include "selected_trace.glsl"
#endif
#include "quality_layout.glsl"
layout(local_size_x=64) in;
layout(set=0,binding=20) uniform sampler2D queryCamera;
void main() {
    uint id=gl_GlobalInvocationID.x;if(id>=qualityDimensions.w)return;
    uvec2 pixel=uvec2(id%qualityDimensions.x,id/qualityDimensions.x);
    vec2 uv=texelFetch(queryCamera,ivec2(pixel),0).xy;
    vec4 farPoint=camera.inverseVP*vec4(uv*2.0-1.0,1,1);
    vec3 direction=normalize(farPoint.xyz/farPoint.w-camera.eye.xyz);
    Hit hit=trace(camera.eye.xyz,direction,1e6);
#ifdef SDF_CACHED_REFINE
    hit=refineCached(camera.eye.xyz,direction,1e6,hit);
#endif
    vec3 n=hit.status==1 ? hitNormal(camera.eye.xyz+direction*hit.t,hit) : vec3(0,0,1);
#ifdef SDF_CACHED_NORMAL
    if(hit.status==1 && camera.options.w>.5) n=cachedSourceNormal(camera.eye.xyz+direction*hit.t);
#endif
    float depth=hit.status==1 ? hit.t : (hit.status==2 ? -2.0 : -1.0);
    qualityGeometry[id]=uvec2(floatBitsToUint(depth),qualityPack(n));
}
