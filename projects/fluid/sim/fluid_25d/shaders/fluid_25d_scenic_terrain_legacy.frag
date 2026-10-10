#version 450
#extension GL_GOOGLE_include_directive : require
#include "cubey/terrain/terrain_lighting.glsl"
#include "fluid_25d_scenic.glsl"
#include "fluid_25d_surface_sampling.glsl"
layout(set=0,binding=0,std430) readonly buffer Terrain { float values[]; } terrain;
layout(set=0,binding=1,std430) readonly buffer Depth { float values[]; } depth;
#include "fluid_25d_terrain_wetness.glsl"
layout(push_constant) uniform Params {
    mat4 view_projection; vec4 grid_cell; vec4 camera_wet; vec4 presentation; vec4 terrain_palette;
} params;
layout(location=0) in vec3 world_position;
layout(location=1) in vec3 world_normal;
layout(location=2) in vec2 world_xz;
layout(location=0) out vec4 out_color;
void main() {
    vec3 n = normalize(world_normal);
    vec3 weights = pow(abs(n),vec3(4.0)); weights /= max(dot(weights,vec3(1)),0.00001);
    // Cubey's existing mipmapped material detail, triplanar to avoid steep-slope stretch.
    vec4 dx = texture(scenic_detail,world_position.zy/2048.0);
    vec4 dy = texture(scenic_detail,world_position.xz/2048.0);
    vec4 dz = texture(scenic_detail,world_position.xy/2048.0);
    vec4 detail = dx*weights.x+dy*weights.y+dz*weights.z;
    float macro = texture(scenic_detail,world_xz/32768.0).b;
    float rock = smoothstep(0.08,0.48,1.0-n.y);
    vec3 grass = mix(vec3(0.070,0.105,0.043),vec3(0.14,0.18,0.080),macro);
    vec3 mineral = mix(vec3(0.15,0.14,0.12),vec3(0.25,0.24,0.21),detail.b)*scenic.art_direction.y;
    vec3 base = mix(grass,mineral,rock)*(0.85+0.30*detail.b);
    if (scenic.ground_material.z!=1.0) {
        float luminance = dot(base,vec3(0.2126,0.7152,0.0722));
        base = mix(vec3(luminance),base,scenic.ground_material.z);
    }
    float footprint = max(length(dFdx(world_position)),length(dFdy(world_position)));
    float detail_strength = 0.12*(1.0-smoothstep(18.0,72.0,footprint));
    n = normalize(n+vec3(detail.r-0.5,0.0,detail.g-0.5)*detail_strength);
    vec2 coordinate = world_xz/params.grid_cell.z+0.5*(params.grid_cell.xy-1.0);
    float h = fluid25d_terrain_wet_depth(coordinate,uvec2(params.grid_cell.xy),
                                       params.presentation.x>0.0);
    float wet = fluid25d_terrain_wet_weight(h,params.camera_wet.w);
    base *= mix(1.0,scenic.ground_material.y,wet);
    float roughness = mix(clamp(0.78+0.10*(detail.a-0.5),0.65,0.90),scenic.ground_material.x,wet);
    vec3 view = normalize(params.camera_wet.xyz-world_position);
    vec3 direct = terrain_lighting_direct(base,roughness,n,view,
        scenic.light_direction_exposure.xyz,scenic.light_color_mips.xyz,
        scenic_sun_visibility(world_position,n));
    vec3 ambient = terrain_lighting_ambient(base,texture(scenic_irradiance,n).rgb,
                                              terrain_lighting_ambient_visibility(world_normal,0.0));
    out_color = vec4(direct*scenic.surface_material.x+ambient*scenic.ground_material.w,1.0);
}
