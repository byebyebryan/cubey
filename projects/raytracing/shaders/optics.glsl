// Exact unpolarized dielectric interface; caller applies radiance weights.
struct Interface { vec3 reflected; vec3 transmitted; float fresnel; bool tir; };
Interface opticalInterface(vec3 incident,vec3 outward,bool inside,float ior) {
    vec3 n=inside?-outward:outward;
    if(ior==1)return Interface(normalize(reflect(incident,n)),normalize(incident),0,false);
    float etaI=inside?ior:1,etaT=inside?1:ior;
    float cosine=clamp(-dot(incident,n),0,1),ratio=etaI/etaT;
    float sine2=ratio*ratio*max(0,1-cosine*cosine);
    bool tir=sine2>=1;
    float transmittedCos=sqrt(max(0,1-sine2));
    float rs=(etaI*cosine-etaT*transmittedCos)/max(etaI*cosine+etaT*transmittedCos,1e-20);
    float rp=(etaT*cosine-etaI*transmittedCos)/max(etaT*cosine+etaI*transmittedCos,1e-20);
    float fresnel=tir?1:(ior==1?0:.5*(rs*rs+rp*rp));
    vec3 transmitted=tir?vec3(0):normalize(ratio*incident+(ratio*cosine-transmittedCos)*n);
    return Interface(normalize(reflect(incident,n)),transmitted,fresnel,tir);
}
vec3 absorption(float distance) {
    return exp(log(environment.attenuation.rgb)*max(distance,0)/environment.glass.y);
}
