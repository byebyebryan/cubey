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
float rtRoughDiffuse(float nl,float nv,float lv,float roughness);
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
    float nv=.1+.89*(x*.5+.5),nl=.1+.89*fract(float(row)*.173);
    vec3 v=vec3(sqrt(1-nv*nv),0,nv),l=normalize(vec3(.6*sqrt(1-nl*nl),.8*sqrt(1-nl*nl),nl));
    float nh=normalize(v+l).z,lv=dot(v,l);
    record[9]=vec4(nv,nl,nh,lv);
    record[10]=vec4(rtRoughDiffuse(nl,nv,lv,r.attenuation.w),rtRoughDiffuse(1,1,1,r.attenuation.w),0,0);
    record[11]=vec4(cubey_pbr_sheen_direct(value.sheen.rgb,value.sheen.w,nv,nl,nh),0);
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
    float roughness, metallic, diffuseRoughness, objectScale;
    vec4 sheen;
};
RecipeSurface recipeSurface(Surface s,float footprint) {
    Instance inst=instances[s.id];
    uint start=inst.offsets.y+3*s.primitive;
    vec3 objectPoint=vec3(0);
    for(uint i=0u;i<3u;++i)
        objectPoint+=vertices[indices[start+i]+inst.offsets.x].position.xyz*s.bary[i];
    Recipe r=recipes[workshop.options.y];
    mat3 inverseLinear=mat3(inverse(inst.world));
    // sqrt(||A||_1 ||A||_infinity) bounds the operator norm and is exact
    // for uniform/axis scale, avoiding an unnecessary sqrt(3) blur jump.
    mat3 absolute=mat3(abs(inverseLinear[0]),abs(inverseLinear[1]),abs(inverseLinear[2]));
    vec3 columnSums=vec3(dot(absolute[0],vec3(1)),dot(absolute[1],vec3(1)),dot(absolute[2],vec3(1)));
    vec3 rowSums=absolute*vec3(1);
    float objectScale=sqrt(max(max(columnSums.x,columnSums.y),columnSums.z)*max(max(rowSums.x,rowSums.y),rowSums.z));
    MaterialSample value=librarySurfaceDomain(r.surface,r.pattern,r.library,r.domain,
                                              objectPoint,footprint*objectScale,workshop.options.z!=0u);
    vec3 normal=workshop.options.w!=0u?materialShadingNormal(s.n,mat3(inst.normalWorld)*value.height_gradient):s.n;
    float metallic=value.metallic<0?r.optics.x:value.metallic;
    float roughness=value.material.base_roughness.w;
    vec3 base=value.material.base_roughness.rgb;
    vec3 f0=cubey_pbr_f0(base,metallic,vec3(cubey_pbr_f0_from_ior(r.optics.z)));
    return RecipeSurface(base,value.material.emission.rgb,normal,f0,roughness,metallic,r.attenuation.w,objectScale,value.sheen);
}
// Same Oren-Nayar direct/A-only environment approximation as the shared
// workshop consumers. This does not add a sampled diffuse transport lobe.
float rtRoughDiffuse(float nl,float nv,float lv,float roughness) {
    float sigma=roughness*(CUBEY_PBR_PI*.5),s2=sigma*sigma;
    float a=1-.5*s2/(s2+.33),b=.45*s2/(s2+.09);
    return a+b*max(0,lv-nl*nv)/max(max(nl,nv),.001);
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
    vec3 diffuseContribution=vec3(0),sheenContribution=vec3(0),emissionContribution=vec3(0);
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
        vec3 directDiffuse=(1-max(max(f.r,f.g),f.b))*diffuse/CUBEY_PBR_PI*environment.directColor.rgb*nl*visibility*workshop.solar.z;
        vec3 diffuseEnvironment=(1-max(max(fv.r,fv.g),fv.b))*diffuse*(workshop.solar.w>.5?vec3(1):irradiance(n)/CUBEY_PBR_PI)*workshop.solar.y;
        if(m.diffuseRoughness>0) {
            directDiffuse*=rtRoughDiffuse(nl,nv,dot(l,v),m.diffuseRoughness);
            diffuseEnvironment*=rtRoughDiffuse(1,1,1,m.diffuseRoughness);
        }
        float sheenMaximum=max(max(m.sheen.r,m.sheen.g),m.sheen.b);
        float sheenEnergy=textureLod(brdfLut,vec2(nv,m.sheen.w),0).a;
        float viewScale=clamp(1-sheenMaximum*sheenEnergy,0,1);
        float directScale=min(viewScale,clamp(1-sheenMaximum*textureLod(brdfLut,vec2(nl,m.sheen.w),0).a,0,1));
        vec3 sheenDirect=cubey_pbr_sheen_direct(m.sheen.rgb,m.sheen.w,nv,nl,max(dot(n,h),0))*environment.directColor.rgb*nl*visibility*workshop.solar.z;
        vec3 reflected=reflect(direction,n);
        vec3 sheenEnvironment=(workshop.solar.w>.5?vec3(1):filteredSky(reflected,m.sheen.w))*m.sheen.rgb*sheenEnergy*workshop.solar.y*
            cubey_pbr_horizon_specular_occlusion(reflected,faceforward(s.outward,direction,s.outward));
        vec3 local=m.emission+directDiffuse*directScale+diffuseEnvironment*viewScale+sheenDirect+sheenEnvironment;
        diffuseContribution+=weight*(directDiffuse*directScale+diffuseEnvironment*viewScale);
        sheenContribution+=weight*(sheenDirect+sheenEnvironment);
        emissionContribution+=weight*m.emission;
        vec3 dfg=textureLod(brdfLut,vec2(nv,m.roughness),0).rgb;
        vec3 compensation=cubey_pbr_energy_compensation(m.f0,dfg.b);
        if(depth==0) {
            observation[0]=vec4(s.t,float(s.id),float(s.primitive),1);
            observation[1]=vec4(faceforward(s.outward,direction,s.outward),0);
            observation[2]=vec4(n,0);observation[3]=vec4(s.bary,0);
            observation[13]=vec4(m.base,m.roughness);
            observation[14]=vec4(m.metallic,footprint,sheenMaximum,m.diffuseRoughness);
        }
        mat3 frame=rtDirectionFrame(n);
        vec3 localV=transpose(frame)*v,localL=transpose(frame)*l;
        if(environment.transport.x<.5) {
            local+=rtGgxReflectionEval(localV,localL,m.roughness,m.f0)*compensation*environment.directColor.rgb*nl*visibility*workshop.solar.z*directScale;
            vec3 reflection=reflect(direction,n);
            local+=(workshop.solar.w>.5?vec3(1):filteredSky(reflection,m.roughness))*workshop.solar.y*cubey_pbr_indirect_specular(m.f0,vec3(1),dfg)*
                cubey_pbr_horizon_specular_occlusion(reflection,faceforward(s.outward,direction,s.outward))*viewScale;
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
        float incomingScale=min(viewScale,clamp(1-sheenMaximum*textureLod(brdfLut,vec2(localNext.z,m.sheen.w),0).a,0,1));
        weight*=rtGgxSampledEval(localV,localNext,m.roughness,m.f0)*compensation*localNext.z/pdf*incomingScale;
        if(max(max(weight.r,weight.g),weight.b)<=environment.glass.z) {discarded+=weight;status|=8u;break;}
        float curvature=workshop.sampling.z==0u?4:workshop.sampling.z==1u?1/.045:workshop.sampling.z==2u?1/.012:1/.065;
        spread+=2*m.roughness*m.roughness+2*width*curvature*m.objectScale;
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
    if(view==11)color=observation[13].rgb;
    if(view==12)color=vec3(observation[13].w);
    if(view==13)color=vec3(observation[14].x);
    if(view==14)color=vec3(observation[14].y);
    if(view==15)color=diffuseContribution;
    if(view==16)color=max(color-diffuseContribution-sheenContribution-emissionContribution,vec3(0));
    if(view==17)color=sheenContribution;
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
