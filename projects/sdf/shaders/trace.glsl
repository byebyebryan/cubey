#ifndef SDF_TRACE_GLSL
#define SDF_TRACE_GLSL
layout(std430,set=0,binding=0) readonly buffer Nodes { uint nodes[]; };
layout(set=0,binding=1) uniform sampler3D atlas;
const float VOXEL = 1.0 / (256.0*7.0);
const float EPSILON = VOXEL*0.2;
// The shader includer supplies atlasAxis (64 in the bounded reference profile).
vec2 rayBox(vec3 origin,vec3 direction,vec3 low,vec3 high) {
    vec3 inv = 1.0 / mix(direction,vec3(1e-20),lessThan(abs(direction),vec3(1e-20)));
    vec3 a = (low-origin)*inv, b = (high-origin)*inv;
    vec3 enter = min(a,b), leave=max(a,b);
    return vec2(max(enter.x,max(enter.y,enter.z)),min(leave.x,min(leave.y,leave.z)));
}
float sampleBrick(vec3 p,ivec3 cell,uint pointer) {
    vec3 local = clamp((p+0.5)*256.0-vec3(cell),0.0,1.0)*7.0;
    return textureLod(atlas,(vec3(brickOrigin(pointer,atlasAxis))+local+0.5)/
        float(atlasAxis*8u),0.0).r;
}
struct Hit { float t; int status; uint steps; ivec3 cell; uint pointer; };
uint lookup(vec3 p,vec3 direction,out ivec3 cell,out uint level) {
    uint header=0u;
    for (level=2u;level<=8u;level+=2u) {
        float dimension=float(1u<<level);
        cell=clamp(ivec3(floor((p+0.5+sign(direction)*1e-6)*dimension)),
                   ivec3(0),ivec3(int(dimension)-1));
        header=nodes[LEVEL[level]+morton(cell)];
        if ((header & SUBDIVIDED)==0u) break;
    }
    return header;
}
Hit trace(vec3 origin,vec3 direction,float limit) {
    Hit hit = Hit(0.0,0,0u,ivec3(0),0u);
    vec2 root = rayBox(origin,direction,vec3(-0.5),vec3(0.5));
    float t = max(root.x,0.0)+1e-6;
    float end = min(root.y,limit);
    if (t > end) return hit;
    bool inside=false;
    if (root.x<=0.0) {
        ivec3 cell; uint level;
        uint header=lookup(origin,direction,cell,level);
        inside=(header & FILLED)!=0u || ((header & SUBDIVIDED)!=0u &&
            sampleBrick(origin,cell,header & POINTER)<-EPSILON);
    }
    for (int outer=0; outer<1024 && t<=end; ++outer) {
        vec3 p = origin+direction*t;
        ivec3 cell=ivec3(0);
        uint level=2u;
        // Choose the forward cell at an exactly represented boundary, even
        // when a nearly parallel ray cannot move that coordinate by one ULP.
        uint header=lookup(p,direction,cell,level);
        ++hit.steps;
        float dimension=float(1u<<min(level,8u));
        vec3 low=vec3(cell)/dimension-0.5;
        float leave = rayBox(origin,direction,low,low+1.0/dimension).y;
        if ((header & SUBDIVIDED)==0u) {
            if ((header & FILLED)!=0u && !inside) {
                hit.t=t; hit.status=1; hit.cell=cell; return hit;
            }
            t=max(t+1e-6,leave+1e-6);
            continue;
        }
        uint pointer=header & POINTER;
        for (int inner=0;inner<128 && t<=min(leave,end);++inner) {
            ++hit.steps;
            float d=sampleBrick(origin+direction*t,cell,pointer);
            if ((inside ? -d : d)<EPSILON) {
                hit.t=t; hit.status=1; hit.cell=cell; hit.pointer=pointer; return hit;
            }
            t+=max(abs(d)-VOXEL*0.866025404,VOXEL*0.1);
        }
        if (t<=min(leave,end)) { hit.status=2; hit.t=t; return hit; }
        // Re-evaluate the hierarchy at the first point beyond this cell.
        t=min(t,leave+1e-6);
    }
    if (t<=end) { hit.status=2; hit.t=t; }
    return hit;
}
vec3 hitNormal(vec3 p,Hit hit) {
    vec3 gradient=vec3(0);
    for (int axis=0;axis<3;++axis) {
        vec3 low=p,high=p;
        low[axis]-=VOXEL*0.5; high[axis]+=VOXEL*0.5;
        low=max(low,vec3(hit.cell)/256.0-0.5);
        high=min(high,vec3(hit.cell+1)/256.0-0.5);
        gradient[axis]=(sampleBrick(high,hit.cell,hit.pointer)-sampleBrick(low,hit.cell,hit.pointer))/
            max(high[axis]-low[axis],1e-8);
    }
    return length(gradient)>1e-6 ? normalize(gradient) : vec3(0,1,0);
}
#endif
