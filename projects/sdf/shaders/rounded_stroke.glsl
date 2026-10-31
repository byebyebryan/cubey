#ifndef SDF_ROUNDED_STROKE
#define SDF_ROUNDED_STROKE
// Derived from the existing endpoint-sphere hull contract: spherical caps
// joined by their common conical tangent. No base-scene evaluation is needed.
vec3 strokeRotate(vec3 p,vec4 q) {return p+2.0*cross(q.xyz,cross(q.xyz,p)+q.w*p);}
vec2 strokeBallRoots(vec3 p,vec3 d,float r) {
    float a=dot(d,d),b=dot(p,d),c=dot(p,p)-r*r,disc=b*b-a*c;
    float guard=16.0*1.1920929e-7*(b*b+abs(a*c)+1e-30);
    if(disc < -guard)return vec2(1,-1);
    float v=sqrt(max(disc,0.0));
    float q=-b-(b>=0.0?v:-v);
    if(abs(q)<1e-30)return vec2(-b/a);
    vec2 t=vec2(q/a,c/q);return vec2(min(t.x,t.y),max(t.x,t.y));
}
void strokeKeep(inout vec2 interval,float t) {
    if(!isnan(t)&&!isinf(t)){interval.x=min(interval.x,t);interval.y=max(interval.y,t);}
}
vec2 strokeRoundedInterval(vec3 origin,vec3 direction,SceneInstruction tool) {
    vec3 p=strokeRotate(origin-tool.center_code.xyz,tool.inverse_rotation)/tool.settings.x;
    vec3 d=strokeRotate(direction,tool.inverse_rotation);
    bool capsule=tool.center_code.w==3.0;
    float bottom=tool.parameters.x,top=capsule?bottom:tool.parameters.y;
    float h=capsule?tool.parameters.y:tool.parameters.z,H=2.0*h;
    p.y+=h;
    if(abs(bottom-top)>=H) {
        if(top>bottom)p.y-=H;
        vec2 t=strokeBallRoots(p,d,max(bottom,top));
        return t.x>t.y?t:t*tool.settings.x;
    }
    float slope=(top-bottom)/H,a=sqrt(max(0.0,1.0-slope*slope));
    float low=-bottom*slope,high=H-top*slope;
    float guard=8.0*1.1920929e-7*(H+bottom+top+length(p));
    vec2 interval=vec2(1e30,-1e30);
    for(int cap=0;cap<2;++cap) {
        vec3 center=vec3(0,cap==0?0.0:H,0);
        vec2 roots=strokeBallRoots(p-center,d,cap==0?bottom:top);
        if(roots.x>roots.y)continue;
        for(int j=0;j<2;++j) {
            float y=p.y+d.y*roots[j];
            if(cap==0?y<=low+guard:y>=high-guard)strokeKeep(interval,roots[j]);
        }
    }
    float r=bottom+slope*p.y;
    float A=a*a*dot(d.xz,d.xz)-slope*slope*d.y*d.y;
    float B=a*a*dot(p.xz,d.xz)-slope*d.y*r;
    float C=a*a*dot(p.xz,p.xz)-r*r;
    vec2 roots=vec2(1,-1);
    if(abs(A)<1e-8) {
        if(abs(B)>1e-30)roots=vec2(-C/(2.0*B));
    } else {
        float disc=B*B-A*C;
        float error=16.0*1.1920929e-7*(B*B+abs(A*C)+1e-30);
        if(disc>=-error) {
            float v=sqrt(max(disc,0.0)),q=-B-(B>=0.0?v:-v);
            roots=abs(q)<1e-30?vec2(-B/A):vec2(q/A,C/q);
            roots=vec2(min(roots.x,roots.y),max(roots.x,roots.y));
        }
    }
    if(roots.x<=roots.y)for(int j=0;j<2;++j) {
        float y=p.y+d.y*roots[j];
        if(y>=low-guard&&y<=high+guard&&bottom+slope*y>=-guard)
            strokeKeep(interval,roots[j]);
    }
    return interval.x>interval.y?vec2(1,-1):interval*tool.settings.x;
}
vec3 strokeRoundedNormal(vec3 point,SceneInstruction tool) {
    vec3 p=strokeRotate(point-tool.center_code.xyz,tool.inverse_rotation)/tool.settings.x;
    bool capsule=tool.center_code.w==3.0;
    float bottom=tool.parameters.x,top=capsule?bottom:tool.parameters.y;
    float h=capsule?tool.parameters.y:tool.parameters.z,H=2.0*h;
    p.y+=h;vec3 n;
    if(abs(bottom-top)>=H)n=p-vec3(0,top>bottom?H:0.0,0);
    else {
        float slope=(top-bottom)/H,a=sqrt(max(0.0,1.0-slope*slope));
        float radial=length(p.xz),along=radial*slope+p.y*a;
        if(along<0.0)n=p;
        else if(along>H*a)n=p-vec3(0,H,0);
        else n=radial>1e-20?vec3(a*p.x/radial,-slope,a*p.z/radial):vec3(0,1,0);
    }
    n=length(n)>1e-20?normalize(n):vec3(0,1,0);
    return normalize(strokeRotate(n,vec4(-tool.inverse_rotation.xyz,tool.inverse_rotation.w)));
}
#endif
