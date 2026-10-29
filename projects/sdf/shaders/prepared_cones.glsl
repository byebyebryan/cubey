// Private derived companion, indexed by original source instruction identity.
// The canonical authored program and independent two-argument evaluator stay intact.
layout(std430,set=0,binding=19) readonly buffer PreparedCones { vec4 preparedCones[]; };
float authoredPreparedPrimitive(vec3 p,SceneInstruction node,uint originalIndex) {
    vec4 c=preparedCones[originalIndex];
    if(uint(node.center_code.w)!=7u || c.w!=1.0) return authoredPrimitive(p,node);
    p-=node.center_code.xyz;
    vec4 q=node.inverse_rotation;
    p+=2.0*cross(q.xyz,cross(q.xyz,p)+q.w*p);
    p/=node.settings.x;
    float bottom=node.parameters.x,top=node.parameters.y,height=2.0*node.parameters.z;
    vec2 v=vec2(length(p.xz),p.y+node.parameters.z);
    vec2 tangent=c.xy,normal=vec2(c.y,-c.x);
    float along=dot(v,tangent),d;
    if(along<0.0) d=length(v)-bottom;
    else if(along>c.z) d=length(v-vec2(0.0,height))-top;
    else d=dot(v,normal)-bottom;
    return d*node.settings.x;
}
