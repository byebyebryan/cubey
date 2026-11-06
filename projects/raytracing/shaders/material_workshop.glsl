#include "cubey/procedural/surface_library.glsl"
struct Recipe {
    SurfaceMaterial surface; MaterialPattern pattern; vec4 optics, attenuation;
    mat4 domain; SurfaceLibrary library; uvec4 routing;
};
layout(std430,set=0,binding=13) readonly buffer Recipes { Recipe recipes[]; };
layout(std140,set=0,binding=14) uniform Workshop { uvec4 options; uvec4 sampling; vec4 solar; } workshop;
layout(rgba32f,set=0,binding=15) uniform image2D sampleHistory;
layout(set=0,binding=16) uniform sampler2D visibleBackground;
layout(set=0,binding=17) uniform samplerCube visualSunComponent;

float solarConeSize() { float s=sin(workshop.solar.x*.5); return 2*s*s; }
vec3 transportSky(vec3 direction,float solarPdf) {
    if(workshop.solar.w>.5) return vec3(1);
    vec3 visual=textureLod(visualSunComponent,direction,0).rgb*environment.options.w;
    vec3 background=max(sky(direction)-visual,vec3(0))*workshop.solar.y;
    float sine=sin(workshop.solar.x);
    vec3 solar=solarPdf>0?
        environment.directColor.rgb/(CUBEY_PBR_PI*sine*sine):vec3(0);
    return background+solar*workshop.solar.z;
}

uint randomKey;
uint rtHash(uint x) { x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; return x^(x>>16); }
vec2 rtStratified(uint index,uint dimension) {
    // Each independent block covers all 8x8 cells. An odd affine permutation
    // and jitter scramble the cells per pixel, seed, dimension and block.
    uint batch=index>>6,within=index&63u;
    uint key=rtHash(randomKey^rtHash(dimension+0x68e31da4u)^rtHash(batch+0xb5297a4du));
    uint cell=(within*((key>>6)|1u)+(key&63u))&63u;
    vec2 jitter=vec2(rtHash(key^rtHash(within+0x9e3779b9u))>>8,
                     rtHash(key^rtHash(within+0x85ebca6bu))>>8)*(1.0/16777216.0);
    return clamp((vec2(cell&7u,cell>>3)+jitter)/8,vec2(1.0/16777216.0),vec2(1-1.0/16777216.0));
}
void materialContractProbe() {
    uint row=gl_LaunchIDEXT.y,slot=row%uint(recipes.length());
    Recipe r=recipes[slot];
    float x=(float(gl_LaunchIDEXT.x)+.5)/float(gl_LaunchSizeEXT.x)*2-1;
    vec3 point=vec3(x*.25,sin(x*7.1)*.21,cos(x*5.3)*.19);
    float footprint=.0001*exp2(float(row/uint(recipes.length())));
    MaterialSample value=librarySurfaceDomain(r.surface,r.pattern,r.library,r.domain,point,footprint,workshop.options.z!=0u);
    vec3 n=normalize(vec3(.3,.8,.2));
    vec3 perturbed=workshop.options.w!=0u?materialShadingNormal(n,value.height_gradient):n;
    uint pixel=gl_LaunchIDEXT.x+row*gl_LaunchSizeEXT.x;
    vec4 record[16];for(int i=0;i<16;++i)record[i]=vec4(0);
    record[0]=vec4(point,footprint);record[1]=value.material.base_roughness;
    record[2]=vec4(value.height_gradient,value.metallic<0?r.optics.x:value.metallic);
    record[3]=vec4(value.material.emission.rgb,value.height);
    record[4]=value.sheen;record[5]=vec4(value.rough_glass,r.optics.yzw);
    record[6]=vec4(perturbed,0);record[7]=r.attenuation;record[8]=vec4(r.routing);
    for(uint i=0u;i<16u;++i)queries[pixel*16u+i]=record[i];
    imageStore(outputImage,ivec2(gl_LaunchIDEXT.xy),vec4(value.material.base_roughness.rgb,1));
}
vec3 filteredSky(vec3 direction,float roughness) {
    float lod=roughness*environment.options.y;
    return mix(textureLod(previousEnvironment,direction,lod).rgb,
               textureLod(currentEnvironment,direction,lod).rgb,environment.options.x)*environment.options.w;
}
struct RecipeSurface {
    vec3 base, emission, normal, f0;
    float roughness, metallic;
};
RecipeSurface recipeSurface(Surface s,float footprint) {
    Instance inst=instances[s.id];
    uint start=inst.offsets.y+3*s.primitive;
    vec3 objectPoint=vec3(0);
    for(uint i=0u;i<3u;++i)
        objectPoint+=vertices[indices[start+i]+inst.offsets.x].position.xyz*s.bary[i];
    Recipe r=recipes[workshop.options.y];
    mat3 inverseLinear=mat3(inverse(inst.world));
    float objectScale=inverseLinear==mat3(1)?1:sqrt(dot(inverseLinear[0],inverseLinear[0])+
        dot(inverseLinear[1],inverseLinear[1])+dot(inverseLinear[2],inverseLinear[2]));
    MaterialSample value=librarySurfaceDomain(r.surface,r.pattern,r.library,r.domain,
                                              objectPoint,footprint*objectScale,workshop.options.z!=0u);
    vec3 normal=workshop.options.w!=0u?materialShadingNormal(s.n,mat3(inst.normalWorld)*value.height_gradient):s.n;
    float metallic=value.metallic<0?r.optics.x:value.metallic;
    float roughness=value.material.base_roughness.w;
    vec3 base=value.material.base_roughness.rgb;
    vec3 f0=cubey_pbr_f0(base,metallic,vec3(cubey_pbr_f0_from_ior(r.optics.z)));
    return RecipeSurface(base,value.material.emission.rgb,normal,f0,roughness,metallic);
}
void traceWorkshop() {
    ivec2 pixel=ivec2(gl_LaunchIDEXT.xy);
    uint linearPixel=gl_LaunchIDEXT.x+gl_LaunchIDEXT.y*gl_LaunchSizeEXT.x;
    vec4 history=imageLoad(sampleHistory,pixel);
    uint completed=uint(history.w);
    int mode=int(camera.options.z+.5);
    if(completed>=workshop.sampling.y) {
        imageStore(outputImage,pixel,vec4(history.rgb/max(history.w,1),1));
        return;
    }
    for(int i=0;i<16;++i)observation[i]=vec4(0);
    observation[0]=vec4(-1,-1,0,1);observation[8].w=-3;
    randomKey=rtHash(linearPixel^rtHash(workshop.sampling.x));
    vec2 uv=(vec2(pixel)+rtStratified(completed,0u))/vec2(gl_LaunchSizeEXT.xy),screen=uv*2-1;
    vec3 origin=camera.eye.xyz;
    vec3 direction=normalize(mat3(camera.inverseView)*vec3(screen.x*camera.eye.w*camera.light.z,-screen.y*camera.light.z,-1));
    vec3 weight=vec3(1),color=vec3(0);
    float priorSolarPdf=-1;
    float width=0,spread=2*camera.light.z/float(gl_LaunchSizeEXT.y);
    int view=int(camera.options.y+.5);
    if(view==10)color=textureLod(visibleBackground,uv,0).rgb*environment.options.w;
    for(int depth=0;depth<=environment.limits.x&&view!=10;++depth) {
        if(radianceQueries>=environment.limits.y||totalQueries>=environment.limits.z) {
            status|=radianceQueries>=environment.limits.y?4u:1u; discarded+=weight; break;
        }
        Surface s=surfaceRay(origin,direction,255u); ++radianceQueries;
        if(depth==1)observation[8]=vec4(s.p,float(s.id));
        maximumDepth=depth;
        if(s.id<0) {
            color+=weight*(depth==0?textureLod(visibleBackground,uv,0).rgb*environment.options.w:transportSky(direction,priorSolarPdf));
            break;
        }
        width+=s.t*spread;
        float footprint=width/max(abs(dot(direction,s.outward)),.0001);
        RecipeSurface m=recipeSurface(s,footprint);
        vec3 n=m.normal,v=-direction,l=normalize(camera.sun.xyz);
        if(dot(n,v)<=0) n=faceforward(s.outward,direction,s.outward);
        float nv=max(dot(n,v),.001),nl=dot(s.outward,l)*dot(s.outward,v)>0?max(dot(n,l),0):0;
        vec3 sum=v+l;
        vec3 diffuse=m.base*(1-m.metallic),h=dot(sum,sum)>1e-20?normalize(sum):n;
        vec3 f=cubey_pbr_fresnel_schlick(max(dot(v,h),0),m.f0);
        vec3 fv=cubey_pbr_fresnel_schlick(nv,m.f0);
        vec3 visibility=nl>0?shadowVisibility(s,l):vec3(1);
        vec3 local=m.emission+(1-max(max(f.r,f.g),f.b))*diffuse/CUBEY_PBR_PI*environment.directColor.rgb*nl*visibility*workshop.solar.z;
        local+=(1-max(max(fv.r,fv.g),fv.b))*diffuse*(workshop.solar.w>.5?vec3(1):irradiance(n)/CUBEY_PBR_PI)*workshop.solar.y;
        vec3 dfg=textureLod(brdfLut,vec2(nv,m.roughness),0).rgb;
        vec3 compensation=cubey_pbr_energy_compensation(m.f0,dfg.b);
        if(depth==0) {
            observation[0]=vec4(s.t,float(s.id),float(s.primitive),1);
            observation[1]=vec4(faceforward(s.outward,direction,s.outward),0);
            observation[2]=vec4(n,0);observation[3]=vec4(s.bary,0);
        }
        mat3 frame=rtDirectionFrame(n);
        vec3 localV=transpose(frame)*v,localL=transpose(frame)*l;
        if(environment.transport.x<.5) {
            local+=rtGgxReflectionEval(localV,localL,m.roughness,m.f0)*compensation*environment.directColor.rgb*nl*visibility*workshop.solar.z;
            vec3 reflection=reflect(direction,n);
            local+=(workshop.solar.w>.5?vec3(1):filteredSky(reflection,m.roughness))*workshop.solar.y*cubey_pbr_indirect_specular(m.f0,vec3(1),dfg)*
                cubey_pbr_horizon_specular_occlusion(reflection,faceforward(s.outward,direction,s.outward));
            color+=weight*local;
            break;
        }
        color+=weight*local;
        if(depth>=environment.limits.x) {status|=2u;discarded+=weight;break;}
        // Randomized two-sample strata retain 50/50 marginal probabilities,
        // including odd sample targets, while balancing each completed pair.
        bool bsdfProposal=((completed&1u)^(rtHash(linearPixel^rtHash(workshop.sampling.x+uint(depth)*0x9e3779b9u))&1u))==0u;
        vec2 xi=rtStratified(completed/2u,1u+uint(depth)*2u+(bsdfProposal?0u:1u));
        vec3 next=bsdfProposal?frame*rtSampleReflection(localV,m.roughness,xi):
            rtSampleCone(l,solarConeSize(),xi);
        vec3 localNext=transpose(frame)*next;
        if(dot(next,s.outward)*dot(v,s.outward)<=0||localNext.z<=0) break;
        // A light sample lies on its emitter by construction. Carry the same
        // support/PDF into a sky miss: re-testing a tiny cone after world-space
        // rounding can disagree with the mixture denominator at its boundary.
        priorSolarPdf=bsdfProposal?rtConePdf(next,l,solarConeSize()):1/(2*CUBEY_PBR_PI*solarConeSize());
        float pdf=.5*rtGgxReflectionPdf(localV,localNext,m.roughness)+.5*priorSolarPdf;
        if(pdf<=0) break;
        weight*=rtGgxSampledEval(localV,localNext,m.roughness,m.f0)*compensation*localNext.z/pdf;
        if(max(max(weight.r,weight.g),weight.b)<=environment.glass.z) {discarded+=weight;status|=8u;break;}
        spread+=2*m.roughness*m.roughness+2*width/.25;
        origin=offsetOrigin(s,next);direction=next;
    }
    if(view==1)color=observation[1].rgb*.5+.5;
    if(view==2)color=observation[2].rgb*.5+.5;
    if(view==3)color=vec3(observation[0].x/6);
    if(view==4)color=observation[0].y<0?vec3(0):vec3(uvec3(uint(observation[0].y+1)*1664525u)*uvec3(1,17,113)&255u)/255;
    if(view==5)color=observation[3].rgb;
    if(view==6)color=observation[8].w<0?vec3(0):vec3(uvec3(uint(observation[8].w+1)*1664525u)*uvec3(1,17,113)&255u)/255;
    if(view==7)color=observation[0].y<0?vec3(0):vec3(.15);
    if(view==8)color=vec3(float(maximumDepth)/float(environment.limits.x));
    if(view==9)color=vec3((status&~8u)!=0u?1:0,(status&8u)!=0u?.5:0,0);
    history+=vec4(color,1);
    imageStore(sampleHistory,pixel,history);
    vec3 average=history.rgb/history.w;
    imageStore(outputImage,pixel,vec4(average,1));
    if(mode==3)queries[linearPixel]=vec4(average,float(status|(completed>0u?uint(queries[linearPixel].w):0u)));
    if(mode==1)for(uint i=0u;i<4u;++i)queries[linearPixel*4u+i]=observation[i];
    if(mode==2) {
        observation[10]=vec4(average,float(totalQueries));
        observation[11]=vec4(discarded,float(status));
        observation[15]=vec4(float(radianceQueries),float(maximumDepth),1,float(shadowQueries));
        if(completed>0u) {
            observation[10].w+=queries[linearPixel*16u+10u].w;
            vec4 previous=queries[linearPixel*16u+11u];
            observation[11]=vec4(previous.rgb+discarded,float(uint(previous.w)|status));
            vec4 counts=queries[linearPixel*16u+15u];
            observation[15]+=vec4(counts.x,0,0,counts.w);
            observation[15].y=max(observation[15].y,counts.y);
        }
        for(uint i=0u;i<16u;++i)queries[linearPixel*16u+i]=observation[i];
    }
}
