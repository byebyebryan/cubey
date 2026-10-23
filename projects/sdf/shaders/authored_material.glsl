#ifndef SDF_AUTHORED_MATERIAL_GLSL
#define SDF_AUTHORED_MATERIAL_GLSL
#ifndef AUTHORED_MATERIAL_BINDING
#define AUTHORED_MATERIAL_BINDING 4
#endif
#include "surface_pattern.glsl"
layout(std430,set=0,binding=AUTHORED_MATERIAL_BINDING) readonly buffer AuthoredMaterials {
    SurfaceMaterial authoredMaterials[];
};
struct MaterialOptics { vec4 settings,attenuation; };
#ifdef SDF_OPTICS
#ifndef AUTHORED_OPTICS_BINDING
#define AUTHORED_OPTICS_BINDING 7
#endif
layout(std430,set=0,binding=AUTHORED_OPTICS_BINDING) readonly buffer AuthoredOptics { MaterialOptics opticsTable[]; };
#endif
MaterialOptics materialOptics(uint i) {
#ifdef SDF_OPTICS
    return opticsTable[i];
#else
    return MaterialOptics(vec4(0,0,1.5,1),vec4(1,1,1,0));
#endif
}
struct SceneSurface { float distance; SurfaceMaterial material; uint part; vec3 height_gradient; MaterialOptics optics; };
MaterialSample sampleAuthoredMaterial(uint i,vec3 p,float footprint,bool patterns) {
    return patterns ? surfacePattern(authoredMaterials[i],authoredPatterns[i],p,footprint) :
        MaterialSample(authoredMaterials[i],vec3(0));
}
SceneSurface authoredSurface(vec3 p,float footprint,bool patterns) {
    if (authoredHeader.x==0u) return SceneSurface(1000.0,SurfaceMaterial(vec4(0.5),vec4(0)),0u,vec3(0),MaterialOptics(vec4(0,0,1.5,1),vec4(1,1,1,0)));
    MaterialSample materials[16];
    for (uint i=0u;i<authoredHeader.y;++i) materials[i]=sampleAuthoredMaterial(i,p,footprint,patterns);
    SceneSurface stack[32];
    int size=0;
    for (uint i=0u;i<authoredHeader.x;++i) {
        SceneInstruction node=authoredNodes[i];
        uint code=uint(node.center_code.w);
        if (code<10u) {
            MaterialSample m=materials[uint(node.settings.z)];
            stack[size++]=SceneSurface(authoredPrimitive(p,node),m.material,i,m.height_gradient,materialOptics(uint(node.settings.z)));
        } else {
            SceneSurface b=stack[--size],a=stack[size-1];
            if (code==10u) {
                float d=min(a.distance,b.distance);
                if (b.distance<a.distance-1e-6) a=b;
                a.distance=d;
            }
            else if (code==11u) {
                float d=max(a.distance,-b.distance);
                if (-b.distance>a.distance+1e-6) {
                    if (node.settings.w>=0.0) {
                        MaterialSample m=materials[uint(node.settings.w)];
                        a.material=m.material;a.height_gradient=m.height_gradient;a.part=i;a.optics=materialOptics(uint(node.settings.w));
                    }
                }
                a.distance=d;
            } else if (code==13u) {
                float h=clamp(0.5+0.5*(a.distance+b.distance)/node.settings.y,0.0,1.0);
                float d=mix(-b.distance,a.distance,h)+node.settings.y*h*(1.0-h);
                if (-b.distance>a.distance+1e-6 && node.settings.w>=0.0) {
                    MaterialSample m=materials[uint(node.settings.w)];
                    a.material=m.material;a.height_gradient=m.height_gradient;a.part=i;a.optics=materialOptics(uint(node.settings.w));
                }
                a.distance=d;
            } else {
                float h=clamp(0.5+0.5*(b.distance-a.distance)/node.settings.y,0.0,1.0);
                float d=mix(b.distance,a.distance,h)-node.settings.y*h*(1.0-h);
                if (node.settings.w>0.5) {
                    a.material.base_roughness=mix(b.material.base_roughness,a.material.base_roughness,h);
                    a.material.emission=mix(b.material.emission,a.material.emission,h);
                    a.optics.settings=mix(b.optics.settings,a.optics.settings,h);
                    a.optics.attenuation=mix(b.optics.attenuation,a.optics.attenuation,h);
                    a.height_gradient=mix(b.height_gradient,a.height_gradient,h);
                } else if (b.distance<a.distance-1e-6) {
                    a.material=b.material;a.height_gradient=b.height_gradient;a.optics=b.optics;
                }
                if (b.distance<a.distance-1e-6) a.part=b.part;
                a.distance=d;
            }
            stack[size-1]=a;
        }
    }
    SceneSurface result=stack[0];
    result.distance=max(result.distance,boxDistance(p,vec3(0.499)));
    return result;
}
SceneSurface authoredSurface(vec3 p) { return authoredSurface(p,0.0,true); }
MaterialSample authoredShadingMaterial(vec3 p,float footprint,bool patterns,bool fastPath) {
    if (authoredHeader.y==1u && fastPath) return sampleAuthoredMaterial(0u,p,footprint,patterns);
    SceneSurface surface=authoredSurface(p,footprint,patterns);
    return MaterialSample(surface.material,surface.height_gradient);
}
#endif
