#ifdef SDF_QUALITY_VALIDATE
layout(std430,set=0,binding=18) readonly buffer QualityGeometry {
#else
layout(std430,set=0,binding=18) buffer QualityGeometry {
#endif
    uvec4 qualityDimensions;
    uvec2 qualityGeometry[];
};
vec2 qualitySign(vec2 v) {return vec2(v.x<0.0 ? -1.0 : 1.0,v.y<0.0 ? -1.0 : 1.0);}
uint qualityPack(vec3 n) {
    n/=max(abs(n.x)+abs(n.y)+abs(n.z),1e-20);
    return packSnorm2x16(n.z>=0.0 ? n.xy : (1.0-abs(n.yx))*qualitySign(n.xy));
}
vec3 qualityUnpack(uint bits) {
    vec2 v=unpackSnorm2x16(bits);
    vec3 n=vec3(v,1.0-abs(v.x)-abs(v.y));
    if(n.z<0.0)n.xy=(1.0-abs(n.yx))*qualitySign(n.xy);
    return normalize(n);
}
