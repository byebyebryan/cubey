#ifndef FLUID_25D_SCENIC_GLSL
#define FLUID_25D_SCENIC_GLSL
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
    vec4 environment_mode; // shared fixed daylight enabled, reserved
    vec4 diffuse_irradiance_sh[9];
} scenic;
layout(set=1,binding=1) uniform samplerCube scenic_environment;
layout(set=1,binding=2) uniform samplerCube scenic_irradiance;
layout(set=1,binding=3) uniform sampler2D scenic_brdf;
layout(set=1,binding=4) uniform sampler2D scenic_detail;
layout(set=1,binding=5) uniform sampler2D scenic_shadow;
layout(set=1,binding=6) uniform sampler2D scenic_opaque;
layout(set=1,binding=7) uniform sampler2D scenic_depth;
layout(set=1,binding=8) uniform sampler2D scenic_composed;

vec3 scenic_unproject(vec2 uv, float z) {
    vec4 p = scenic.inverse_view_projection * vec4(uv*2.0-1.0,z,1.0);
    return p.xyz / p.w;
}
vec3 scenic_irradiance_at(vec3 direction) {
    if (scenic.environment_mode.x<0.5) return texture(scenic_irradiance,direction).rgb;
    // Same SH order and basis as Cubey terrain/forward PBR and atmosphere CPU.
    vec3 n = normalize(direction);
    float x=n.x,y=n.y,z=n.z;
    float basis[9] = float[9](0.282095,0.488603*y,0.488603*z,0.488603*x,
        1.092548*x*y,1.092548*y*z,0.315392*(3.0*z*z-1.0),
        1.092548*x*z,0.546274*(x*x-y*y));
    vec3 irradiance = vec3(0);
    for (int i=0;i<9;++i) irradiance += scenic.diffuse_irradiance_sh[i].rgb*basis[i];
    return max(irradiance,vec3(0));
}
float scenic_sun_visibility(vec3 p, vec3 n) {
    vec4 clip = scenic.shadow_view_projection * vec4(p,1.0);
    vec3 q = clip.xyz / clip.w;
    vec2 uv = q.xy*0.5+0.5;
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
    for (int y=0;y<2;++y) for (int x=0;x<2;++x)
        visibility += q.z-bias <= texture(scenic_shadow,uv+(vec2(x,y)-0.5)*texel).r ? 0.25 : 0.0;
    return mix(1.0,visibility,scenic.surface_material.y);
}
#endif
