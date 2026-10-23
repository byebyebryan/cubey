#ifndef SDF_OPTICS_MATH_GLSL
#define SDF_OPTICS_MATH_GLSL
struct OpticalInterface { vec3 reflected; vec3 transmitted; float fresnel; bool tir; };
OpticalInterface opticalInterface(vec3 incident,vec3 outward,bool inside,float ior) {
    vec3 normal=inside ? -outward : outward;
    float cosine=clamp(-dot(incident,normal),0,1);
    float eta=inside ? ior : 1.0/ior;
    vec3 transmitted=refract(incident,normal,eta);
    bool tir=dot(transmitted,transmitted)<1e-12;
    float f0=pow((ior-1)/(ior+1),2);
    float fresnel=tir ? 1 : f0+(1-f0)*pow(1-cosine,5);
    return OpticalInterface(reflect(incident,normal),transmitted,fresnel,tir);
}
vec3 opticalAbsorption(vec3 transmittance,float referenceDistance,float distance) {
    return exp(log(transmittance)*(distance/referenceDistance));
}
#endif
