// Rough dielectric equations: PBRT 4e, section 9.5 (Dielectric BSDF).
// https://www.pbr-book.org/4ed/Reflection_Models/Dielectric_BSDF
// Cubey implementation: visible GGX normals, correlated Smith, radiance mode.
// Directions point away from the interface; V.z > 0; eta = etaT / etaI.
#ifndef CUBEY_RT_MATERIAL_DIELECTRIC_GLSL
#define CUBEY_RT_MATERIAL_DIELECTRIC_GLSL

float rtDielectricFresnel(float cosine,float eta) {
    if(eta==1) return 0;
    float c=clamp(cosine,0,1);
    // Compensate the sum of squares at the critical angle. Subtracting
    // 1-sin² after two rounded products loses most of the transmitted cosine.
    precise float c2=c*c,e2=eta*eta,sum=c2+e2;
    precise float virtualE=sum-c2;
    precise float correction=(c2-(sum-virtualE))+(e2-virtualE);
    precise float critical=(sum-1)+(fma(c,c,-c2)+fma(eta,eta,-e2)+correction);
    if(critical<=0) return 1;
    float root=sqrt(critical);
    float rs=(c-root)/(c+root),rp=(e2*c-root)/(e2*c+root);
    return .5*(rs*rs+rp*rp);
}
float rtDielectricG2(vec3 v,vec3 l,float alpha) {
    float nv=abs(v.z),nl=abs(l.z),a2=alpha*alpha;
    float denominator=nl*sqrt(nv*nv+a2*dot(v.xy,v.xy))+
                       nv*sqrt(nl*nl+a2*dot(l.xy,l.xy));
    return denominator>0?2*nv*nl/denominator:0;
}
// value and PDF are both zero for a delta distribution; the sample carries
// its discrete mass separately. Rejected microfacets retain their null mass.
vec2 rtDielectricEvalPdf(vec3 v,vec3 l,float roughness,float eta) {
    if(v.z<=0||l.z==0||roughness<=0||eta==1) return vec2(0);
    bool reflection=l.z>0;
    vec3 sum=v+(reflection?1:eta)*l;
    if(dot(sum,sum)<=1e-30) return vec2(0);
    vec3 h=normalize(sum); if(h.z<0)h=-h;
    float vh=dot(v,h),lh=dot(l,h);
    if(vh<=0||lh*l.z<=0||h.z<=0) return vec2(0);
    float alpha=roughness*roughness,d=rtGgxDistribution(h,alpha);
    float g=rtDielectricG2(v,l,alpha);
    float normalPdf=d*rtGgxG1(v,alpha)*vh/v.z;
    if(reflection) {
        float f=rtDielectricFresnel(vh,eta);
        return vec2(d*g*f/(4*v.z*l.z),normalPdf*f/(4*vh));
    }
    float denominator=lh+vh/eta,denominator2=denominator*denominator;
    if(denominator2<=1e-30) return vec2(0);
    float jacobian=abs(lh)/denominator2;
    // At a transmitted direction both interface cosines are available.
    // Evaluate 1-F directly, avoiding a second critical-angle square root
    // and the subtraction of a Fresnel value close to one.
    float ds=vh-eta*lh,dp=eta*vh-lh;
    float transmission=clamp(-2*eta*vh*lh*(1/(ds*ds)+1/(dp*dp)),0,1);
    float value=transmission*d*g*abs(lh*vh/(l.z*v.z*denominator2))/(eta*eta);
    return vec2(value,normalPdf*transmission*jacobian);
}
struct RtDielectricSample {
    vec3 direction;
    float value,pdf,weight;
    bool transmitted,valid,tir,delta;
};
float rtDielectricComponentPdf(vec3 v,vec3 l,float roughness,float eta) {
    if(v.z<=0||l.z==0||roughness<=0||eta==1)return 0;
    bool reflection=l.z>0;
    vec3 sum=v+(reflection?1:eta)*l;
    if(dot(sum,sum)<=1e-30)return 0;
    vec3 h=normalize(sum);if(h.z<0)h=-h;
    float vh=dot(v,h),lh=dot(l,h);
    if(vh<=0||lh*l.z<=0||h.z<=0)return 0;
    float alpha=roughness*roughness;
    float normalPdf=rtGgxDistribution(h,alpha)*rtGgxG1(v,alpha)*vh/v.z;
    float denominator=lh+vh/eta;
    return reflection?normalPdf/(4*vh):normalPdf*abs(lh)/(denominator*denominator);
}
RtDielectricSample rtSampleDielectric(vec3 v,float roughness,float eta,vec3 xi) {
    RtDielectricSample result=RtDielectricSample(vec3(0),0,0,0,false,false,false,roughness<=0||eta==1);
    if(v.z<=0) return result;
    vec3 h=result.delta?vec3(0,0,1):rtVisibleGgxNormal(v,roughness*roughness,xi.xy);
    float cosine=dot(v,h),f=rtDielectricFresnel(cosine,eta);
    result.tir=f>=1;
    result.transmitted=xi.z>=f;
    result.direction=result.transmitted?refract(-v,h,1/eta):reflect(-v,h);
    if(dot(result.direction,result.direction)<=1e-20) return result;
    result.direction=normalize(result.direction);
    if(result.transmitted?result.direction.z>=0:result.direction.z<=0) return result;
    if(result.delta) {
        result.pdf=result.transmitted?1-f:f;
        result.weight=result.transmitted?1/(eta*eta):1;
        result.value=result.pdf*result.weight/abs(result.direction.z);
    } else {
        vec2 response=rtDielectricEvalPdf(v,result.direction,roughness,eta);
        result.value=response.x;result.pdf=response.y;
        if(result.pdf<=0) return result;
        result.weight=result.value*abs(result.direction.z)/result.pdf;
    }
    result.valid=result.pdf>0;
    return result;
}
#endif
