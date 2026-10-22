#ifndef SDF_AUTHORED_GLSL
#define SDF_AUTHORED_GLSL
#ifndef AUTHORED_BINDING
#define AUTHORED_BINDING 3
#endif
struct SceneInstruction {
    vec4 center_code;
    vec4 inverse_rotation;
    vec4 parameters;
    vec4 settings;
};
layout(std430,set=0,binding=AUTHORED_BINDING) readonly buffer AuthoredProgram {
    uvec4 authoredHeader;
    SceneInstruction authoredNodes[];
};
float authoredPrimitive(vec3 p,SceneInstruction node) {
    p-=node.center_code.xyz;
    vec4 q=node.inverse_rotation;
    p+=2.0*cross(q.xyz,cross(q.xyz,p)+q.w*p);
    p/=node.settings.x;
    uint code=uint(node.center_code.w);
    float d;
    if (code==1u) d=boxDistance(p,node.parameters.xyz)-node.parameters.w;
    else if (code==2u) d=length(p)-node.parameters.x;
    else if (code==3u) {
        p.y-=clamp(p.y,-node.parameters.y,node.parameters.y);
        d=length(p)-node.parameters.x;
    } else {
        vec2 v=vec2(length(p.xz)-node.parameters.x,abs(p.y)-node.parameters.y);
        d=length(max(v,0.0))+min(max(v.x,v.y),0.0);
    }
    return d*node.settings.x;
}
float authoredDistance(vec3 p) {
    float stack[32];
    int size=0;
    for (uint i=0u;i<authoredHeader.x;++i) {
        SceneInstruction node=authoredNodes[i];
        uint code=uint(node.center_code.w);
        if (code<10u) stack[size++]=authoredPrimitive(p,node);
        else {
            float b=stack[--size],a=stack[size-1];
            if (code==10u) a=min(a,b);
            else if (code==11u) a=max(a,-b);
            else {
                float h=clamp(0.5+0.5*(b-a)/node.settings.y,0.0,1.0);
                a=mix(b,a,h)-node.settings.y*h*(1.0-h);
            }
            stack[size-1]=a;
        }
    }
    return max(stack[0],boxDistance(p,vec3(0.499)));
}
#endif
