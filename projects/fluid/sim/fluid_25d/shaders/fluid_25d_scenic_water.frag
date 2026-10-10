#version 450
#extension GL_GOOGLE_include_directive : require
#define FLUID25D_SCENIC_WATER
#include "cubey/pbr.glsl"
#include "cubey/procedural/noise.glsl"
#include "fluid_25d_bank_edge.glsl"
#include "fluid_25d_scenic.glsl"
#include "fluid_25d_surface_sampling.glsl"
#include "fluid_25d_water_film.glsl"
#include "fluid_25d_surface_gradient.glsl"
#include "fluid_25d_water_detail.glsl"
#include "fluid_25d_water_agitation.glsl"
#include "fluid_25d_water_rapids.glsl"
layout(set=0,binding=0,std430) readonly buffer Terrain { float values[]; } terrain;
layout(set=0,binding=1,std430) readonly buffer Depth { float values[]; } depth;
#include "fluid_25d_bspline_surface.glsl"
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
float rapid_pattern(vec2 p, vec2 velocity, float clock, float period) {
    vec2 dx=dFdx(p),dy=dFdy(p);
    float noise=textureGrad(scenic_detail,p/512.0,dx/512.0,dy/512.0).b;
    vec4 phases=fluid25d_rapid_phases(clock,noise);
    // World-fixed base coordinates, bounded offsets, no rotation by a
    // spatially varying velocity. The 8 m/s cap is an artist motion limit.
    vec2 flow=velocity/max(1.0,length(velocity)/8.0);
    vec2 uv0=p/period-flow*(phases.x-0.5)*8.0/period;
    vec2 uv1=p/period-flow*(phases.y-0.5)*8.0/period+vec2(0.37,0.61);
    float a=textureGrad(scenic_detail,uv0,dFdx(uv0),dFdy(uv0)).b;
    float b=textureGrad(scenic_detail,uv1,dFdx(uv1),dFdy(uv1)).b;
    float pattern=a*phases.z+b*phases.w;
    float edge=max(0.005,0.5*fwidth(pattern));
    return smoothstep(0.50-edge,0.62+edge,pattern);
}
float stream_foam_pattern(vec2 p, vec2 velocity, float clock, float patchiness) {
    // Uniform fallback retains the previous stream/cascade arithmetic exactly.
    if (patchiness<=0.0) return rapid_pattern(p,velocity,clock,64.0);
    vec2 dx=dFdx(p),dy=dFdy(p);
    float noise=textureGrad(scenic_detail,p/512.0,dx/512.0,dy/512.0).b;
    vec4 phases=fluid25d_rapid_phases(clock,noise);
    vec2 flow=velocity/max(1.0,length(velocity)/8.0);
    vec2 p0=p-flow*(phases.x-0.5)*8.0;
    vec2 p1=p-flow*(phases.y-0.5)*8.0;
    vec3 domain0=fluid25d_stream_foam_domain(p0);
    vec3 domain1=fluid25d_stream_foam_domain(p1+vec2(117.7,241.3));
    vec2 uv0=mix(p0/64.0,domain0.xy,patchiness);
    vec2 uv1=mix(p1/64.0+vec2(0.37,0.61),domain1.xy,patchiness);
    float a=textureGrad(scenic_detail,uv0,dFdx(uv0),dFdy(uv0)).b;
    float b=textureGrad(scenic_detail,uv1,dFdx(uv1),dFdy(uv1)).b;
    float combined=a*phases.z+b*phases.w;
    vec3 edges=max(vec3(0.005),0.5*vec3(fwidth(combined),fwidth(a),fwidth(b)));
    // Filter and gate each layer before crossfading. The envelope moves with
    // the texture; it is not a stationary world mask pinning foam to the bed.
    vec2 footprint=vec2(max(length(dFdx(p0)),length(dFdy(p0))),
                        max(length(dFdx(p1)),length(dFdy(p1))))/173.0;
    vec2 broad=mix(vec2(domain0.z,domain1.z),vec2(0.5),smoothstep(0.4,1.0,footprint));
    vec2 envelope=mix(vec2(1),smoothstep(0.18,0.75,broad),patchiness);
    return fluid25d_stream_foam_shape(vec2(a,b),phases.zw,patchiness,edges,envelope);
}
float cascade_pattern(vec3 p, vec3 velocity, float clock, float period) {
    float dephase=0.5+0.5*cubey_proc_value_noise_3d(p/800.0,0x25d1u);
    vec4 phases=fluid25d_rapid_phases(clock,dephase);
    // A fixed 3D domain survives vertical faces. Long vertical features, no
    // borrowed terrain texture and no per-fragment coordinate rotation.
    vec3 scale=period*vec3(0.20,0.90,0.20);
    vec3 a=(p-velocity*(phases.x-0.5)*8.0)/scale;
    vec3 b=(p-velocity*(phases.y-0.5)*8.0)/scale+vec3(7.3,2.1,4.7);
    // Filter each layer BEFORE weighting. A resetting zero-weight layer must
    // never force the live layer to its mean via an unrelated huge derivative.
    float fa=max(length(dFdx(a)),length(dFdy(a)));
    float fb=max(length(dFdx(b)),length(dFdy(b)));
    float va=mix(0.5,0.5+0.5*cubey_proc_value_noise_3d(a,0x25d2u),
                 1.0-smoothstep(0.4,1.0,fa));
    float vb=mix(0.5,0.5+0.5*cubey_proc_value_noise_3d(b,0x25d2u),
                 1.0-smoothstep(0.4,1.0,fb));
    float value=va*phases.z+vb*phases.w;
    float edge=max(0.025,0.5*fwidth(value));
    return smoothstep(0.40-edge,0.65+edge,value);
}
vec2 surface_at(vec2 p) {
    vec2 f; uvec4 i=fluid25d_quad_indices(p,uvec2(params.grid_cell.xy),f);
    vec4 h=vec4(depth.values[i.x],depth.values[i.y],depth.values[i.z],depth.values[i.w]);
    vec4 z=vec4(terrain.values[i.x],terrain.values[i.y],terrain.values[i.z],terrain.values[i.w]);
    // Conservative landing cue: do not infer a wet drop from dry neighbor bed.
    if (any(lessThanEqual(h,vec4(params.camera_wet.w)))) return vec2(0);
    return vec2(fluid25d_bilinear_sample(z+h,f),fluid25d_bilinear_sample(h,f));
}
float landing_activity(vec3 n,vec2 velocity,float h) {
    float speed=length(velocity);
    if (speed<=0.00001 || n.y<=0.0) return 0.0;
    vec2 direction=velocity/speed;
    vec2 near_p=field_coordinate-direction;
    vec2 far_p=field_coordinate-direction*2.0;
    if (any(lessThan(near_p,vec2(0))) || any(lessThan(far_p,vec2(0))) ||
        any(greaterThan(near_p,params.grid_cell.xy-1.0)) ||
        any(greaterThan(far_p,params.grid_cell.xy-1.0))) return 0.0;
    vec2 near_h=surface_at(near_p),far_h=surface_at(far_p);
    float local=dot(n.xz,direction)/(max(n.y,0.05)*max(params.grid_cell.w,0.001));
    return fluid25d_landing_activity(local,(far_h.x-near_h.x)/params.grid_cell.z,
                                    speed,vec3(h,near_h.y,far_h.y),params.camera_wet.w);
}
vec3 wet_normal(uint index) {
    uvec2 extent = uvec2(params.grid_cell.xy);
    uvec2 c = uvec2(index%extent.x,index/extent.x);
    uvec4 adjacent = uvec4(c.x>0u ? index-1u : index,
                          c.x+1u<extent.x ? index+1u : index,
                          c.y>0u ? index-extent.x : index,
                          c.y+1u<extent.y ? index+extent.x : index);
    vec4 dh = vec4(depth.values[adjacent.x],depth.values[adjacent.y],
                   depth.values[adjacent.z],depth.values[adjacent.w]);
    vec4 eta = vec4(terrain.values[adjacent.x],terrain.values[adjacent.y],
                    terrain.values[adjacent.z],terrain.values[adjacent.w])+dh;
    bvec4 supported = greaterThan(dh,vec4(params.camera_wet.w));
    supported = bvec4(supported.x && c.x>0u,supported.y && c.x+1u<extent.x,
                      supported.z && c.y>0u,supported.w && c.y+1u<extent.y);
    return fluid25d_wet_surface_normal(terrain.values[index]+depth.values[index],
                                       eta,supported,params.grid_cell.z,params.grid_cell.w);
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
    uint requested_water_view = uint(scenic.water_view.x);
    uint water_view = requested_water_view==19u ? 0u : requested_water_view;
    float film_weight = fluid25d_film_weight(h,scenic.water_film.x,scenic.water_film.y);
    vec3 n = normalize(world_normal);
    if (scenic.water_view.y>0.0) {
        vec4 weights = vec4((1.0-f.x)*(1.0-f.y),f.x*(1.0-f.y),
                             (1.0-f.x)*f.y,f.x*f.y)*
                       vec4(greaterThan(depths,vec4(params.camera_wet.w)));
        if (dot(weights,vec4(1.0))>1e-6) {
            vec3 wet_n = normalize(wet_normal(i.x)*weights.x+wet_normal(i.y)*weights.y+
                                   wet_normal(i.z)*weights.z+wet_normal(i.w)*weights.w);
            n = normalize(mix(n,wet_n,scenic.water_view.y));
        }
    }
    vec3 view = normalize(params.camera_wet.xyz-world_position);
    vec2 visual_u = vec2(fluid25d_bilinear_sample(vec4(visual_velocity.values[i.x].x,
        visual_velocity.values[i.y].x,visual_velocity.values[i.z].x,visual_velocity.values[i.w].x),f),
        fluid25d_bilinear_sample(vec4(visual_velocity.values[i.x].y,
        visual_velocity.values[i.y].y,visual_velocity.values[i.z].y,visual_velocity.values[i.w].y),f));
    float speed = length(visual_u);
    float rapid_activity = 0.0;
    if (((scenic.water_rapids.x>0.0 || scenic.water_rapids.z>0.0 || scenic.water_stream_foam.x>0.0) &&
         (water_view==0u || water_view>=12u)) || water_view==12u)
        rapid_activity=fluid25d_rapid_activity(n,visual_u,h,params.grid_cell.w,
                                              params.camera_wet.w);
    float rapid_gain=scenic.water_rapids.x*rapid_activity;
    float rapid_foam=0.0;
    float stream_foam=0.0;
    if (scenic.water_stream_foam.x>0.0 && water_view==0u) {
        float activity=fluid25d_stream_foam_activity(speed,h,rapid_activity,params.camera_wet.w);
        stream_foam=0.35*scenic.water_stream_foam.x*activity*
                    stream_foam_pattern(world_position.xz,visual_u,scenic.water_agitation.w,
                                        scenic.water_stream_foam.y);
    }
    // Uniform branch: derivatives remain defined even in inactive lake pixels.
    if (scenic.water_rapids.x>0.0 && (water_view==0u || water_view==13u))
        rapid_foam=0.25*rapid_gain*rapid_pattern(world_position.xz,visual_u,
                                  scenic.water_agitation.w,scenic.water_rapids.y);
    float cascade_gain=0.0,landing_gain=0.0,cascade_weight=0.0,landing_weight=0.0;
    if (scenic.water_rapids.z>0.0 && (water_view==0u || water_view==15u || water_view==17u))
        cascade_gain=scenic.water_rapids.z*rapid_activity*
                     fluid25d_cascade_support(h,params.camera_wet.w);
    float landing=0.0;
    if ((scenic.water_rapids.w>0.0 && water_view==0u) || water_view==16u)
        landing=landing_activity(n,visual_u,h);
    if (water_view==0u) landing_gain=scenic.water_rapids.w*landing;
    // Uniform branch, so all derivative-based filtering precedes any discard.
    if ((scenic.water_rapids.z>0.0 || scenic.water_rapids.w>0.0) &&
        (water_view==0u || water_view==15u || water_view==17u)) {
        vec3 p=world_position; p.y/=max(params.grid_cell.w,0.001);
        float pattern=cascade_pattern(p,fluid25d_cascade_flow(n,visual_u,params.grid_cell.w),
                                      scenic.water_agitation.w,scenic.water_rapids.y);
        cascade_weight=fluid25d_cascade_replacement(cascade_gain,pattern);
        landing_weight=0.75*landing_gain*(0.35+0.65*pattern);
    }
    // Activity is a visual proxy from filtered speed and the unperturbed
    // surface slope. A steep, stationary lake is not flowing turbulence.
    float surface_slope = length(n.xz)/max(n.y,0.05);
    vec2 flow = visual_u / max(1.0,speed/2.0);
    float footprint = max(length(dFdx(world_position)),length(dFdy(world_position)));
    float detail_strength = 0.14*scenic.surface_material.w*smoothstep(0.025,0.40,speed)*(1.0-smoothstep(2.0,12.0,footprint));
    if (water_view==7u) detail_strength=0.0;
    vec2 detail = flow_detail(world_position.xz,flow,scenic.clock_encoding.x)*detail_strength;
    n = normalize(n+vec3(detail.x,0.0,detail.y));
    float lost_variance = 0.0;
    vec2 ripple_dx=dFdx(world_position.xz),ripple_dy=dFdy(world_position.xz);
    if (scenic.water_view.z>0.0 && water_view!=7u) {
        float ripple_strength = scenic.water_view.z*scenic.surface_material.w*
                                smoothstep(scenic.water_film.x,scenic.water_film.y,h);
        // Skip dry/shallow zero-amplitude work. Explicit gradients keep texture
        // filtering defined across this depth-dependent branch.
        if (ripple_strength>0.0) {
            // Ocean-inspired domain decorrelation, using the existing filtered
            // procedural texture at broad, differently rotated/scaled domains.
            vec2 p = world_position.xz;
            vec3 dephase = vec3(textureGrad(scenic_detail,p/512.0,ripple_dx/512.0,ripple_dy/512.0).b,
                textureGrad(scenic_detail,vec2(p.y,-p.x)/683.0+vec2(0.17,0.41),
                            vec2(ripple_dx.y,-ripple_dx.x)/683.0,vec2(ripple_dy.y,-ripple_dy.x)/683.0).b,
                textureGrad(scenic_detail,p/937.0+vec2(0.73,0.29),ripple_dx/937.0,ripple_dy/937.0).b)*2.0;
            vec3 ripple = fluid25d_ripple_detail(p,scenic.clock_encoding.x,
                             max(length(ripple_dx),length(ripple_dy)),
                             scenic.water_view.w,ripple_strength,dephase);
            vec3 gradient = vec3(ripple.x,0.0,ripple.y);
            gradient -= dot(n,gradient)*n;
            n = fluid25d_resolve_surface_normal(n,gradient,1.0);
            lost_variance = ripple.z;
        }
    }
    if (water_view!=7u && (scenic.water_agitation.x>0.0 || scenic.water_agitation.y>0.0)) {
        float open_water = smoothstep(scenic.water_film.x,scenic.water_film.y,h);
        float activity = smoothstep(0.08,1.4,speed)*
                         (0.35+0.65*smoothstep(0.015,0.22,surface_slope));
        float flow_variance = 0.07*activity*open_water*
                              scenic.water_agitation.x*scenic.water_agitation.x;
        float rain_variance = 0.08*scenic.water_agitation.z*open_water*
                              scenic.water_agitation.y*scenic.water_agitation.y;
        if (flow_variance>0.0 || rain_variance>0.0) {
            vec3 agitation = vec3(0.0);
            if (flow_variance>0.0)
                agitation += fluid25d_agitation_bands(world_position.xz,scenic.water_agitation.w,
                    footprint,visual_u/max(speed,0.00001),4.0,flow_variance);
            if (rain_variance>0.0)
                agitation += fluid25d_agitation_bands(world_position.xz,scenic.water_agitation.w,
                    footprint,vec2(0.8,0.6),1.25,rain_variance);
            vec3 gradient = vec3(agitation.x,0.0,agitation.y);
            gradient -= dot(n,gradient)*n;
            n = fluid25d_resolve_surface_normal(n,gradient,1.0);
            lost_variance += agitation.z;
        }
    }
    vec3 nx = dFdx(n), ny = dFdy(n);
    // Ocean's normal-variance roughness principle: unresolved detail broadens
    // reflection instead of turning into bright crawling pixels.
    float base_roughness = scenic.surface_material.z;
    float roughness = sqrt(clamp(base_roughness*base_roughness+min(0.18,0.5*max(dot(nx,nx),dot(ny,ny))),base_roughness*base_roughness,0.36));
    if (lost_variance>0.0) roughness = sqrt(min(0.36,roughness*roughness+lost_variance));
    // The retained V5 wet-ground treatment: only shallow RGB shading changes.
    if (film_weight>0.0)
        roughness=mix(roughness,max(roughness,scenic.water_film.z),film_weight);
    if (rapid_gain>0.0 && (water_view==0u || water_view==14u))
        roughness=mix(roughness,max(roughness,0.52),rapid_gain);
    if (cascade_gain>0.0 || landing_gain>0.0)
        roughness=mix(roughness,max(roughness,0.72),max(cascade_gain,landing_gain));
    // Evaluate before any fragment-dependent discard/specular branch so shared
    // receiver-plane derivatives are defined for the entire quad.
    float sun_visibility=scenic_sun_visibility(world_position,n);
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
    float extinction_scale = scenic.water_optics.x;
    if (scenic.water_shallow_optics.x>0.0) {
        // Artistic absorption only; local physical depth, not body size or
        // coverage. The cubic reaches the retained base with zero slope/curvature.
        // For base=1 and boost<=3, h*scale stays monotone on a vertical ray;
        // a direct smoothstep scale blend can instead brighten at greater depth.
        float remaining = max(0.0,1.0-h/max(scenic.water_shallow_optics.y,0.05));
        extinction_scale += scenic.water_shallow_optics.x*remaining*remaining*remaining;
    }
    vec3 transmittance = exp(-vec3(0.17,0.055,0.030)*path_m*extinction_scale);
    vec3 scattering = vec3(0.015,0.085,0.105)*scenic.water_optics.y;
    if (scenic.water_optics.w>0.0) {
        // Artistic single-layer source lighting, not a new volume integrator.
        // Current-field water in shadow should not share an unlit turquoise source.
        vec3 source_light = scenic_irradiance_at(n)+
            scenic_sun_radiance()*max(dot(n,scenic.light_direction_exposure.xyz),0.0)*
            sun_visibility/CUBEY_PBR_PI;
        scattering *= mix(vec3(1.0),source_light,scenic.water_optics.w);
    }
    vec3 transmitted = background*transmittance+scattering*(1.0-transmittance);
    if (scenic.art_direction.x>0.0 && water_view!=6u) {
        // Explicit visual-demo tint within existing coverage; not apparent depth
        // or enlarged wet area. It fades out before the deeper optical treatment.
        float clarity = scenic.art_direction.x*(1.0-smoothstep(0.15,0.50,h));
        vec3 clearer_bed = background*vec3(0.55,0.95,1.35);
        transmitted = mix(transmitted,clearer_bed,clarity);
    }
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
            scenic_sun_radiance()*sun_visibility;
    }
    vec2 environment_brdf = texture(scenic_brdf,vec2(ndotv,roughness)).rg;
    vec3 environment_term=reflection*(vec3(0.02037)*environment_brdf.x+environment_brdf.y)*scenic.water_optics.z;
    vec3 direct_term=specular*scenic.water_optics.z;
    vec3 transmission_term=transmitted*(1.0-fresnel);
    // Preserve the audited arithmetic and deeper-water result, not reassociated terms.
    vec3 color = transmitted*(1.0-fresnel)+
        (reflection*(vec3(0.02037)*environment_brdf.x+environment_brdf.y)+specular)*scenic.water_optics.z;
    if (film_weight>0.0)
        color=fluid25d_film_ground_emphasis(color,texture(scenic_opaque,uv).rgb,
                                            film_weight,scenic.water_film.w);
    if (water_view==0u && rapid_foam>0.0) {
        // Replace clear-water energy rather than adding emissive white. Share
        // sun/shadow and sky irradiance with the accepted Scenic lighting.
        vec3 foam_light=scenic_irradiance_at(n)+scenic_sun_radiance()*ndotl*
                         sun_visibility/CUBEY_PBR_PI;
        color=mix(color,vec3(0.22,0.25,0.23)*foam_light,rapid_foam);
    }
    if ((water_view==0u || water_view==17u) && (cascade_weight>0.0 || landing_weight>0.0)) {
        vec3 foam_light=scenic_irradiance_at(n)+scenic_sun_radiance()*ndotl*
                         sun_visibility/CUBEY_PBR_PI;
        float weight=1.0-(1.0-cascade_weight)*(1.0-landing_weight);
        color=mix(color,vec3(0.32,0.35,0.33)*foam_light,weight);
    }
    if (water_view==0u && stream_foam>0.0) {
        vec3 foam_light=scenic_irradiance_at(n)+scenic_sun_radiance()*ndotl*
                        sun_visibility/CUBEY_PBR_PI;
        color=mix(color,vec3(0.84,0.89,0.87)*foam_light*scenic.water_stream_foam.z,stream_foam);
    }
    if (water_view==1u) color=environment_term;
    if (water_view==2u) color=direct_term;
    if (water_view==3u) color=transmission_term;
    if (water_view==4u) color=transmission_term+direct_term;
    if (water_view==5u) color=transmission_term+environment_term;
    if (water_view==8u) color=h<0.01 ? vec3(1.0,0.8,0.01) : (h<0.05 ? vec3(1.0,0.20,0.01) : vec3(0.01,0.8,1.0));
    if (water_view==9u) color=vec3(1.0);
    if (water_view==10u) color=vec3(film_weight);
    if (water_view==11u) color=vec3(roughness);
    if (water_view==12u) color=vec3(rapid_activity);
    if (water_view==13u) color=vec3(rapid_foam);
    if (water_view==15u) color=vec3(cascade_weight);
    if (water_view==16u) color=vec3(landing);
    float coverage_depth=h;
    if ((requested_water_view==0u || requested_water_view==18u) &&
        (scenic.water_bank.x>0.0 || scenic.water_bank.y>0.0)) {
        // Compute derivatives before any per-fragment early exits in the helper.
        vec2 dx=dFdx(world_position.xz),dy=dFdy(world_position.xz);
        vec2 gradient=fluid25d_bank_gradient(dx,dy,dFdx(h),dFdy(h));
        float bank_depth=h;
        if (((options>>5u)&7u)>0u) {
            // Use the same cubic reconstruction as the displayed B-spline
            // surface, including its analytic continuous depth gradient.
            // Native-quad thresholds and triangle derivatives must not define
            // the irregular shoreline or its local strength limits.
            vec2 bh,bx,by;
            fluid25d_bspline_sample(field_coordinate,uvec2(params.grid_cell.xy),bh,bx,by);
            bank_depth=bh.y;
            gradient=vec2(bx.y,by.y)/params.grid_cell.z;
        }
        vec2 bank=fluid25d_bank_inset(world_position.xz,bank_depth,gradient,visual_u,
            scenic.water_agitation.w,max(length(dx),length(dy)),params.grid_cell.z,
            params.camera_wet.w,scenic.water_bank);
        if (requested_water_view==0u) {
            // Start the trim from reconstructed depth, not the native/refined
            // triangle ramp. Fade the analytic correction with the same smooth
            // edge band and never reveal water outside baseline coverage.
            coverage_depth=min(h,mix(h,bank_depth,bank.y)-bank.x*length(gradient));
            edge_width=max(fwidth(coverage_depth),0.000001);
        } else color=vec3(bank.y);
    }
    if (requested_water_view==18u && scenic.water_bank.x<=0.0 && scenic.water_bank.y<=0.0)
        color=vec3(0);
    float coverage = smoothstep(0.0,edge_width,coverage_depth-params.camera_wet.w);
    // Rain films make the displayed bank an opacity transition, not a true
    // wet/dry boundary. Trim that transition too; physical h, optics and raw
    // component views retain their original values. Zero strengths are exact.
    if (params.presentation.z>0.5) coverage *= smoothstep(0.002,0.050,coverage_depth);
    // Only presentation and its coverage view: raw field/component diagnostics
    // must not conceal water just because an artist chooses a depth fade.
    if ((water_view==0u || water_view==9u || water_view==14u || water_view==17u) && scenic.water_shallow_optics.z>0.0)
        coverage *= fluid25d_shallow_coverage(h,scenic.water_shallow_optics.z,
                                              scenic.water_shallow_optics.w);
    if ((options&512u)!=0u) {
        vec2 cf;
        uvec4 ci = fluid25d_quad_indices(field_coordinate*display_coverage.grid.z,uvec2(display_coverage.grid.xy),cf);
        coverage *= clamp(fluid25d_bilinear_sample(vec4(display_coverage.values[ci.x],display_coverage.values[ci.y],
            display_coverage.values[ci.z],display_coverage.values[ci.w]),cf),0.0,1.0);
    }
    out_color = vec4(color*coverage,coverage);
}
