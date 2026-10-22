#ifndef SDF_SURFACE_PATTERN_GLSL
#define SDF_SURFACE_PATTERN_GLSL
#include "cubey/procedural/noise.glsl"
struct SurfaceMaterial { vec4 base_roughness; vec4 emission; };
struct MaterialPattern { vec4 settings,secondary,veins,detail,offset; };
struct MaterialSample { SurfaceMaterial material; vec3 height_gradient; };
#ifndef AUTHORED_PATTERN_BINDING
#define AUTHORED_PATTERN_BINDING 6
#endif
layout(std430,set=0,binding=AUTHORED_PATTERN_BINDING) readonly buffer AuthoredPatterns {
    MaterialPattern authoredPatterns[];
};
// xyz is the analytic derivative of quintic value noise, w is its value.
vec4 surfaceNoise(vec3 p,uint seed) {
    ivec3 c=ivec3(floor(p));vec3 t=p-vec3(c);
    vec3 s=t*t*t*(t*(t*6.0-15.0)+10.0);
    vec3 ds=30.0*t*t*(t-1.0)*(t-1.0);
    float a=cubey_proc_hash01_3d(c,seed)*2.0-1.0;
    float b=cubey_proc_hash01_3d(c+ivec3(1,0,0),seed)*2.0-1.0;
    float d=cubey_proc_hash01_3d(c+ivec3(0,1,0),seed)*2.0-1.0;
    float e=cubey_proc_hash01_3d(c+ivec3(1,1,0),seed)*2.0-1.0;
    float f=cubey_proc_hash01_3d(c+ivec3(0,0,1),seed)*2.0-1.0;
    float g=cubey_proc_hash01_3d(c+ivec3(1,0,1),seed)*2.0-1.0;
    float h=cubey_proc_hash01_3d(c+ivec3(0,1,1),seed)*2.0-1.0;
    float j=cubey_proc_hash01_3d(c+ivec3(1,1,1),seed)*2.0-1.0;
    float x00=mix(a,b,s.x),x10=mix(d,e,s.x),x01=mix(f,g,s.x),x11=mix(h,j,s.x);
    float y0=mix(x00,x10,s.y),y1=mix(x01,x11,s.y);
    return vec4(mix(mix(b-a,e-d,s.y),mix(g-f,j-h,s.y),s.z)*ds.x,
                mix(x10-x00,x11-x01,s.z)*ds.y,(y1-y0)*ds.z,mix(y0,y1,s.z));
}
float surfaceFade(float footprint,float frequency) {
    return 1.0-smoothstep(.15,.65,footprint*frequency);
}
vec4 surfaceFractal(vec3 p,uint seed,float footprint) {
    vec4 result=vec4(0);float frequency=1.0,amplitude=.5;
    for (uint i=0u;i<3u;++i) {
        vec4 n=surfaceNoise(p*frequency,seed+i*1013u);
        result+=vec4(n.xyz*frequency,n.w)*(amplitude*surfaceFade(footprint,frequency)/.875);
        frequency*=2.03;amplitude*=.5;
    }
    return result;
}
MaterialSample surfacePattern(SurfaceMaterial base,MaterialPattern pattern,vec3 point,float footprint) {
    MaterialSample result=MaterialSample(base,vec3(0));
    if (pattern.settings.x==0.0) return result;
    uint seed=uint(pattern.settings.y);vec3 p=point+pattern.offset.xyz;
    float scale=pattern.settings.z;
    vec4 macro=surfaceFractal(p*scale,seed,footprint*scale);
    float mask=.5+.5*macro.w;
    if (pattern.settings.x==2.0) {
        vec3 direction=normalize(pattern.veins.xyz);
        float phase=dot(p*scale,direction)*pattern.veins.w+pattern.detail.w*macro.w;
        vec3 phaseGradient=scale*(direction*pattern.veins.w+pattern.detail.w*macro.xyz);
        vec4 variationNoise=surfaceNoise(p*(scale*2.3)+vec3(3.1,7.2,1.7),seed+811u);
        float variationFade=surfaceFade(footprint,scale*2.3);
        float variation=variationNoise.w*variationFade;
        vec3 variationGradient=variationNoise.xyz*(scale*2.3*variationFade);
        float cloud=.5+.5*variation;
        float width=pattern.detail.z*(.2+2.5*cloud*cloud),coverage=footprint*length(phaseGradient);
        float aa=max(.025,coverage),band=abs(sin(phase));
        float core=1.0-smoothstep(width*.15,width+aa,band);
        float halo=1.0-smoothstep(width,width*5.0+aa,band);
        float secondaryPhase=phase*1.93+variation*2.5;
        float secondaryCoverage=footprint*length(phaseGradient*1.93+variationGradient*2.5);
        float secondaryAA=max(.025,secondaryCoverage);
        float secondary=1.0-smoothstep(width*.06,width*.4+secondaryAA,abs(sin(secondaryPhase)));
        secondary=mix(secondary,width*.146,smoothstep(.7,2.0,secondaryCoverage));
        mask=clamp((.65*core+.18*halo)*(.5+.5*cloud)+.16*secondary+.045*cloud,0.0,1.0);
        mask=mix(mask,clamp(width*.63662+.025,0.0,1.0),smoothstep(.7,2.0,coverage));
    }
    vec3 color=mix(base.base_roughness.xyz,pattern.secondary.xyz,pattern.settings.w*mask);
    result.material.base_roughness=vec4(clamp(color,0.0,1.0),
        clamp(base.base_roughness.w+pattern.secondary.w*macro.w,.05,1.0));
    float grain=pattern.detail.y;
    if (pattern.detail.x>0.0) {
        vec4 n=surfaceNoise(p*grain,seed+3001u),fine=surfaceNoise(p*(grain*2.03),seed+4013u);
        result.height_gradient=pattern.detail.x*grain*(.7*surfaceFade(footprint,grain)*n.xyz+
            .3*2.03*surfaceFade(footprint,grain*2.03)*fine.xyz);
    }
    return result;
}
vec3 materialShadingNormal(vec3 normal,vec3 gradient) {
    if (dot(gradient,gradient)==0.0) return normal;
    vec3 tangent=gradient-normal*dot(normal,gradient);
    tangent*=min(1.0,.65/max(length(tangent),1e-6));
    return normalize(normal-tangent);
}
#endif
