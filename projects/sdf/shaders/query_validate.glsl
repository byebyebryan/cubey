#define main queryNativeMain
#include "sdf_surface.glsl"
#undef main
#include "query_workload_layout.glsl"
layout(std430,set=0,binding=18) readonly buffer QueryWorkload {
    uvec4 queryDimensions;QueryWork queryWork[];
};
void main() {
    queryNativeMain();
    uvec2 pixel=uvec2(gl_FragCoord.xy);
    // Same sample locations as the compute replay, including partial edge tiles.
    uvec2 tile=pixel/uvec2(8u,4u);
    uvec2 selected=min(tile*uvec2(8u,4u)+uvec2(4u,2u),queryDimensions.xy-1u);
    if(any(notEqual(pixel,selected))) return;
    uint cols=(queryDimensions.x+7u)/8u;
    QueryWork q=queryWork[tile.y*cols+tile.x];
    // Same frozen regular-depth (4 EPSILON) and one-degree normal contract.
    // Different shader stages need not produce bit-identical float arithmetic.
    bool bad=q.identity.w!=0x51575231u || q.hit.y!=queryNativeHit.y ||
        abs(q.hit.x-queryNativeHit.x)>4.0*EPSILON ||
        q.stats.x!=queryNativeShadowRays || q.stats.y!=queryNativeSkipped;
    if(queryNativeHit.y==1.0) {
        bad=bad || dot(q.normal.xyz,queryNativeNormal.xyz)<cos(radians(1.0));
    }
    if(bad) color=vec4(1,0,1,1);
}
