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
Recipe workshopRecipe(int instanceId) {
    return recipes[workshop.options.x==1u?workshop.options.y:instances[instanceId].offsets.z];
}
vec3 workshopAbsorption(Recipe r,float distance) {
    return exp(log(r.attenuation.rgb)*max(distance,0)/r.optics.w);
}
vec3 workshopShadowVisibility(Surface receiver,vec3 light) {
    if(camera.options.x<.5)return vec3(1);
    vec3 origin=offsetOrigin(receiver,light),segment=receiver.p,visibility=vec3(1);
    int medium=-1;
    for(int i=0;i<environment.limits.w;++i) {
        if(totalQueries>=environment.limits.z){status|=1u;return vec3(0);}
        Surface s=surfaceRay(origin,light,255u);++shadowQueries;
        if(s.id<0){if(medium>=0){status|=16u;return vec3(0);}return visibility;}
        if(medium>=0) {
            if(s.id!=medium){status|=16u;return vec3(0);}
            visibility*=workshopAbsorption(workshopRecipe(medium),length(s.p-segment));
        }
        Recipe r=workshopRecipe(s.id);
        if(r.optics.y<.5)return vec3(0);
        bool entering=dot(light,s.outward)<0;
        if(entering!=(medium<0)){status|=16u;return vec3(0);}
        Interface boundary=opticalInterface(light,s.outward,medium>=0,r.optics.z);
        visibility*=1-boundary.fresnel;
        medium=entering?s.id:-1;
        segment=s.p;origin=offsetOrigin(s,light);
    }
    status|=32u;return vec3(0);
}

float solarConeSize() { float s=sin(workshop.solar.x*.5); return 2*s*s; }
vec3 transportSky(vec3 direction,float solarPdf,float solarWeight) {
    if(workshop.solar.w>.5) return vec3(1);
    vec3 visual=textureLod(visualSunComponent,direction,0).rgb*environment.options.w;
    vec3 background=max(sky(direction)-visual,vec3(0))*workshop.solar.y;
    float sine=sin(workshop.solar.x);
    vec3 solar=solarPdf>0?
        environment.directColor.rgb/(CUBEY_PBR_PI*sine*sine):vec3(0);
    return background+solar*workshop.solar.z*solarWeight;
}

uint randomKey;
float rtRoughDiffuse(float nl,float nv,float lv,float roughness);
uint rtHash(uint x) { x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; return x^(x>>16); }
bool rtBsdfProposal(uint sampleIndex,uint pixel,uint depth) {
    // Distinct sample bits provide joint coverage across event depths. Using
    // the same parity at every depth gives correct marginals but biased paths.
    uint scramble=rtHash(pixel^rtHash(workshop.sampling.x+depth*0x9e3779b9u));
    return (((sampleIndex>>depth)^scramble)&1u)==0u;
}
uint rtProposalIndex(uint sampleIndex,uint depth) {
    // Remove this proposal bit so either conditional proposal sees a complete,
    // unique contiguous sampling sequence rather than every other stratum.
    uint lower=(1u<<depth)-1u;
    return (sampleIndex&lower)|((sampleIndex>>(depth+1u))<<depth);
}
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
    float roughness, metallic, diffuseRoughness, objectScale,roughGlass;
    vec4 sheen;
};
RecipeSurface recipeSurface(Surface s,float footprint) {
    Instance inst=instances[s.id];
    Recipe r=workshopRecipe(s.id);
    if(r.optics.y>.5) {
        // The shared recipe validator requires homogeneous plain white glass
        // with no procedural/sheen/emissive layer. Avoid reconstructing and
        // filtering a material domain that cannot affect this optical route.
        float objectScale=1/length(inst.world[0].xyz);
        return RecipeSurface(r.surface.base_roughness.rgb,r.surface.emission.rgb,s.n,
            vec3(cubey_pbr_f0_from_ior(r.optics.z)),r.surface.base_roughness.w,0,0,
            objectScale,r.library.optical_options.x,vec4(0));
    }
    uint start=inst.offsets.y+3*s.primitive;
    vec3 objectPoint=vec3(0);
    for(uint i=0u;i<3u;++i)
        objectPoint+=vertices[indices[start+i]+inst.offsets.x].position.xyz*s.bary[i];
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
    return RecipeSurface(base,value.material.emission.rgb,normal,f0,roughness,metallic,r.attenuation.w,objectScale,value.rough_glass,value.sheen);
}
// Same Oren-Nayar direct/A-only environment approximation as the shared
// workshop consumers. This does not add a sampled diffuse transport lobe.
float rtRoughDiffuse(float nl,float nv,float lv,float roughness) {
    float sigma=roughness*(CUBEY_PBR_PI*.5),s2=sigma*sigma;
    float a=1-.5*s2/(s2+.33),b=.45*s2/(s2+.09);
    return a+b*max(0,lv-nl*nv)/max(max(nl,nv),.001);
}
float workshopCurvature() {
    return workshop.options.x==2u?1/.075:workshop.sampling.z==0u?4:workshop.sampling.z==1u?1/.045:workshop.sampling.z==2u?1/.012:workshop.sampling.z==3u?1/.065:1/.005;
}
struct WorkshopPath {
    vec3 origin,direction,weight,segmentStart;
    float width,spread,solarPdf,solarMis;
    int medium,depth;
    bool environmentOnly,trunk;
    uint sequence;
};
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
    observation[12]=vec4(-1,-1,0,0);
    randomKey=rtHash(linearPixel^rtHash(workshop.sampling.x));
    vec2 uv=(vec2(pixel)+rtStratified(completed,0u))/vec2(gl_LaunchSizeEXT.xy),screen=uv*2-1;
    vec3 origin=camera.eye.xyz;
    vec3 direction=normalize(mat3(camera.inverseView)*vec3(screen.x*camera.eye.w*camera.light.z,-screen.y*camera.light.z,-1));
    vec3 weight=vec3(1),color=vec3(0);
    vec3 diffuseContribution=vec3(0),sheenContribution=vec3(0),emissionContribution=vec3(0);
    float priorSolarPdf=-1;
    float solarMisWeight=1;
    uint terminatedBranches=0u,rejectedBranches=0u;
    uint trunkBoundaries=0u;
    int medium=-1;
    vec3 segmentStart=origin;
    bool environmentOnly=false;
    float width=0,spread=2*camera.light.z/float(gl_LaunchSizeEXT.y);
    int view=int(camera.options.y+.5);
    if(view==10)color=textureLod(visibleBackground,uv,0).rgb*environment.options.w;
    Surface primary=Surface(vec3(0),vec3(0),vec3(0),vec3(0),-1,-1,0u,0u);
    if(view!=10){primary=surfaceRay(origin,direction,255u);++radianceQueries;}
    uint trials=1u;
    if(primary.id>=0) {
        Recipe initial=workshopRecipe(primary.id);
        // Conditional secondary trials share this one jittered primary and
        // all of its original event/radiance/total-call limits. History SPP
        // counts primary iterations, not these variance-reduction trials.
        if(initial.optics.y>.5&&initial.library.optical_options.x>.5&&initial.optics.z!=1)trials=10u;
    }
    bool knownEmptyOutside=workshop.options.x==1u&&workshop.sampling.z<3u;
    WorkshopPath pending[16];int pendingCount=int(trials);
    for(uint trial=0u;trial<trials;++trial)
        pending[trial]=WorkshopPath(origin,direction,weight/float(trials),segmentStart,width,spread,priorSolarPdf,solarMisWeight,medium,0,environmentOnly,true,completed*trials+trial);
    while(pendingCount>0) {
    WorkshopPath path=pending[--pendingCount];
    origin=path.origin;direction=path.direction;weight=path.weight;segmentStart=path.segmentStart;
    width=path.width;spread=path.spread;priorSolarPdf=path.solarPdf;solarMisWeight=path.solarMis;
    medium=path.medium;environmentOnly=path.environmentOnly;
    bool trunk=path.trunk;
    uint sequence=path.sequence;
    for(int depth=path.depth;depth<=environment.limits.x&&view!=10;++depth) {
        if(environmentOnly&&medium<0) {
            color+=weight*transportSky(direction,priorSolarPdf,solarMisWeight);
            break;
        }
        if(depth>0&&(radianceQueries>=environment.limits.y||totalQueries>=environment.limits.z)) {
            status|=radianceQueries>=environment.limits.y?4u:1u; discarded+=weight; ++terminatedBranches; break;
        }
        Surface s=primary;
        if(depth>0){s=surfaceRay(origin,direction,255u);++radianceQueries;}
        if(depth==1&&trunk)observation[8]=vec4(s.p,float(s.id));
        maximumDepth=max(maximumDepth,depth);
        if(medium>=0) {
            if(s.id!=medium){status|=16u;discarded+=weight;++terminatedBranches;break;}
            float distance=length(s.p-segmentStart);
            vec3 absorption=workshopAbsorption(workshopRecipe(medium),distance);
            weight*=absorption;if(trunk)observation[9]=vec4(absorption,distance);
        }
        if(s.id<0) {
            color+=weight*(depth==0?textureLod(visibleBackground,uv,0).rgb*environment.options.w:transportSky(direction,priorSolarPdf,solarMisWeight));
            break;
        }
        width+=s.t*spread;
        float footprint=width/max(abs(dot(direction,s.outward)),.0001);
        RecipeSurface m=recipeSurface(s,footprint);
        Recipe boundaryRecipe=workshopRecipe(s.id);
        if(boundaryRecipe.optics.y>.5) {
            if(trunk)++trunkBoundaries;
            bool entering=dot(direction,s.outward)<0;
            if(entering!=(medium<0)){status|=16u;discarded+=weight;++terminatedBranches;break;}
            vec3 n=faceforward(s.outward,direction,s.outward),v=-direction;
            mat3 frame=rtDirectionFrame(n);
            vec3 localV=transpose(frame)*v;
            float eta=medium>=0?1/boundaryRecipe.optics.z:boundaryRecipe.optics.z;
            bool delta=m.roughGlass<.5||eta==1;
            bool split=depth<2&&eta!=1;
            if(depth==0) {
                Interface b=opticalInterface(direction,s.outward,medium>=0,boundaryRecipe.optics.z);
                observation[0]=vec4(s.t,float(s.id),float(s.primitive),1);
                observation[1]=vec4(n,2);observation[2]=vec4(n,0);observation[3]=vec4(s.bary,0);
                observation[4]=vec4(direction,float(medium));observation[5]=vec4(b.reflected,b.fresnel);
                observation[6]=vec4(b.transmitted,b.tir?1:0);
                observation[13]=vec4(m.base,m.roughness);observation[14]=vec4(0,footprint,0,0);
            }
            color+=weight*m.emission;emissionContribution+=weight*m.emission;
            if(depth>=environment.limits.x){status|=2u;discarded+=weight;++terminatedBranches;break;}
            // Sample the direct solar emitter separately, then always continue
            // the BSDF. Repeating a one-sample mixture through glass multiplies
            // path variance at every boundary. Balance weights apply only to
            // solar misses; smooth sky and intermediate surfaces are not dimmed.
            float sunPdf=1/(2*CUBEY_PBR_PI*solarConeSize());
            vec3 sunAxis=normalize(camera.sun.xyz);
            if(!delta&&workshop.solar.w<.5&&workshop.solar.z>0) {
                vec3 light=rtSampleCone(sunAxis,solarConeSize(),rtStratified(sequence,193u+uint(depth)));
                vec3 localLight=transpose(frame)*light;
                bool lightTransmitted=localLight.z<0;
                int outgoingMedium=lightTransmitted?(entering?s.id:-1):medium;
                bool lightEnvironmentOnly=knownEmptyOutside||environmentOnly||(lightTransmitted?environment.transport.y<.5:environment.transport.x<.5);
                vec2 response=rtDielectricEvalPdf(localV,localLight,m.roughness,eta);
                float proposalPdf=split?rtDielectricComponentPdf(localV,localLight,m.roughness,eta):response.y;
                bool visible=outgoingMedium<0&&response.x>0;
                if(visible&&!lightEnvironmentOnly) {
                    if(totalQueries>=environment.limits.z){status|=1u;visible=false;}
                    else {Surface blocker=surfaceRay(offsetOrigin(s,light),light,255u);++shadowQueries;visible=blocker.id<0;}
                }
                if(visible) {
                    float sine=sin(workshop.solar.x);
                    vec3 radiance=environment.directColor.rgb/(CUBEY_PBR_PI*sine*sine)*workshop.solar.z;
                    color+=weight*response.x*abs(localLight.z)/(sunPdf+proposalPdf)*radiance;
                }
            }
            vec2 xi=rtStratified(sequence,1u+uint(depth)*2u);
            float branch=rtStratified(sequence,97u+uint(depth)).x;
            float curvature=workshopCurvature();
            float angularIncrease=(delta?0:2*m.roughness*m.roughness)+2*width*curvature*m.objectScale;
            RtDielectricSample sampled=rtSampleDielectric(localV,delta?0:m.roughness,eta,vec3(xi,split?1:branch));
            if(split) {
                RtDielectricSample reflected=rtSampleDielectric(localV,delta?0:m.roughness,eta,vec3(xi,0));
                if(!sampled.valid)sampled=reflected;
                else if(reflected.valid&&!reflected.transmitted) {
                    vec3 reflectedDirection=normalize(frame*reflected.direction);
                    float reflectedPdf=delta?0:rtDielectricComponentPdf(localV,reflected.direction,m.roughness,eta);
                    float reflectedWeight=delta?reflected.weight*reflected.pdf:reflected.value*abs(reflected.direction.z)/max(reflectedPdf,1e-30);
                    float solarPdf=rtConePdf(reflectedDirection,sunAxis,solarConeSize());
                    float solarMis=delta||solarPdf<=0?1:reflectedPdf/(reflectedPdf+solarPdf);
                    vec3 residual=weight*reflectedWeight;
                    if(pendingCount>=16){status|=64u;discarded+=residual;++terminatedBranches;}
                    else if(reflectedWeight>0)pending[pendingCount++]=WorkshopPath(offsetOrigin(s,reflectedDirection),reflectedDirection,residual,s.p,width,spread+angularIncrease,solarPdf,solarMis,medium,depth+1,environmentOnly||environment.transport.x<.5||(knownEmptyOutside&&medium<0),false,sequence);
                }
            }
            if(!sampled.valid){++rejectedBranches;break;}
            vec3 next=normalize(frame*sampled.direction);
            bool transmitted=sampled.transmitted;
            vec3 localNext=transpose(frame)*next;
            if(transmitted?dot(next,n)>=0:dot(next,n)<=0){++rejectedBranches;break;}
            priorSolarPdf=rtConePdf(next,sunAxis,solarConeSize());
            float bsdfPdf=delta?0:split?rtDielectricComponentPdf(localV,localNext,m.roughness,eta):rtDielectricEvalPdf(localV,localNext,m.roughness,eta).y;
            solarMisWeight=delta||priorSolarPdf<=0?1:bsdfPdf/(bsdfPdf+priorSolarPdf);
            float continuationWeight=sampled.weight;
            if(split)continuationWeight=delta?sampled.weight*sampled.pdf:sampled.value*abs(sampled.direction.z)/max(rtDielectricComponentPdf(localV,sampled.direction,m.roughness,eta),1e-30);
            weight*=continuationWeight;
            if(sampled.tir){++tirEvents;observation[12].w+=1;}
            if(transmitted) {
                if(entering&&trunk) {
                    if(observation[12].z<.5)observation[12].x=float(s.id);
                    else if(observation[12].z<1.5)observation[12].y=float(s.id);
                    observation[12].z+=1;
                } else if(!entering&&trunk)observation[7]=vec4(s.p,float(s.id));
                medium=entering?s.id:-1;
                environmentOnly=environmentOnly||environment.transport.y<.5;
                spread/=eta;
            } else environmentOnly=environmentOnly||environment.transport.x<.5;
            if(knownEmptyOutside&&medium<0)environmentOnly=true;
            spread+=angularIncrease;
            segmentStart=s.p;origin=offsetOrigin(s,next);direction=next;
            continue;
        }
        vec3 n=m.normal,v=-direction,l=normalize(camera.sun.xyz);
        if(dot(n,v)<=0) n=faceforward(s.outward,direction,s.outward);
        float nv=max(dot(n,v),.001),nl=dot(s.outward,l)*dot(s.outward,v)>0?max(dot(n,l),0):0;
        vec3 sum=v+l;
        vec3 diffuse=m.base*(1-m.metallic),h=dot(sum,sum)>1e-20?normalize(sum):n;
        vec3 f=cubey_pbr_fresnel_schlick(max(dot(v,h),0),m.f0);
        vec3 fv=cubey_pbr_fresnel_schlick(nv,m.f0);
        vec3 visibility=nl>0?workshopShadowVisibility(s,l):vec3(1);
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
        if(depth>=environment.limits.x) {status|=2u;discarded+=weight;++terminatedBranches;break;}
        // Depth-indexed digital strata cover joint proposal combinations.
        // Remove this depth's proposal bit before sampling its direction.
        bool bsdfProposal=rtBsdfProposal(sequence,linearPixel,uint(depth));
        vec2 xi=rtStratified(rtProposalIndex(sequence,uint(depth)),1u+uint(depth)*2u+(bsdfProposal?0u:1u));
        vec3 next=bsdfProposal?frame*rtSampleReflection(localV,m.roughness,xi):
            rtSampleCone(l,solarConeSize(),xi);
        vec3 localNext=transpose(frame)*next;
        if(dot(next,s.outward)*dot(v,s.outward)<=0||localNext.z<=0){++rejectedBranches;break;}
        // A light sample lies on its emitter by construction. Carry the same
        // support/PDF into a sky miss: re-testing a tiny cone after world-space
        // rounding can disagree with the mixture denominator at its boundary.
        priorSolarPdf=bsdfProposal?rtConePdf(next,l,solarConeSize()):1/(2*CUBEY_PBR_PI*solarConeSize());
        solarMisWeight=1;
        float pdf=.5*rtGgxReflectionPdf(localV,localNext,m.roughness)+.5*priorSolarPdf;
        if(pdf<=0){++rejectedBranches;break;}
        float incomingScale=min(viewScale,clamp(1-sheenMaximum*textureLod(brdfLut,vec2(localNext.z,m.sheen.w),0).a,0,1));
        weight*=rtGgxSampledEval(localV,localNext,m.roughness,m.f0)*compensation*localNext.z/pdf*incomingScale;
        if(max(max(weight.r,weight.g),weight.b)<=environment.glass.z) {discarded+=weight;status|=8u;++terminatedBranches;break;}
        float curvature=workshopCurvature();
        spread+=2*m.roughness*m.roughness+2*width*curvature*m.objectScale;
        origin=offsetOrigin(s,next);direction=next;
        environmentOnly=knownEmptyOutside;
    }
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
        observation[1].w=float(trunkBoundaries);
        observation[2].w=float(terminatedBranches);
        observation[3].w=float(rejectedBranches);
        observation[14].zw=vec2(float(trials),status!=0u?1:0);
        observation[10]=vec4(average,float(totalQueries));
        observation[11]=vec4(discarded,float(status));
        observation[15]=vec4(float(radianceQueries),float(maximumDepth),1,float(shadowQueries));
        if(completed>0u) {
            observation[10].w+=queries[linearPixel*16u+10u].w;
            vec4 previous=queries[linearPixel*16u+11u];
            observation[11]=vec4(previous.rgb+discarded,float(uint(previous.w)|status));
            vec4 counts=queries[linearPixel*16u+15u];
            observation[15]+=vec4(counts.x,0,counts.z,counts.w);
            observation[2].w+=queries[linearPixel*16u+2u].w;
            observation[3].w+=queries[linearPixel*16u+3u].w;
            observation[14].zw+=queries[linearPixel*16u+14u].zw;
            observation[15].y=max(observation[15].y,counts.y);
        }
        for(uint i=0u;i<16u;++i)queries[linearPixel*16u+i]=observation[i];
    }
}
