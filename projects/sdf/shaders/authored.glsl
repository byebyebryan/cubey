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
    } else if (code==4u) {
        vec2 v=vec2(length(p.xz)-node.parameters.x,abs(p.y)-node.parameters.y);
        d=length(max(v,0.0))+min(max(v.x,v.y),0.0);
    } else if (code==5u) {
        d=length(vec2(length(p.xz)-node.parameters.x,p.y))-node.parameters.y;
    } else if (code==6u) {
        float bottom=node.parameters.x,top=node.parameters.y,h=node.parameters.z;
        vec2 v=vec2(length(p.xz),p.y),a=vec2(bottom,-h),edge=vec2(top-bottom,2.0*h);
        vec2 side=v-a-edge*clamp(dot(v-a,edge)/dot(edge,edge),0.0,1.0);
        float caps=min(length(vec2(max(v.x-bottom,0.0),v.y+h)),
                       length(vec2(max(v.x-top,0.0),v.y-h)));
        float radius=bottom+(top-bottom)*(v.y+h)/(2.0*h);
        bool inside=abs(v.y)<=h && v.x<=radius;
        d=min(caps,length(side))*(inside ? -1.0 : 1.0);
    } else {
        float bottom=node.parameters.x,top=node.parameters.y,height=2.0*node.parameters.z;
        vec2 v=vec2(length(p.xz),p.y+node.parameters.z);
        if (abs(bottom-top)>=height) {
            d=bottom>=top ? length(v)-bottom : length(v-vec2(0.0,height))-top;
        } else {
            float slope=(top-bottom)/height;
            float radial=sqrt(max(0.0,1.0-slope*slope));
            vec2 tangent=vec2(slope,radial),normal=vec2(radial,-slope);
            float along=dot(v,tangent);
            if (along<0.0) d=length(v)-bottom;
            else if (along>height*radial) d=length(v-vec2(0.0,height))-top;
            else d=dot(v,normal)-bottom;
        }
    }
    return d*node.settings.x;
}
float authoredDistance(vec3 p) {
    if (authoredHeader.x==0u) return 1000.0;
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
            else if (code==13u) {
                float h=clamp(0.5+0.5*(a+b)/node.settings.y,0.0,1.0);
                a=mix(-b,a,h)+node.settings.y*h*(1.0-h);
            }
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
