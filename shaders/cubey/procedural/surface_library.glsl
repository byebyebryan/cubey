#ifndef CUBEY_SURFACE_LIBRARY_GLSL
#define CUBEY_SURFACE_LIBRARY_GLSL
#include "cubey/procedural/surface_material.glsl"
// Matches procedural::SurfaceLibrary. Parameter vectors are named per family;
// none of the legacy MaterialPattern components change their meaning.
struct WoodParameters { float ring_frequency,distortion,grain_frequency,pore_strength; };
struct RustParameters { float coverage,patch_frequency,edge_breakup,pit_strength; };
struct WeaveParameters { float yarn_spacing,yarn_width,irregularity,fiber_strength; };
struct PaperParameters { float fiber_frequency,directionality,fleck_strength,fiber_strength; };
struct SurfaceLibrary {
    vec4 settings, secondary;
    WoodParameters wood; // ring frequency, distortion, grain frequency, pore strength
    RustParameters rust; // coverage, rustPatch frequency, edge breakup, pit strength
    WeaveParameters weave; // yarn spacing, width, irregularity, fiber strength
    PaperParameters paper; // fiber frequency, directionality, fleck strength, fiber strength
    vec4 rust_roughness, sheen, optical_options;
};
const float LIBRARY_PI=3.141592654;
struct LibraryGradient { float value;vec3 gradient; };
LibraryGradient libraryNoise(vec3 p,vec3 frequency,uint seed,float footprint) {
    vec4 n=surfaceNoise(p*frequency,seed);
    float weight=surfaceFade(footprint,max(max(frequency.x,frequency.y),frequency.z));
    return LibraryGradient(n.w*weight,n.xyz*frequency*weight);
}
LibraryGradient librarySmooth(float lo,float hi,LibraryGradient n) {
    float t=clamp((n.value-lo)/(hi-lo),0.0,1.0);
    return LibraryGradient(t*t*(3.0-2.0*t),n.gradient*(6.0*t*(1.0-t)/(hi-lo)));
}
MaterialSample librarySurface(SurfaceMaterial base, SurfaceLibrary s,
                                      vec3 point, float footprint) {
    MaterialSample result=materialSample(base,vec3(0));
    result.sheen = s.sheen;
    result.rough_glass = s.optical_options.x;
    if (s.settings.x == 0) return result;
    uint seed = uint(s.settings.y);
    float scale = s.settings.z, relief = s.settings.w;
    vec3 p = point * scale;
    footprint *= scale;
    LibraryGradient macro = libraryNoise(p, vec3(4), seed, footprint);
    vec3 color = vec3(base.base_roughness);
    float roughness = base.base_roughness.w;
    LibraryGradient height;
    if (s.settings.x == 1) {
        float radius = sqrt(p.x * p.x + p.z * p.z + 1e-8);
        vec3 radial = vec3(p.x, 0, p.z) / radius;
        float phase = 2 * LIBRARY_PI * (radius * s.wood.ring_frequency + s.wood.distortion * macro.value);
        vec3 phase_gradient = 2 * LIBRARY_PI * (radial * s.wood.ring_frequency + s.wood.distortion * macro.gradient);
        float shaped = phase + .55 * sin(phase);
        float band = .5 + .5 * sin(shaped);
        float filterWeight = surfaceFade(footprint, s.wood.ring_frequency * 1.8 + s.wood.distortion * 32);
        LibraryGradient ring=LibraryGradient(mix(.3, band * band * band * band, filterWeight),
            phase_gradient * (2 * band * band * band * cos(shaped) * (1 + .55 * cos(phase)) * filterWeight));
        LibraryGradient fiber = libraryNoise(p, vec3(s.wood.grain_frequency, 3, s.wood.grain_frequency), seed + 1013u, footprint);
        LibraryGradient pores = librarySmooth(.15, .6, libraryNoise(p, vec3(s.wood.grain_frequency * .8, 14, s.wood.grain_frequency * .8), seed + 3001u, footprint));
        float pigment = clamp(.75 * ring.value + .12 * (.5 + .5 * fiber.value) + s.wood.pore_strength * .3 * pores.value, 0.0, 1.0);
        color = mix(color, vec3(s.secondary), pigment);
        roughness += .07 * ring.value + .025 * fiber.value;
        height = LibraryGradient(relief * (.25 * ring.value + .35 * fiber.value - s.wood.pore_strength * pores.value),
                  relief * (.25 * ring.gradient + .35 * fiber.gradient - s.wood.pore_strength * pores.gradient));
    } else if (s.settings.x == 2) {
        LibraryGradient rustPatch = libraryNoise(p, vec3(s.rust.patch_frequency), seed, footprint);
        LibraryGradient edge = libraryNoise(p, vec3(s.rust.patch_frequency * 3), seed + 1013u, footprint);
        LibraryGradient field=LibraryGradient(rustPatch.value + s.rust.edge_breakup * .35 * edge.value,
                                  rustPatch.gradient + s.rust.edge_breakup * .35 * edge.gradient);
        float threshold = .75 - 1.5 * s.rust.coverage;
        float aa = max(.06, footprint * s.rust.patch_frequency * 2);
        LibraryGradient oxide = librarySmooth(threshold - aa, threshold + aa, field);
        if (s.rust.coverage == 0 || s.rust.coverage == 1) oxide = LibraryGradient(s.rust.coverage, vec3(0));
        float filterWeight = surfaceFade(footprint, s.rust.patch_frequency);
        oxide.value = mix(s.rust.coverage, oxide.value, filterWeight);
        oxide.gradient *= filterWeight;
        LibraryGradient pits = libraryNoise(p, vec3(130), seed + 4013u, footprint);
        vec3 oxide_color = vec3(s.secondary) * (1 + .25 * macro.value + .15 * pits.value);
        color = mix(color, oxide_color, oxide.value);
        roughness = mix(s.rust_roughness.x, s.rust_roughness.y, oxide.value) + s.rust_roughness.z * oxide.value * pits.value;
        result.metallic = 1 - oxide.value;
        float crust = .5 + .5 * s.rust.pit_strength * pits.value;
        height = LibraryGradient(relief * oxide.value * crust,
                  relief * (oxide.gradient * crust + oxide.value * .5 * s.rust.pit_strength * pits.gradient));
    } else if (s.settings.x == 3) {
        float frequency = 1 / s.weave.yarn_spacing;
        float u = p.x * frequency, v = p.y * frequency;
        float power = 2 / (s.weave.yarn_width * s.weave.yarn_width);
        float x = .5 + .5 * cos(2 * LIBRARY_PI * u), y = .5 + .5 * cos(2 * LIBRARY_PI * v);
        float a = pow(x, power), b = pow(y, power);
        vec3 ga=vec3(-power * pow(x, power - 1) * LIBRARY_PI * frequency * sin(2 * LIBRARY_PI * u), 0, 0);
        vec3 gb=vec3(0, -power * pow(y, power - 1) * LIBRARY_PI * frequency * sin(2 * LIBRARY_PI * v), 0);
        float over=.5+.5*cos(LIBRARY_PI*u)*cos(LIBRARY_PI*v);
        vec3 go=vec3(-.5*LIBRARY_PI*frequency*sin(LIBRARY_PI*u)*cos(LIBRARY_PI*v),
                    -.5*LIBRARY_PI*frequency*cos(LIBRARY_PI*u)*sin(LIBRARY_PI*v),0);
        float filterWeight = surfaceFade(footprint, frequency);
        float yarn = mix(.4, mix(a, b, over), filterWeight);
        vec3 yarn_gradient = (mix(ga, gb, over) + (b - a) * go) * filterWeight;
        LibraryGradient fiber = libraryNoise(p, vec3(frequency * 8, frequency * 2, 8), seed + 4013u, footprint);
        float variation = 1 + s.weave.irregularity * macro.value;
        color = mix(color * .7, vec3(s.secondary), .25 + .5 * yarn) * variation;
        roughness += .035 * fiber.value;
        height = LibraryGradient(relief * (yarn * variation + .2 * s.weave.fiber_strength * fiber.value),
            relief * (yarn_gradient * variation + yarn * s.weave.irregularity * macro.gradient + .2 * s.weave.fiber_strength * fiber.gradient));
    } else {
        LibraryGradient fiber = libraryNoise(p, vec3(s.paper.fiber_frequency, s.paper.fiber_frequency * (1 - .95 * s.paper.directionality), 12), seed + 1013u, footprint);
        LibraryGradient fleck_noise = libraryNoise(p, vec3(s.paper.fiber_frequency * .15), seed + 3001u, footprint);
        LibraryGradient fleck = librarySmooth(.25, .65, fleck_noise);
        float pigment = clamp(.08 + .12 * s.paper.fiber_strength * fiber.value + s.paper.fleck_strength * fleck.value, 0.0, 1.0);
        color = mix(color, vec3(s.secondary), pigment) * (1 + .035 * macro.value);
        height = LibraryGradient(relief * (s.paper.fiber_strength * fiber.value - s.paper.fleck_strength * fleck.value),
                  relief * (s.paper.fiber_strength * fiber.gradient - s.paper.fleck_strength * fleck.gradient));
    }
    result.material.base_roughness = vec4(clamp(color, vec3(0), vec3(1)), clamp(roughness, .05, 1.0));
    result.height = height.value;
    result.height_gradient = height.gradient * scale;
    return result;
}
MaterialSample librarySurfaceDomain(SurfaceMaterial base,MaterialPattern legacy,SurfaceLibrary library,mat4 domain,vec3 p,float footprint,bool patterns) {
    if(library.settings.x==0.0 || !patterns) {
        MaterialSample result=patterns ? surfacePatternDomain(base,legacy,domain,p,footprint) : materialSample(base,vec3(0));
        result.sheen=library.sheen;result.rough_glass=library.optical_options.x;
        return result;
    }
    mat3 j=mat3(domain);
    float bound=domain==mat4(1) ? 1.0 : sqrt(dot(j[0],j[0])+dot(j[1],j[1])+dot(j[2],j[2]));
    MaterialSample result=librarySurface(base,library,(domain*vec4(p,1)).xyz,footprint*bound);
    result.height_gradient=transpose(j)*result.height_gradient;
    return result;
}
#endif
