#include "cubey/atmosphere.glsl"
#include "cubey/pbr.glsl"
#include "cubey/atmosphere/sky_background_direction.glsl"
struct AtmosphereParams {
    vec4 camera_right_aspect, camera_up_tan_half_fovy, camera_forward_debug_view;
    vec4 camera_position_radius, radii_ground, rayleigh, mie, ozone, sun_direction_radius;
    vec4 atmosphere_options, night_options, celestial_options, moon_direction_radius;
    vec4 moon_options, milky_way_options, render_options, celestial_render_options;
};
layout(set=0,binding=2) uniform Lighting {
    AtmosphereParams atmosphere;
    vec4 direct_color;
    vec4 irradiance[9];
    vec4 material;
    vec4 quality;
    Edit cutter;
    vec4 cutter_style;
    vec4 preview_style;
    vec4 surface_options;
    vec4 transport_options;
} lighting;
#ifdef SDF_MATERIAL_STUDY
layout(set=0,binding=8) uniform samplerCube studyDiffuse;
layout(set=0,binding=9) uniform samplerCube studySpecular;
layout(set=0,binding=10) uniform sampler2D studyDfg;
#endif
vec3 skyRadiance(vec3 direction) {
#ifdef SDF_MATERIAL_STUDY
    return lighting.surface_options.w>.5 ? vec3(1) : textureLod(studySpecular,direction,0).rgb;
#else
    AtmosphereParams a=lighting.atmosphere;
    CubeyAtmosphereMedium medium=CubeyAtmosphereMedium(vec3(0),a.radii_ground.x,a.radii_ground.y,
        a.rayleigh.xyz,a.rayleigh.w,a.mie.x,a.mie.y,a.mie.z,a.mie.w,a.ozone.xyz,a.ozone.w,
        a.atmosphere_options.x,normalize(a.sun_direction_radius.xyz),a.sun_direction_radius.w,
        vec3(CUBEY_ATMOSPHERE_SUN_INTENSITY),0.022);
    vec3 origin=a.camera_position_radius.xyz;
    direction=sky_background_sample_direction(direction,origin,vec3(0));
    CubeyAtmosphereSample sampleValue=cubey_atmosphere_integrate_view(medium,origin,direction,-1.0);
    return sampleValue.color;
#endif
}
vec3 environmentIrradiance(vec3 n) {
    float x=n.x,y=n.y,z=n.z;
    float basis[9]=float[9](0.282095,0.488603*y,0.488603*z,0.488603*x,1.092548*x*y,
        1.092548*y*z,0.315392*(3.0*z*z-1.0),1.092548*x*z,0.546274*(x*x-y*y));
    vec3 result=vec3(0);
    for (int i=0;i<9;++i) result+=lighting.irradiance[i].xyz*basis[i];
    return max(result,0.0);
}
float shadowVisibility(vec3 p,vec3 n,vec3 lightDirection,int mode,inout bool exhausted) {
    if (mode==0) return 1.0;
    vec3 right=normalize(cross(lightDirection,abs(lightDirection.y)>0.9 ? vec3(1,0,0) : vec3(0,1,0)));
    vec3 up=cross(right,lightDirection);
    const vec2 disk[4]=vec2[4](vec2(-0.5,-0.5),vec2(0.5,-0.5),vec2(-0.5,0.5),vec2(0.5,0.5));
    float visibility=0;
    int samples=mode==1 ? 1 : 4;
    for (int i=0;i<samples;++i) {
        vec3 direction=mode==1 ? lightDirection : normalize(lightDirection+
            lighting.quality.y*(right*disk[i].x+up*disk[i].y));
        Hit shadow=trace(p+n*VOXEL*2.0,direction,5.0);
        visibility+=shadow.status==0 ? 1.0 : 0.0;
        exhausted=exhausted || shadow.status==2;
    }
    return visibility/float(samples);
}
float environmentVisibility(vec3 p,vec3 n,inout bool exhausted) {
    if (lighting.quality.z<0.5) return 1.0;
    vec3 right=normalize(cross(n,abs(n.y)>0.9 ? vec3(1,0,0) : vec3(0,1,0)));
    vec3 up=cross(right,n);
    float visibility=0;
    for (int i=0;i<4;++i) {
        float angle=1.570796327*float(i)+0.785398163;
        vec3 direction=normalize(n+0.8*(right*cos(angle)+up*sin(angle)));
        Hit ao=trace(p+n*VOXEL*2.0,direction,0.04);
        visibility+=ao.status==0 ? 1.0 : smoothstep(0.0,0.04,ao.t);
        exhausted=exhausted || ao.status==2;
    }
    return visibility*0.25;
}
#ifdef SDF_MATERIAL_STUDY
#include "optics_math.glsl"
vec3 glassDiagnostics=vec3(0);
vec3 glassRadiance(vec3 point,vec3 outward,vec3 direction,MaterialOptics optics,inout bool exhausted) {
    vec3 result=vec3(0),throughput=vec3(1);
    bool inside=dot(direction,outward)>0;
    glassDiagnostics=vec3(0,0,.25);
    if(inside) throughput*=opticalAbsorption(optics.attenuation.rgb,optics.settings.w,length(point-camera.eye.xyz));
    int budget=clamp(int(lighting.transport_options.x+.5),4,16);
    for(int crossing=0;crossing<16;++crossing) {
        OpticalInterface face=opticalInterface(direction,outward,inside,optics.settings.z);
        if(face.tir) {
            glassDiagnostics.y=1;
            return result+throughput*skyRadiance(face.reflected);
        }
        result+=throughput*face.fresnel*skyRadiance(face.reflected);
        throughput*=1-face.fresnel;
        direction=normalize(face.transmitted);
        inside=!inside;
        vec3 origin=point+outward*(inside ? -VOXEL : VOXEL);
        Hit next=trace(origin,direction,5);
        if(next.status==2) { exhausted=true;return vec3(0); }
        if(next.status==0) return result+throughput*skyRadiance(direction);
        vec3 nextPoint=origin+direction*next.t;
        if(inside) throughput*=opticalAbsorption(optics.attenuation.rgb,optics.settings.w,length(nextPoint-point));
        if(crossing==budget-1) {
            glassDiagnostics.x=1;
            return result+throughput*skyRadiance(direction);
        }
        // The next trace starts at an offset origin. A small local t can be
        // a valid adjacent interface; test world-space progress instead.
        if(length(nextPoint-point)<EPSILON*.5 || any(isnan(nextPoint)) || any(isinf(nextPoint))) {
            exhausted=true;return vec3(0);
        }
        point=nextPoint;outward=hitNormal(point,next);
    }
    return result;
}
vec3 studyRadiance(vec3 p,vec3 geometricNormal,vec3 viewDirection,float footprint,inout bool exhausted) {
    MaterialSample sampleValue=authoredShadingMaterial(p,footprint,lighting.surface_options.x>.5,lighting.surface_options.z>.5);
    MaterialOptics optics=authoredHeader.y==1u ? materialOptics(0u) : authoredSurface(p,footprint,lighting.surface_options.x>.5).optics;
    if(optics.settings.y>.5 && lighting.material.w<.5)
        return glassRadiance(p,geometricNormal,-viewDirection,optics,exhausted);
    vec3 base=sampleValue.material.base_roughness.rgb;
    float roughness=sampleValue.material.base_roughness.a;
    vec3 n=lighting.surface_options.y>.5 ? materialShadingNormal(geometricNormal,sampleValue.height_gradient) : geometricNormal;
    if(lighting.material.w>.5) { base=lighting.material.rgb;roughness=lighting.quality.x;n=geometricNormal;optics=MaterialOptics(vec4(0,0,1.5,1),vec4(1,1,1,0)); }
    float metallic=optics.settings.x,ior=optics.settings.z;
    float dielectric=pow((ior-1)/(ior+1),2);
    vec3 f0=mix(vec3(dielectric),base,metallic);
    vec3 l=camera.sunlight.xyz,halfVector=l+viewDirection;
    vec3 h=dot(halfVector,halfVector)>1e-8 ? normalize(halfVector) : n;
    float nl=max(dot(n,l),0),nv=max(dot(n,viewDirection),.001);
    vec3 dfg=texture(studyDfg,vec2(nv,roughness)).rgb;
    vec3 compensation=cubey_pbr_energy_compensation(f0,dfg.b);
    vec3 f=cubey_pbr_fresnel_schlick(max(dot(h,viewDirection),0),f0);
    float d=cubey_pbr_distribution_ggx(max(dot(n,h),0),roughness);
    float v=cubey_pbr_visibility_smith_ggx_correlated(nv,nl,roughness);
    float shadow=shadowVisibility(p,geometricNormal,l,int(camera.options.y+.5),exhausted);
    vec3 diffuse=base*(1-metallic);
    vec3 direct=((1-f)*diffuse/3.141592654+f*d*v*compensation)*lighting.direct_color.rgb*nl*shadow;
    vec3 fv=cubey_pbr_fresnel_schlick(nv,f0);
    float visibility=environmentVisibility(p,geometricNormal,exhausted);
    vec3 indirect=(1-fv)*diffuse*(lighting.surface_options.w>.5 ? vec3(1) : texture(studyDiffuse,n).rgb)*visibility;
    float diffuseFactor=1;
    if(optics.attenuation.w>0) {
        diffuseFactor=roughDiffuseFactor(nl,nv,dot(l,viewDirection),optics.attenuation.w);
        direct=((1-f)*diffuse*diffuseFactor/3.141592654+f*d*v*compensation)*lighting.direct_color.rgb*nl*shadow;
        // Existing irradiance cube has no angular rough-diffuse convolution.
        // Retain a conservative A-only approximation for the environment lobe.
        indirect*=roughDiffuseFactor(1,1,1,optics.attenuation.w);
    }
    vec3 reflected=lighting.surface_options.w>.5 ? vec3(1) : textureLod(studySpecular,reflect(-viewDirection,n),roughness*4).rgb;
    // Opaque component views retain the normal exposure/tone map. They expose
    // the actual lobes; they do not change the material or the combined path.
    if(camera.options.x>10.5)
        return f*d*v*compensation*lighting.direct_color.rgb*nl*shadow+
            reflected*(f0*dfg.x+dfg.y)*compensation;
    if(camera.options.x>9.5)
        return (1-f)*diffuse*diffuseFactor/3.141592654*lighting.direct_color.rgb*nl*shadow+indirect;
    vec3 result=direct+indirect+reflected*(f0*dfg.x+dfg.y)*compensation+sampleValue.material.emission.rgb;
    // Furnace mode is a diagnostic: flag excess energy before HDR output clips it.
    if (lighting.surface_options.w>.5 && any(greaterThan(result,vec3(1.02)))) return vec3(20,0,20);
    return result;
}
#endif
vec3 surfaceRadiance(vec3 p,vec3 n,vec3 viewDirection,bool object,float footprint,inout bool exhausted) {
#ifdef SDF_MATERIAL_STUDY
    return studyRadiance(p,n,viewDirection,footprint,exhausted);
#else
    vec3 lightDirection=camera.sunlight.xyz;
    float visibility=shadowVisibility(p,n,lightDirection,int(camera.options.y+0.5),exhausted);
    float ambientVisibility=environmentVisibility(p,n,exhausted);
    vec3 base=object ? lighting.material.xyz : vec3(0.12,0.14,0.16);
    float roughness=object ? lighting.quality.x : 0.8;
    vec3 emission=vec3(0);
#ifdef SDF_AUTHORED
    if (object && authoredHeader.x>0u && lighting.material.w<0.5) {
        MaterialSample surface=authoredShadingMaterial(p,footprint,lighting.surface_options.x>.5,
                                                       lighting.surface_options.z>.5);
        base=surface.material.base_roughness.xyz;
        roughness=surface.material.base_roughness.w;
        emission=surface.material.emission.xyz;
        // Trace origins and AO above use the geometric normal, never bump.
        if (lighting.surface_options.y>.5) n=materialShadingNormal(n,surface.height_gradient);
    }
#endif
    vec3 halfVector=lightDirection+viewDirection;
    vec3 h=dot(halfVector,halfVector)>1e-8 ? normalize(halfVector) : n;
    float nl=max(dot(n,lightDirection),0.0),nv=max(dot(n,viewDirection),0.001);
    vec3 f=cubey_pbr_fresnel_schlick(max(dot(h,viewDirection),0.0),vec3(0.04));
    float d=cubey_pbr_distribution_ggx(max(dot(n,h),0.0),roughness);
    float v=cubey_pbr_visibility_smith_ggx_correlated(nv,nl,roughness);
    vec3 direct=((1.0-f)*base/3.141592654+f*d*v)*lighting.direct_color.xyz*nl*visibility;
    vec3 ambient=base*environmentIrradiance(n)/3.141592654*ambientVisibility;
    return direct+ambient+emission;
#endif
}
