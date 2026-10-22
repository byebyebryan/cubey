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
} lighting;
vec3 skyRadiance(vec3 direction) {
    AtmosphereParams a=lighting.atmosphere;
    CubeyAtmosphereMedium medium=CubeyAtmosphereMedium(vec3(0),a.radii_ground.x,a.radii_ground.y,
        a.rayleigh.xyz,a.rayleigh.w,a.mie.x,a.mie.y,a.mie.z,a.mie.w,a.ozone.xyz,a.ozone.w,
        a.atmosphere_options.x,normalize(a.sun_direction_radius.xyz),a.sun_direction_radius.w,
        vec3(CUBEY_ATMOSPHERE_SUN_INTENSITY),0.022);
    vec3 origin=a.camera_position_radius.xyz;
    direction=sky_background_sample_direction(direction,origin,vec3(0));
    CubeyAtmosphereSample sampleValue=cubey_atmosphere_integrate_view(medium,origin,direction,-1.0);
    return sampleValue.color;
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
vec3 surfaceRadiance(vec3 p,vec3 n,vec3 viewDirection,bool object,inout bool exhausted) {
    vec3 lightDirection=camera.sunlight.xyz;
    float visibility=shadowVisibility(p,n,lightDirection,int(camera.options.y+0.5),exhausted);
    float ambientVisibility=environmentVisibility(p,n,exhausted);
    vec3 base=object ? lighting.material.xyz : vec3(0.12,0.14,0.16);
    float roughness=object ? lighting.quality.x : 0.8;
    vec3 emission=vec3(0);
#ifdef SDF_AUTHORED
    if (object && authoredHeader.x>0u && lighting.material.w<0.5) {
        SceneSurface surface=authoredSurface(p);
        base=surface.material.base_roughness.xyz;
        roughness=surface.material.base_roughness.w;
        emission=surface.material.emission.xyz;
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
}
