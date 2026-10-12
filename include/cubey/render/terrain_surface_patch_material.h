#pragma once

namespace cubey::render {

// Scenic-only artistic controls. Never used by hydraulic code.
struct TerrainSurfacePatchMaterial {
    float wet_roughness = 0.38F;
    float wet_darkening = 0.72F;
    float terrain_saturation = 1.0F;
    float terrain_ambient = 1.0F;
    float terrain_direct = 1.0F;
    float shadow_strength = 1.0F;
    float water_roughness = 0.18F;
    float water_normal_strength = 1.0F;
    float water_extinction_scale = 1.0F;
    // Artistic boost, fading to the base scale at a physical depth.
    float water_shallow_extinction_boost = 0.0F;
    float water_shallow_extinction_end_m = 2.0F;
    // Display-only shallow-water opacity. Zero preserves accepted coverage.
    float water_shallow_coverage_strength = 0.0F;
    float water_shallow_coverage_end_m = 0.30F;
    float water_scatter_scale = 1.0F;
    float water_reflection_scale = 1.0F;
    float water_scatter_lighting = 0.0F;
    float water_clarity = 0.0F;
    float terrain_mineral_scale = 1.0F;
    float terrain_material_blend = 0.0F;
    // -1 retains the legacy RG-to-world-XZ shortcut; >=0 uses surface gradients.
    float terrain_normal_strength = -1.0F;
    float terrain_ambient_softening = 0.0F;
    float terrain_diffuse_convolution = 0.0F;
    float terrain_specular_scale = 1.0F;
    float terrain_shadow_scale = 1.0F;
    float terrain_slope_color_scale = 1.0F;
    // Opt-in terrain readability study: 0 retained, 1 periodic detail off,
    // 2 footprint-filtered/world-stable. Does not affect water or geometry.
    float terrain_readability = 0.0F;
    float terrain_world_x_m = 0.0F;
    float terrain_world_z_m = 0.0F;
    // Shallow-water artistic shading in every Scenic preset. Physical metres.
    float film_begin_m = 0.02F;
    float film_end_m = 0.12F;
    float film_roughness = 0.65F;
    float film_ground_mix = 0.75F;
    // Rendering-study controls. Zero strength preserves the retained shading.
    float water_wet_normal = 0.0F;
    float water_ripple_strength = 0.0F; // slope, not displaced surface height
    float water_ripple_scale_m = 48.0F;
    // Render-only activity proxies, not turbulence or additional water mass.
    // Zero retains the previous shading, independently of visible rain.
    float water_flow_agitation = 0.0F;
    float water_rain_agitation = 0.0F;
    // Base/reference presets stay disabled; Macro enables reviewed cascades.
    float water_rapid_strength = 0.0F;
    float water_rapid_scale_m = 192.0F;  // texture period, not a physical wave size
    float water_cascade_strength = 0.0F; // directional artistic replacement, not detached water
    float water_landing_strength = 0.0F; // local steep-to-flat cue, not transported foam
    // Render-only whitewater; Macro enables reviewed 3 m flecks / 2x travel.
    float water_whitewater_strength = 0.0F;
    float water_whitewater_radius_m = 3.0F;
    float water_whitewater_lift_m = 1.5F;
    float water_whitewater_speed = 1.0F;       // travel only, not lifetime or hydraulic time
    float water_whitewater_budget = 131072.0F; // integer draw cap; no per-frame reseeding
    float water_stream_foam_strength = 0.0F;   // lighter material foam on fast gentler streams
    // Zero patchiness / unit brightness preserve the retained dense treatment.
    // Patchiness shapes coverage and non-repeating breakup, independently of
    // opacity, velocity and lighting.
    float water_stream_foam_patchiness = 0.0F;
    float water_stream_foam_brightness = 1.0F;
    // Opt-in display-edge insets, not terrain erosion or extra water. Metres.
    float water_bank_irregularity_m = 0.0F;
    float water_bank_motion_m = 0.0F;
    float water_bank_scale_m = 48.0F;
    float water_bank_band_m = 12.0F;
    float daylight_environment = 0.0F; // opt-in fixed shared Cubey sky, no weather system
    float daylight_exposure = 0.4F;    // EV bias; shared mode only, no automatic exposure
    float daylight_sun_scale = 1.0F;   // shared direct sun only; 1 matches sky source units
};

// Accepted macro recipe, shared by the native Fluid frontend and captures.
[[nodiscard]] TerrainSurfacePatchMaterial terrain_surface_patch_macro_material();
} // namespace cubey::render
