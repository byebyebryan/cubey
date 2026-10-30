#define SDF_QUALITY_VALIDATE
#define main qualityOriginalMain
#include "sdf_surface.glsl"
#undef main
#include "quality_layout.glsl"
void main() {
    qualityOriginalMain();
    uvec2 pixel=uvec2(gl_FragCoord.xy);
    uvec2 record=qualityGeometry[pixel.y*qualityDimensions.x+pixel.x];
    float depth=uintBitsToFloat(record.x);
    bool badClass=(depth>=0.0)!=(qualityNative.x>=0.0),badDepth=false,badNormal=false;
    if(depth>=0.0 && qualityNative.x>=0.0) {
        badDepth=abs(depth-qualityNative.x)>4.0/(256.0*7.0*32.0);
        badNormal=dot(qualityUnpack(record.y),qualityNative.yzw)<cos(radians(1.0));
    } else badClass=badClass || depth!=qualityNative.x;
    if(badClass || badDepth || badNormal || isnan(depth) || isinf(depth))
        color=vec4(badClass ? 100.0 : 0.0,badDepth ? 100.0 : 0.0,badNormal ? 100.0 : 0.0,1);
}
