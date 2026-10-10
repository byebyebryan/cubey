#ifndef FLUID_25D_SCENIC_GLSL
#define FLUID_25D_SCENIC_GLSL
#include "fluid_25d_daylight.glsl"
layout(set=1,binding=0,std140) uniform ScenicFrame {
    mat4 inverse_view_projection;
    mat4 shadow_view_projection;
    vec4 light_direction_exposure;
    vec4 light_color_mips;
    vec4 clock_encoding;
    vec4 ground_material;
    vec4 surface_material;
    vec4 water_optics;
    vec4 art_direction;
    vec4 terrain_macro;
    vec4 water_film; // begin_m, end_m, film perceptual roughness, ground mix
    vec4 water_view; // component view, wet-normal blend, ripple slope, wavelength m
    vec4 terrain_surface; // mode, grid width, grid height, reserved
    vec4 environment_mode; // shared fixed daylight enabled, direct sun gain, reserved
    vec4 water_shallow_optics; // absorption boost/end m, coverage fade strength/end m
    vec4 water_agitation; // flow strength, rain strength, applied rain response, wall clock s
    vec4 water_rapids; // artist steep-flow strength, texture period m, reserved
    vec4 water_stream_foam; // strength, patchiness, brightness, reserved; no extra water coverage
} scenic;
layout(set=1,binding=1) uniform samplerCube scenic_environment;
layout(set=1,binding=2) uniform samplerCube scenic_irradiance;
layout(set=1,binding=3) uniform sampler2D scenic_brdf;
layout(set=1,binding=4) uniform sampler2D scenic_detail;
layout(set=1,binding=5) uniform sampler2D scenic_shadow;
layout(set=1,binding=6) uniform sampler2D scenic_opaque;
layout(set=1,binding=7) uniform sampler2D scenic_depth;
layout(set=1,binding=8) uniform sampler2D scenic_composed;
layout(set=1,binding=14,std430) readonly buffer CapturedDaylight {
    vec4 coefficients[10];
} captured_daylight;

vec3 scenic_unproject(vec2 uv, float z) {
    vec4 p = scenic.inverse_view_projection * vec4(uv*2.0-1.0,z,1.0);
    return p.xyz / p.w;
}
vec3 scenic_irradiance_at(vec3 direction) {
    if (scenic.environment_mode.x<0.5) return texture(scenic_irradiance,direction).rgb;
    vec3 n = normalize(direction);
    vec3 irradiance = vec3(0);
    for (int i=0;i<9;++i)
        irradiance += captured_daylight.coefficients[i].rgb*fluid25d_sh_basis(i,n);
    return max(irradiance,vec3(0));
}
vec3 scenic_sun_radiance() {
    return scenic.environment_mode.x>0.5 ? captured_daylight.coefficients[9].rgb*
                                         scenic.environment_mode.y
                                        : scenic.light_color_mips.rgb;
}
float scenic_sun_visibility(vec3 p, vec3 n) {
    vec4 clip = scenic.shadow_view_projection * vec4(p,1.0);
    vec3 q = clip.xyz / clip.w;
    vec2 uv = q.xy*0.5+0.5;
    vec2 gradient=vec2(0);
    if (scenic.environment_mode.x>0.5)
        gradient=fluid25d_receiver_plane_gradient(dFdx(vec3(uv,q.z)),dFdy(vec3(uv,q.z)));
    if (any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1))) || q.z < 0 || q.z > 1)
        return 1.0;
    // Fixed 2x2 PCF, no temporal stochastic sampling. Bias is shadow-only.
    vec2 texel = 1.0/vec2(textureSize(scenic_shadow,0));
    float ndotl = max(dot(n,scenic.light_direction_exposure.xyz),0.0);
    // Receiver slope and the fixed world-extent/depth-range ratio determine
    // the shadow texel footprint. A tiny constant bias alone creates contour
    // acne across native 30 m slopes; this does not bias water/terrain geometry.
    float bias = 0.000025+0.00025*sqrt(max(0.0,1.0-ndotl*ndotl))/max(ndotl,0.10);
    float visibility = 0.0;
    if (scenic.environment_mode.x>0.5) {
        // Continuous four-wide tent, centered on the fractional texel position.
        // Each comparison follows the receiver plane. The small residual bias
        // also covers curvature between this receiver triangle and adjacent
        // terrain triangles; an exact plane-only bias exposes the 30 m facets.
        // This is shadow-only; no water/terrain position is displaced.
        vec2 coordinate=uv/texel-0.5;
        ivec2 origin=ivec2(floor(coordinate));
        vec2 fraction=fract(coordinate);
        ivec2 extent=textureSize(scenic_shadow,0);
        for (int y=-1;y<=2;++y) for (int x=-1;x<=2;++x) {
            ivec2 cell=clamp(origin+ivec2(x,y),ivec2(0),extent-1);
            vec2 sample_uv=(vec2(cell)+0.5)*texel;
            float residual_bias=0.00006+min(0.0002,0.75*dot(abs(gradient),texel));
            float receiver=q.z+dot(gradient,sample_uv-uv)-residual_bias;
            float weight=fluid25d_shadow_tent(float(x)-fraction.x)*
                         fluid25d_shadow_tent(float(y)-fraction.y)/16.0;
            visibility+=receiver<=texelFetch(scenic_shadow,cell,0).r ? weight : 0.0;
        }
        return mix(1.0,visibility,scenic.surface_material.y);
    }
    for (int y=0;y<2;++y) for (int x=0;x<2;++x)
        visibility += q.z-bias <= texture(scenic_shadow,uv+(vec2(x,y)-0.5)*texel).r ? 0.25 : 0.0;
    return mix(1.0,visibility,scenic.surface_material.y);
}
#endif
