#ifndef SDF_AUTHORED_MATERIAL_GLSL
#define SDF_AUTHORED_MATERIAL_GLSL
#ifndef AUTHORED_MATERIAL_BINDING
#define AUTHORED_MATERIAL_BINDING 4
#endif
struct SurfaceMaterial { vec4 base_roughness; vec4 emission; };
layout(std430,set=0,binding=AUTHORED_MATERIAL_BINDING) readonly buffer AuthoredMaterials {
    SurfaceMaterial authoredMaterials[];
};
struct SceneSurface { float distance; SurfaceMaterial material; uint part; };
SceneSurface authoredSurface(vec3 p) {
    if (authoredHeader.x==0u) return SceneSurface(1000.0,SurfaceMaterial(vec4(0.5),vec4(0)),0u);
    SceneSurface stack[32];
    int size=0;
    for (uint i=0u;i<authoredHeader.x;++i) {
        SceneInstruction node=authoredNodes[i];
        uint code=uint(node.center_code.w);
        if (code<10u) {
            stack[size++]=SceneSurface(authoredPrimitive(p,node),authoredMaterials[uint(node.settings.z)],i);
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
                    if (node.settings.w>=0.0) { a.material=authoredMaterials[uint(node.settings.w)]; a.part=i; }
                }
                a.distance=d;
            } else if (code==13u) {
                float h=clamp(0.5+0.5*(a.distance+b.distance)/node.settings.y,0.0,1.0);
                float d=mix(-b.distance,a.distance,h)+node.settings.y*h*(1.0-h);
                if (h<0.5 && node.settings.w>=0.0) {
                    a.material=authoredMaterials[uint(node.settings.w)]; a.part=i;
                }
                a.distance=d;
            } else {
                float h=clamp(0.5+0.5*(b.distance-a.distance)/node.settings.y,0.0,1.0);
                float d=mix(b.distance,a.distance,h)-node.settings.y*h*(1.0-h);
                if (node.settings.w>0.5) {
                    a.material.base_roughness=mix(b.material.base_roughness,a.material.base_roughness,h);
                    a.material.emission=mix(b.material.emission,a.material.emission,h);
                } else if (h<0.5) a.material=b.material;
                if (h<0.5) a.part=b.part;
                a.distance=d;
            }
            stack[size-1]=a;
        }
    }
    SceneSurface result=stack[0];
    result.distance=max(result.distance,boxDistance(p,vec3(0.499)));
    return result;
}
#endif
