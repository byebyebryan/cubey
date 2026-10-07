#version 450
#extension GL_GOOGLE_include_directive : require
#include "cubey/pbr.glsl"
#include "fluid_25d_scenic.glsl"
#include "fluid_25d_surface_sampling.glsl"
layout(set=0,binding=1,std430) readonly buffer Depth { float values[]; } depth;
layout(set=1,binding=9,std430) readonly buffer VisualVelocity { vec4 values[]; } visual_velocity;
layout(set=0,binding=8,std430) readonly buffer DisplayCoverage { vec4 grid; float values[]; } display_coverage;
layout(push_constant) uniform Params {
    mat4 view_projection; vec4 grid_cell; vec4 camera_wet; vec4 presentation; vec4 terrain_palette;
} params;
layout(location=0) in vec3 world_position;
layout(location=1) in vec3 world_normal;
layout(location=2) in float triangle_water_depth;
layout(location=3) in vec3 water_flow;
layout(location=4) in float presentation_cue;
layout(location=5) in float dye_concentration;
layout(location=6) in vec2 field_coordinate;
layout(location=0) out vec4 out_color;

vec2 flow_detail(vec2 p, vec2 flow, float clock) {
    // Two half-period-offset layers reset only at zero weight. Local spatial
    // dephasing avoids a whole river pulsing in unison; offsets remain bounded.
    float noise = texture(scenic_detail,p/512.0).b;
    float phase0 = fract(clock/8.0+noise);
    float phase1 = fract(clock/8.0+noise+0.5);
    float weight0 = 1.0-abs(2.0*phase0-1.0);
    float weight1 = 1.0-abs(2.0*phase1-1.0);
    vec2 offset = flow*8.0/96.0;
    vec2 a = texture(scenic_detail,p/96.0-offset*phase0).rg*2.0-1.0;
    vec2 b = texture(scenic_detail,p/96.0-offset*phase1).rg*2.0-1.0;
    return (a*weight0+b*weight1)/max(weight0+weight1,0.00001);
}
void main() {
    uint options = uint(params.terrain_palette.w);
    float h = triangle_water_depth;
    vec2 f;
    uvec4 i = fluid25d_quad_indices(field_coordinate,uvec2(params.grid_cell.xy),f);
    vec4 depths = vec4(depth.values[i.x],depth.values[i.y],depth.values[i.z],depth.values[i.w]);
    if ((options&8u)!=0u) h = fluid25d_supported_depth(depths,f,params.camera_wet.w);
    if ((options&16u)!=0u && fluid25d_bilinear_sample(vec4(greaterThan(depths,vec4(params.camera_wet.w))),f)<=0.5) h=0.0;
    float edge_width = max(fwidth(h),0.000001);
    vec3 n = normalize(world_normal);
    vec3 view = normalize(params.camera_wet.xyz-world_position);
    vec2 visual_u = vec2(fluid25d_bilinear_sample(vec4(visual_velocity.values[i.x].x,
        visual_velocity.values[i.y].x,visual_velocity.values[i.z].x,visual_velocity.values[i.w].x),f),
        fluid25d_bilinear_sample(vec4(visual_velocity.values[i.x].y,
        visual_velocity.values[i.y].y,visual_velocity.values[i.z].y,visual_velocity.values[i.w].y),f));
    float speed = length(visual_u);
    vec2 flow = visual_u / max(1.0,speed/2.0);
    float footprint = max(length(dFdx(world_position)),length(dFdy(world_position)));
    float detail_strength = 0.14*smoothstep(0.025,0.40,speed)*(1.0-smoothstep(2.0,12.0,footprint));
    vec2 detail = flow_detail(world_position.xz,flow,scenic.clock_encoding.x)*detail_strength;
    n = normalize(n+vec3(detail.x,0.0,detail.y));
    vec3 nx = dFdx(n), ny = dFdy(n);
    // Ocean's normal-variance roughness principle: unresolved detail broadens
    // reflection instead of turning into bright crawling pixels.
    float roughness = sqrt(clamp(0.18*0.18+min(0.18,0.5*max(dot(nx,nx),dot(ny,ny))),0.0324,0.36));
    if (h<=params.camera_wet.w) discard;
    vec2 uv = gl_FragCoord.xy/scenic.clock_encoding.zw;
    float scene_z = texture(scenic_depth,uv).r;
    // Use the same raster-only depth bias as the retained native water VS.
    // Optical distance below uses unbiased world positions, never gl_FragCoord.z.
    if (gl_FragCoord.z>scene_z) discard;
    vec3 bed = scenic_unproject(uv,scene_z);
    vec3 physical_ray = bed-world_position;
    physical_ray.y /= max(params.grid_cell.w,0.001);
    float path_m = clamp(length(physical_ray),0.0,80.0);
    // A screen-space refraction approximation, deliberately capped at 2 px.
    vec2 normal_screen = (params.view_projection*vec4(n,0.0)).xy;
    normal_screen /= max(1.0,length(normal_screen));
    vec2 candidate_uv = clamp(uv+normal_screen*min(2.0,path_m*0.5)/scenic.clock_encoding.zw,
                               0.5/scenic.clock_encoding.zw,1.0-0.5/scenic.clock_encoding.zw);
    float candidate_z = texture(scenic_depth,candidate_uv).r;
    vec3 candidate_bed = scenic_unproject(candidate_uv,candidate_z);
    // Reject foreground and bank/sky samples before fetching their color.
    bool safe = candidate_z<1.0 && candidate_bed.y<=world_position.y+0.02 &&
        dot(candidate_bed-world_position,-view)>=-0.02;
    // Linear color filtering covers four texels: reject the offset if any
    // footprint texel belongs to foreground terrain, not just its center.
    ivec2 extent = textureSize(scenic_depth,0);
    ivec2 low = ivec2(floor(candidate_uv*vec2(extent)-0.5));
    for (int y=0;y<2;++y) for (int x=0;x<2;++x) {
        float z = texelFetch(scenic_depth,clamp(low+ivec2(x,y),ivec2(0),extent-1),0).r;
        safe = safe && z>=gl_FragCoord.z && z<1.0;
    }
    vec2 bed_cell = candidate_bed.xz/params.grid_cell.z+0.5*(params.grid_cell.xy-1.0);
    ivec2 c = clamp(ivec2(floor(bed_cell+0.5)),ivec2(0),ivec2(params.grid_cell.xy)-1);
    safe = safe && depth.values[c.y*int(params.grid_cell.x)+c.x]>params.camera_wet.w;
    vec3 background = texture(scenic_opaque,safe ? candidate_uv : uv).rgb;
    vec3 transmittance = exp(-vec3(0.17,0.055,0.030)*path_m);
    vec3 transmitted = background*transmittance+vec3(0.015,0.085,0.105)*(1.0-transmittance);
    float ndotv = max(dot(n,view),0.0);
    vec3 fresnel = cubey_pbr_fresnel_schlick(ndotv,vec3(0.02037));
    vec3 reflection = textureLod(scenic_environment,reflect(-view,n),
                                  roughness*(scenic.light_color_mips.w-1.0)).rgb;
    vec3 l = scenic.light_direction_exposure.xyz;
    vec3 half_vector = view+l;
    float half_length = dot(half_vector,half_vector);
    float ndotl = max(dot(n,l),0.0);
    vec3 specular = vec3(0);
    if (half_length>0.000001 && ndotv>0.0 && ndotl>0.0) {
        vec3 half_direction = half_vector*inversesqrt(half_length);
        specular = cubey_pbr_fresnel_schlick(max(dot(view,half_direction),0.0),vec3(0.02037))*
            cubey_pbr_distribution_ggx(max(dot(n,half_direction),0.0),roughness)*
            cubey_pbr_visibility_smith_ggx_correlated(ndotv,ndotl,roughness)*ndotl*
            scenic.light_color_mips.xyz*scenic_sun_visibility(world_position,n);
    }
    vec2 environment_brdf = texture(scenic_brdf,vec2(ndotv,roughness)).rg;
    vec3 color = transmitted*(1.0-fresnel)+
        reflection*(vec3(0.02037)*environment_brdf.x+environment_brdf.y)+specular;
    float coverage = smoothstep(0.0,edge_width,h-params.camera_wet.w);
    if (params.presentation.z>0.5) coverage *= smoothstep(0.002,0.050,h);
    if ((options&512u)!=0u) {
        vec2 cf;
        uvec4 ci = fluid25d_quad_indices(field_coordinate*display_coverage.grid.z,uvec2(display_coverage.grid.xy),cf);
        coverage *= clamp(fluid25d_bilinear_sample(vec4(display_coverage.values[ci.x],display_coverage.values[ci.y],
            display_coverage.values[ci.z],display_coverage.values[ci.w]),cf),0.0,1.0);
    }
    out_color = vec4(color*coverage,coverage);
}
