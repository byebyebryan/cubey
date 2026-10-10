#ifndef FLUID25D_BANK_EDGE_GLSL
#define FLUID25D_BANK_EDGE_GLSL
#include "cubey/procedural/noise.glsl"
#include "fluid_25d_water_rapids.glsl"

// Recover the physical XZ depth gradient from raster derivatives. Relative
// determinant guard makes a grazing/degenerate projection an unchanged edge.
vec2 fluid25d_bank_gradient(vec2 dx, vec2 dy, float hx, float hy) {
    float determinant=dx.x*dy.y-dx.y*dy.x;
    if (abs(determinant)<=1e-5*max(length(dx)*length(dy),1e-8)) return vec2(0);
    return vec2(hx*dy.y-hy*dx.y,dx.x*hy-dy.x*hx)/determinant;
}
float fluid25d_bank_support(float h, vec2 gradient, float cell) {
    float slope=length(gradient);
    // All eligibility comes from the continuous displayed field. A one-cell
    // linear depth estimate excludes isolated rain films without classifying
    // whole native quads or jumping when their minimum/maximum changes.
    return smoothstep(0.12,0.20,h+cell*slope)*smoothstep(1e-5,5e-5,slope);
}
float fluid25d_bank_noise(vec2 p, float scale) {
    vec2 q=vec2(0.8*p.x+0.6*p.y,-0.6*p.x+0.8*p.y)/scale;
    return 0.75*cubey_proc_value_noise_pcg_2d(q+vec2(13.7,41.2))+
           0.25*cubey_proc_value_noise_pcg_2d(q*2.17+vec2(-27.3,9.1));
}
float fluid25d_bank_moving_noise(vec2 p, vec2 velocity, float clock, float scale) {
    float dephase=cubey_proc_value_noise_pcg_2d(p/800.0+vec2(6.7,-11.2));
    vec4 phases=fluid25d_rapid_phases(clock,dephase);
    vec2 flow=velocity/max(1.0,length(velocity)/8.0);
    float a=fluid25d_bank_noise(p-flow*(phases.x-0.5)*8.0,scale);
    float b=fluid25d_bank_noise(p-flow*(phases.y-0.5)*8.0+vec2(117.7,241.3),scale);
    return dot(vec2(a,b),phases.zw);
}
// Inputs are physical metres; result.x is the bounded water-side inset and
// result.y the localized band for inspection. No horizontal terrain movement.
vec2 fluid25d_bank_inset(vec2 p, float h, vec2 gradient,
                       vec2 velocity, float clock, float footprint,
                       float cell, float wet, vec4 controls) {
    float slope=length(gradient);
    float support=fluid25d_bank_support(h,gradient,cell);
    if (h<=wet || slope<=1e-5 || support<=0.0) return vec2(0);
    float distance=(h-wet)/slope;
    float band=1.0-smoothstep(0.5*controls.w,controls.w,distance);
    // Small unresolved features disappear instead of turning into edge shimmer.
    float resolved=1.0-smoothstep(0.125*controls.z,0.35*controls.z,footprint);
    float weight=support*band*resolved;
    if (weight<=0.0) return vec2(0);
    float supported_depth=h+cell*slope;
    // Higher opt-in requests progressively relax the old quiet cap, up to
    // 45% of a cell / 60% of local supported depth. Keep the old <=8/3 m
    // strength scaling. At large doses preserve noise instead of clipping it
    // into an almost uniform inset. This is visual retreat, never erosion.
    float exaggeration=clamp(max(controls.x/8.0,controls.y/3.0)-1.0,0.0,2.0);
    float cap=min(0.15*(1.0+exaggeration)*cell,
                  0.20*(1.0+exaggeration)*max(supported_depth-wet,0.0)/slope);
    float reshape=smoothstep(0.0,1.0,exaggeration);
    float fixed_amplitude=mix(controls.x,min(controls.x,cap),reshape);
    float moving_amplitude=mix(controls.y,min(controls.y,cap),reshape);
    float inset=fixed_amplitude*(0.25+0.75*fluid25d_bank_noise(p,controls.z));
    float activity=smoothstep(0.05,1.5,length(velocity));
    if (controls.y>0.0 && activity>0.0)
        inset+=moving_amplitude*activity*(2.0*fluid25d_bank_moving_noise(p,velocity,clock,controls.z)-1.0);
    return vec2(clamp(inset,0.0,cap)*weight,weight);
}
#endif
