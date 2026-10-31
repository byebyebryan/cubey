// Full original prefix at one center; no regional rewrite of operands.
float localBaseDistance(vec3 p) {
    if(localHeader.z==0u)return 1000.0;
    float stack[32];int size=0;
    for(uint i=0u;i<localHeader.z;i++) {
        SceneInstruction node=authoredNodes[i];uint code=uint(node.center_code.w);
        if(code<10u)stack[size++]=authoredPrimitive(p,node);
        else {
            float b=stack[--size],a=stack[size-1];
            if(code==10u)a=min(a,b);
            else if(code==11u)a=max(a,-b);
            else if(code==13u){float h=clamp(.5+.5*(a+b)/node.settings.y,0.0,1.0);a=mix(-b,a,h)+node.settings.y*h*(1.0-h);}
            else {float h=clamp(.5+.5*(b-a)/node.settings.y,0.0,1.0);a=mix(b,a,h)-node.settings.y*h*(1.0-h);}
            stack[size-1]=a;
        }
    }
    return max(stack[0],boxDistance(p,vec3(.499)));
}
float localToolOperand(vec3 p) {
    if(localHeader.x==0u)return 1000.0;
    float b=authoredPrimitive(p,localTools[0]);
    for(uint i=1u;i<localHeader.x;i++)b=min(b,authoredPrimitive(p,localTools[i]));
    return localHeader.y==0u?max(b,boxDistance(p,vec3(.499))):-b;
}
