// Lit pipelines omit debug branches; the general pipeline retains every view.
#if defined(SDF_PROFILE_DEBUG_ZERO) || defined(SDF_LIT_ONLY)
#define SDF_DEBUG_MODE 0.0
#else
#define SDF_DEBUG_MODE camera.options.x
#endif
#ifdef SDF_FIXED_STAGE
#define SDF_INV_STAGE float(SDF_FIXED_STAGE)
#else
#define SDF_INV_STAGE camera.options.w
#endif
#include "field.glsl"
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
layout(push_constant) uniform Camera {
    mat4 inverseVP;
    vec4 eye;
    vec4 sunlight;
    vec4 options; // debug mode, shadow mode, ground, raster occlusion test
    vec4 forward;
} camera;
#define atlasAxis uint(camera.eye.w)
#if defined(SDF_ANALYTIC) || defined(SDF_HYBRID)
#include "authored.glsl"
#include "selected_trace.glsl"
#else
#include "trace.glsl"
#endif
#ifdef SDF_AUTHORED
#include "authored.glsl"
#include "authored_material.glsl"
#endif
#ifdef SDF_QUERY_VALIDATE
vec4 queryNativeHit=vec4(0),queryNativeNormal=vec4(0);
uint queryNativeShadowRays=0u,queryNativeSkipped=0u;
#endif
#include "lighting.glsl"
const vec3 SUBTRACTION_HIGHLIGHT=vec3(2.0,0.08,0.04);
// Color-only guide for the next cutter's bounding box. Test both box faces so
// rear edges remain visible through empty space, but never through the solid.
float cutterOutline(vec3 origin,vec3 direction,float surfaceT) {
    if (lighting.cutter_style.x<=0.0) return 0.0;
    vec4 q=lighting.cutter.inverse_rotation;
    vec3 localOrigin=origin-lighting.cutter.center_operation.xyz;
    localOrigin+=2.0*cross(q.xyz,cross(q.xyz,localOrigin)+q.w*localOrigin);
    vec3 localDirection=direction+2.0*cross(q.xyz,cross(q.xyz,direction)+q.w*direction);
    vec3 extent=lighting.cutter.extent_rounding.xyz+lighting.cutter.extent_rounding.w;
    vec2 interval=rayBox(localOrigin,localDirection,-extent,extent);
    if (interval.x>interval.y || interval.y<0.0) return 0.0;
    float alpha=0.0;
    for (int i=0;i<2;++i) {
        float t=i==0 ? interval.x : interval.y;
        if (t<0.0 || t>=surfaceT-EPSILON*2.0) continue;
        vec3 world=origin+direction*t;
        if (any(greaterThan(abs(world),vec3(0.499)))) continue;
        vec3 edgeDistance=abs(abs(localOrigin+localDirection*t)-extent);
        // At a face one distance is zero; the second-smallest selects its edges.
        float edge=min(max(edgeDistance.x,edgeDistance.y),
                       min(max(edgeDistance.y,edgeDistance.z),max(edgeDistance.x,edgeDistance.z)));
        alpha=max(alpha,1.0-smoothstep(lighting.cutter_style.y*0.35,lighting.cutter_style.y,edge));
    }
    return alpha*lighting.cutter_style.x;
}
#ifdef SDF_AUTHORED
layout(std430,set=0,binding=5) readonly buffer AuthoredPreviews {
    SceneInstruction previewNodes[];
};
float authoredPrimitivePreview(vec3 origin,vec3 direction,float surfaceT,SceneInstruction node) {
    vec4 q=node.inverse_rotation;
    vec3 localOrigin=origin-node.center_code.xyz;
    localOrigin=(localOrigin+2.0*cross(q.xyz,cross(q.xyz,localOrigin)+q.w*localOrigin))/node.settings.x;
    vec3 localDirection=(direction+2.0*cross(q.xyz,cross(q.xyz,direction)+q.w*direction))/node.settings.x;
    uint code=uint(node.center_code.w);
    vec3 extent=code==1u ? node.parameters.xyz+node.parameters.w :
        (code==2u ? vec3(node.parameters.x) :
        vec3(node.parameters.x,node.parameters.y+(code==3u ? node.parameters.x : 0.0),node.parameters.x));
    if (code==5u) extent=vec3(node.parameters.x+node.parameters.y,node.parameters.y,node.parameters.x+node.parameters.y);
    if (code==6u) extent=vec3(max(node.parameters.x,node.parameters.y),node.parameters.z,max(node.parameters.x,node.parameters.y));
    if (code==7u) {
        float radius=max(node.parameters.x,node.parameters.y);
        extent=vec3(radius,node.parameters.z+radius,radius);
    }
    vec2 interval=rayBox(localOrigin,localDirection,-extent,extent);
    float t=max(interval.x,0.0),end=min(interval.y,surfaceT);
    // A color-only preview, depth limited by the currently published solid.
    // The actual cut is committed by the history scene, never this overlay.
    for (int i=0;i<80 && t<=end;++i) {
        float d=authoredPrimitive(origin+direction*t,node);
        if (d<0.0004) return lighting.preview_style.x;
        t+=max(d,0.0002);
    }
    return 0.0;
}
float authoredCutPreview(vec3 origin,vec3 direction,float surfaceT) {
    if (lighting.preview_style.x<=0.0) return 0.0;
    uint count=min(uint(lighting.preview_style.y),1024u);
    for (uint i=0u;i<count;++i)
        if (authoredPrimitivePreview(origin,direction,surfaceT,previewNodes[i])>0.0)
            return lighting.preview_style.x;
    return 0.0;
}
#endif
void main() {
    vec4 farPoint=camera.inverseVP*vec4(uv*2.0-1.0,1,1);
    vec3 direction=normalize(farPoint.xyz/farPoint.w-camera.eye.xyz);
    float groundT= camera.options.z>0.5 && direction.y < -1e-6 ?
        (lighting.quality.w-camera.eye.y)/direction.y : 1e6;
    if (groundT<0.0) groundT=1e6;
    Hit hit=trace(camera.eye.xyz,direction,groundT);
#ifdef SDF_GUARDED_STEPS
    guardedPrimary=false;
#endif
#ifdef SDF_QUERY_VALIDATE
    queryNativeHit=vec4(hit.t,float(hit.status),float(hit.steps),0);
#endif
    vec3 radiance=vec3(0.02);
    gl_FragDepth=1.0;
    if (hit.status==2) { color=vec4(1,0,1,1); return; }
    bool object=hit.status==1;
#ifdef SDF_INVESTIGATE
    // Private diagnostic pipeline only. The ordinary shader compiles unchanged.
    if(SDF_INV_STAGE>.5 && !object) {color=vec4(.02,.02,.02,1);return;}
    if(SDF_INV_STAGE>.5 && SDF_INV_STAGE<1.5) {
        color=vec4(object ? vec3(hit.t*.25) : vec3(0.02),1);return;
    }
#endif
#ifdef SDF_MATERIAL_STUDY
    if(lighting.transport_options.w>.5 && SDF_DEBUG_MODE<.5 && !object && groundT>=1e5) discard;
#endif
    float t=object ? hit.t : groundT;
    if (t<1e5) {
        vec3 p=camera.eye.xyz+direction*t;
        vec3 normal=object ? hitNormal(p,hit) : vec3(0,1,0);
#ifdef SDF_QUERY_VALIDATE
        queryNativeNormal=vec4(normal,0);
#endif
#ifdef SDF_INVESTIGATE
        if(SDF_INV_STAGE>1.5 && SDF_INV_STAGE<2.5) {
            color=vec4(normal*.5+.5,1);return;
        }
        if(SDF_INV_STAGE>2.5 && SDF_INV_STAGE<3.5) {
            float footprint=t*camera.forward.w/max(abs(dot(normal,-direction)),.2);
            MaterialSample m=authoredShadingMaterial(p,footprint,lighting.surface_options.x>.5,
                                                      lighting.surface_options.z>.5);
            // Keep the normal observable even when the uniform-material shortcut applies.
            color=vec4(m.material.base_roughness.xyz*.3+(normal*.5+.5)*.3+
                m.height_gradient*.01+m.material.emission.xyz*.01+
                vec3(m.material.base_roughness.w+m.metallic)*.05+m.sheen.xyz*.01+vec3(m.rough_glass)*.01,1);return;
        }
        if(SDF_INV_STAGE>3.5 && SDF_INV_STAGE<4.5) {
            bool exhausted=false;
            float visibility=shadowVisibility(p,normal,camera.sunlight.xyz,
                                              int(camera.options.y+.5),exhausted);
            color=exhausted ? vec4(1,0,1,1) : vec4(vec3(visibility)*.5+(normal*.5+.5)*.5,1);
            return;
        }
#endif
        // Conservative projected pixel footprint, avoiding derivatives inside
        // divergent hit/miss control flow. Grain fades at distance/grazing angles.
        float footprint=t*camera.forward.w/max(abs(dot(normal,-direction)),.2);
        bool exhausted=false;
        if (SDF_DEBUG_MODE<0.5) radiance=surfaceRadiance(p,normal,-direction,object,footprint,exhausted);
        if (exhausted) { color=vec4(1,0,1,1); return; }
        if (SDF_DEBUG_MODE>0.5 && SDF_DEBUG_MODE<1.5) radiance=normal*0.5+0.5;
        if (SDF_DEBUG_MODE>1.5 && SDF_DEBUG_MODE<2.5) radiance=vec3(t/2.0);
        if (SDF_DEBUG_MODE>2.5 && SDF_DEBUG_MODE<3.5) radiance=mix(vec3(0.03,0.15,0.3),vec3(1,0.2,0),min(float(hit.steps)/160.0,1.0));
        if (SDF_DEBUG_MODE>3.5 && SDF_DEBUG_MODE<4.5) {
            uint code=morton(hit.cell)*1664525u+1013904223u;
            radiance=object ? vec3(uvec3(code,code>>8,code>>16)&255u)/255.0 : vec3(0.025);
        }
        if (SDF_DEBUG_MODE>4.5 && SDF_DEBUG_MODE<8.5) {
#ifdef SDF_AUTHORED
            if (object && authoredHeader.x>0u) {
                SceneSurface surface=authoredSurface(p,footprint,lighting.surface_options.x>.5);
                uint code=(surface.part+1u)*1664525u+1013904223u;
                radiance=SDF_DEBUG_MODE<5.5 ? vec3(uvec3(code,code>>8,code>>16)&255u)/255.0 :
                    (SDF_DEBUG_MODE<6.5 ? surface.material.base_roughness.xyz :
                     SDF_DEBUG_MODE<7.5 ? vec3(surface.material.base_roughness.w) :
                     materialShadingNormal(normal,lighting.surface_options.y>.5 ? surface.height_gradient : vec3(0))*.5+.5);
            } else radiance=vec3(0.025);
#else
            radiance=vec3(0.025);
#endif
        }
#ifdef SDF_MATERIAL_STUDY
        if(SDF_DEBUG_MODE>8.5 && SDF_DEBUG_MODE<9.5) {
            radiance=vec3(0);
            if(object) {
                bool glassExhausted=false;
                studyRadiance(p,normal,-direction,footprint,glassExhausted);
                if(glassExhausted) { color=vec4(1,0,1,1);return; }
                radiance=glassDiagnostics;
            }
        }
        if(SDF_DEBUG_MODE>9.5 && SDF_DEBUG_MODE<11.5) {
            radiance=studyRadiance(p,normal,-direction,footprint,exhausted);
            if(exhausted) { color=vec4(1,0,1,1);return; }
        }
        if(SDF_DEBUG_MODE>11.5) {
            radiance=vec3(authoredShadingMaterial(p,footprint,lighting.surface_options.x>.5,true).metallic);
        }
#endif
        float viewZ=dot(p-camera.eye.xyz,camera.forward.xyz);
        gl_FragDepth=clamp(100.0/99.99-(1.0/99.99)/viewZ,0.0,1.0);
    } else if (SDF_DEBUG_MODE<0.5) {
#ifdef SDF_PROFILE_NO_SKY
        radiance=vec3(.02);
#else
        radiance=backgroundRadiance(direction);
#endif
    }
    if (SDF_DEBUG_MODE<0.5)
        radiance=mix(radiance,SUBTRACTION_HIGHLIGHT,cutterOutline(camera.eye.xyz,direction,t));
#ifdef SDF_AUTHORED
    if (SDF_DEBUG_MODE<0.5)
        radiance=mix(radiance,SUBTRACTION_HIGHLIGHT,authoredCutPreview(camera.eye.xyz,direction,t));
#endif
    color=vec4(radiance,1);
}
