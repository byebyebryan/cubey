#ifndef CUBEY_TERRAIN_MATERIAL_READABILITY_GLSL
#define CUBEY_TERRAIN_MATERIAL_READABILITY_GLSL
// Shared render-only scalar filtering. Samples retain their native sampler mip
// derivatives; no UV cell randomization, extra texture or terrain displacement.
// Landform/climate weights remain the dominant broad material organization.
vec3 terrain_material_world_position(vec3 local_position, vec2 source_origin_xz) {
    return local_position+vec3(source_origin_xz.x,0.0,source_origin_xz.y);
}
float terrain_readability_detail_weight(float footprint_m, float rock) {
    return (1.0-smoothstep(18.0,72.0,footprint_m))*mix(0.22,0.65,clamp(rock,0.0,1.0));
}
vec4 terrain_readability_detail(vec4 sample_value, float footprint_m, float rock) {
    return mix(vec4(0.5),sample_value,terrain_readability_detail_weight(footprint_m,rock));
}
vec4 terrain_readability_macro(vec4 sample_value, float footprint_m) {
    return mix(vec4(0.5),sample_value,0.35*(1.0-smoothstep(200.0,1000.0,footprint_m)));
}
#endif
