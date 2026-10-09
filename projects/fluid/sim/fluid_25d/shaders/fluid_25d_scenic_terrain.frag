#version 450
#extension GL_GOOGLE_include_directive : require
#include "cubey/terrain/terrain_lighting.glsl"
#define FLUID25D_TERRAIN_MACRO
#include "fluid_25d_scenic.glsl"
#include "fluid_25d_surface_sampling.glsl"
#include "fluid_25d_surface_gradient.glsl"
layout(set=0,binding=1,std430) readonly buffer Depth { float values[]; } depth;
layout(push_constant) uniform Params {
    mat4 view_projection; vec4 grid_cell; vec4 camera_wet; vec4 presentation; vec4 terrain_palette;
} params;
layout(location=0) in vec3 world_position;
layout(location=1) in vec3 world_normal;
layout(location=2) in vec2 world_xz;
layout(location=0) out vec4 out_color;
layout(set=1,binding=10) uniform samplerCube terrain_diffuse_irradiance;
layout(set=1,binding=11) uniform sampler2D terrain_landform_surface;
layout(set=1,binding=12) uniform sampler2D terrain_climate_surface;
layout(set=1,binding=13) uniform sampler2D terrain_correlated_surface;
vec3 terrain_linear(vec3 c) {
    return mix(pow((c+0.055)/1.055,vec3(2.4)),c/12.92,lessThanEqual(c,vec3(0.04045)));
}
vec3 terrain_soft_irradiance(vec3 n) {
    // A bounded terrain-only presentation filter, not a new environment asset
    // or a calibrated cosine convolution. Suppresses the generated narrow key.
    vec3 t = normalize(cross(n,abs(n.y)<0.95 ? vec3(0,1,0) : vec3(1,0,0)));
    vec3 b = cross(n,t);
    vec3 sum = texture(scenic_irradiance,n).rgb;
    sum += texture(scenic_irradiance,normalize(n+0.65*t)).rgb;
    sum += texture(scenic_irradiance,normalize(n-0.65*t)).rgb;
    sum += texture(scenic_irradiance,normalize(n+0.65*b)).rgb;
    sum += texture(scenic_irradiance,normalize(n-0.65*b)).rgb;
    return sum*0.2;
}
void main() {
    int terrain_view = int(round(params.terrain_palette.x));
    vec3 n = normalize(world_normal);
    if (terrain_view == 11) {
        vec3 face = normalize(cross(dFdx(world_position),dFdy(world_position)));
        n = dot(face,n)<0.0 ? -face : face;
    }
    vec3 base_normal = n;
    vec3 weights = pow(abs(n),vec3(4.0)); weights /= max(dot(weights,vec3(1)),0.00001);
    // Cubey's existing mipmapped material detail, triplanar to avoid steep-slope stretch.
    vec4 dx = texture(scenic_detail,world_position.zy/2048.0);
    vec4 dy = texture(scenic_detail,world_position.xz/2048.0);
    vec4 dz = texture(scenic_detail,world_position.xy/2048.0);
    vec4 detail = dx*weights.x+dy*weights.y+dz*weights.z;
    float macro = texture(scenic_detail,world_xz/32768.0).b;
    float rock = smoothstep(0.08,0.48,1.0-n.y);
    float rock_color = rock;
    vec4 surface_weights = vec4(rock,0.0,0.0,0.0);
    bool organized = scenic.terrain_surface.x > 0.5;
    bool correlated = scenic.terrain_surface.x > 2.5;
    if (organized) {
        vec2 uv = (world_xz/params.grid_cell.z+0.5*(params.grid_cell.xy-1.0)+0.5)
                  /params.grid_cell.xy;
        surface_weights = scenic.terrain_surface.x < 1.5
            ? texture(terrain_landform_surface,uv) : texture(terrain_climate_surface,uv);
        if (correlated) surface_weights = texture(terrain_correlated_surface,uv);
        rock = surface_weights.r;
        rock_color = rock;
    }
    if (scenic.terrain_macro.w != 1.0) rock_color *= scenic.terrain_macro.w;
    vec3 grass = mix(vec3(0.070,0.105,0.043),vec3(0.14,0.18,0.080),macro);
    vec3 mineral = mix(vec3(0.15,0.14,0.12),vec3(0.25,0.24,0.21),detail.b)*scenic.art_direction.y;
    vec3 base = mix(grass,mineral,rock_color)*(0.85+0.30*detail.b);
    float material_blend = params.terrain_palette.y;
    if (material_blend>0.0) {
        // Reuse the neutral weathered mineral/soil interpretation of Cubey's
        // terrain backdrop. Same four mipmapped samples; no invented geometry.
        float weathering = clamp(0.55*macro+0.30*detail.b+0.15*rock,0.0,1.0);
        vec3 soil = mix(terrain_linear(vec3(0.260,0.275,0.280)),
                        terrain_linear(vec3(0.380,0.325,0.265)),weathering);
        vec3 stone = mix(terrain_linear(vec3(0.325,0.350,0.370)),
                         terrain_linear(vec3(0.465,0.400,0.325)),
                         smoothstep(0.16,0.84,0.65*detail.b+0.35*macro));
        vec3 refined = mix(soil,stone,rock_color)*(0.92+0.16*detail.b);
        base = mix(base,refined,material_blend);
    }
    if (organized) {
        // The same mineral structure affects colour and roughness. Imported
        // climate never becomes high-frequency noise or live wetness.
        float mineral_feature = smoothstep(0.18,0.82,detail.b);
        vec3 stone = mix(terrain_linear(vec3(0.39,0.40,0.40)),
                         terrain_linear(vec3(0.55,0.50,0.44)),mineral_feature);
        vec3 dry_soil = mix(terrain_linear(vec3(0.43,0.34,0.25)),
                            terrain_linear(vec3(0.57,0.46,0.34)),mineral_feature);
        vec3 sheltered_soil = mix(terrain_linear(vec3(0.30,0.31,0.26)),
                                  terrain_linear(vec3(0.43,0.41,0.32)),mineral_feature);
        // Cover potential affects bare-substrate character; no fake grass
        // texture or vegetation geometry is implied by this first recipe.
        float shelter = clamp(surface_weights.g*1.4+surface_weights.b*0.2,0.0,1.0);
        vec3 soil = mix(dry_soil,sheltered_soil,shelter);
        base = mix(soil,stone,rock_color);
        base = mix(base,terrain_linear(vec3(0.80,0.82,0.82)),surface_weights.a);
    }
    if (scenic.ground_material.z!=1.0) {
        float luminance = dot(base,vec3(0.2126,0.7152,0.0722));
        base = mix(vec3(luminance),base,scenic.ground_material.z);
    }
    float footprint = max(length(dFdx(world_position)),length(dFdy(world_position)));
    float surface_detail_scale = organized ? mix(0.35,0.85,rock) : 1.0;
    float detail_strength = 0.12*(1.0-smoothstep(18.0,72.0,footprint))*surface_detail_scale;
    n = normalize(n+vec3(detail.r-0.5,0.0,detail.g-0.5)*detail_strength);
    float normal_strength = params.terrain_palette.z;
    if (normal_strength>=0.0) {
        vec3 gradient = fluid25d_triplanar_surface_gradient(base_normal,weights,
            fluid25d_normal_derivative(dx.rg),fluid25d_normal_derivative(dy.rg),
            fluid25d_normal_derivative(dz.rg));
        n = fluid25d_resolve_surface_normal(base_normal,gradient,
            normal_strength*fluid25d_detail_fade(footprint)*surface_detail_scale);
    }
    if (terrain_view == 10) n = base_normal;
    vec2 f;
    vec2 coordinate = world_xz/params.grid_cell.z+0.5*(params.grid_cell.xy-1.0);
    uvec4 i = fluid25d_quad_indices(coordinate,uvec2(params.grid_cell.xy),f);
    float h = max(0.0,fluid25d_triangle_sample(vec4(depth.values[i.x],depth.values[i.y],
                                                    depth.values[i.z],depth.values[i.w]),f));
    // Current thin film only: no invented persistent wetness history or widening banks.
    float wet = smoothstep(params.camera_wet.w,0.012,h)*(1.0-smoothstep(0.02,0.05,h));
    base *= mix(1.0,scenic.ground_material.y,wet);
    if (terrain_view == 9) base = vec3(0.12);
    float roughness = mix(clamp(0.78+0.10*(detail.a-0.5),0.65,0.90),scenic.ground_material.x,wet);
    if (material_blend>0.0) {
        float dry_roughness = clamp(mix(0.94,0.86,rock)+0.08*(detail.a-0.5),0.80,0.98);
        roughness = mix(roughness,mix(dry_roughness,scenic.ground_material.x,wet),material_blend);
    }
    if (organized) {
        float dry_roughness = clamp(mix(0.97,0.90,rock)+0.04*(detail.b-0.5),0.87,0.99);
        roughness = mix(dry_roughness,scenic.ground_material.x,wet);
    }
    vec3 view = normalize(params.camera_wet.xyz-world_position);
    float sun_visibility = scenic_sun_visibility(world_position,n);
    if (scenic.terrain_macro.z != 1.0)
        sun_visibility = mix(1.0,sun_visibility,scenic.terrain_macro.z);
    if (terrain_view == 13) sun_visibility = 1.0;
    vec3 direct = terrain_lighting_direct(base,roughness,n,view,
        scenic.light_direction_exposure.xyz,scenic.light_color_mips.xyz,
        sun_visibility);
    if (scenic.terrain_macro.y != 1.0 || terrain_view == 12 || terrain_view == 14) {
        float ndotl = max(dot(n,scenic.light_direction_exposure.xyz),0.0);
        vec3 half_vector = view+scenic.light_direction_exposure.xyz;
        float half_length_squared = dot(half_vector,half_vector);
        vec3 fresnel = vec3(0.0);
        if (dot(n,view)>0.0 && half_length_squared>1e-8)
            fresnel = cubey_pbr_fresnel_schlick(max(dot(view,half_vector*inversesqrt(half_length_squared)),0.0),vec3(0.04));
        vec3 diffuse = cubey_pbr_lambert_diffuse(base)*(vec3(1.0)-fresnel)*
            scenic.light_color_mips.xyz*ndotl*sun_visibility;
        vec3 specular = max(direct-diffuse,vec3(0.0));
        direct = terrain_view == 14 ? specular :
            diffuse+specular*(terrain_view == 12 ? 0.0 : scenic.terrain_macro.y);
    }
    vec3 ambient_normal = normal_strength>=0.0 ? base_normal : n;
    vec3 irradiance = texture(scenic_irradiance,ambient_normal).rgb;
    if (scenic.art_direction.w>0.0)
        irradiance = mix(irradiance,terrain_soft_irradiance(base_normal),scenic.art_direction.w);
    if (scenic.terrain_macro.x>0.0)
        irradiance = mix(irradiance,texture(terrain_diffuse_irradiance,base_normal).rgb,scenic.terrain_macro.x);
    if (scenic.environment_mode.x>0.5) irradiance = scenic_irradiance_at(ambient_normal);
    vec3 ambient = terrain_lighting_ambient(base,irradiance,
                                              terrain_lighting_ambient_visibility(world_normal,0.0));
    vec3 color = direct*scenic.surface_material.x+ambient*scenic.ground_material.w;
    if (terrain_view == 2) color = base;
    if (terrain_view == 3) color = organized
        ? vec3(rock,surface_weights.g,surface_weights.a) : vec3(rock,1.0-rock,0.0);
    if (terrain_view == 4) color = base_normal*0.5+0.5;
    if (terrain_view == 5) color = n*0.5+0.5;
    if (terrain_view == 6) color = vec3(roughness);
    if (terrain_view == 7) color = direct*scenic.surface_material.x;
    if (terrain_view == 8) color = ambient*scenic.ground_material.w;
    if (terrain_view == 14) color = direct*scenic.surface_material.x;
    out_color = vec4(color,1.0);
}
