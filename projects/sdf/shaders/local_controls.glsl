#ifndef SDF_LOCAL_CONTROLS
#define SDF_LOCAL_CONTROLS
layout(std430,set=0,binding=11) readonly buffer LocalTools {
    uvec4 localHeader; // count, subtract, base program length, reserved
    vec4 localInfo; // stored-sample enclosure, brush evaluation guard
    SceneInstruction localTools[8];
};
bool localFar(vec3 p,float radius) {
    for(uint i=0u;i<localHeader.x;i++)
        if(authoredPrimitive(p,localTools[i])-radius-localInfo.y<=localInfo.x)return false;
    return true;
}
float localApply(vec3 p,float a) {
    for(uint i=0u;i<localHeader.x;i++) {
        float b=authoredPrimitive(p,localTools[i]);
        if(localHeader.y==0u)b=max(b,boxDistance(p,vec3(.499)));
        a=localHeader.y==0u?min(a,b):max(a,-b);
    }
    return a;
}
#endif
