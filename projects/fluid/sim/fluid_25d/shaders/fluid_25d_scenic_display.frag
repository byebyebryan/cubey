#version 450
#extension GL_GOOGLE_include_directive : require
#include "cubey/pbr.glsl"
#include "fluid_25d_scenic.glsl"
layout(location=0) out vec4 out_color;
void main() {
    vec3 hdr = texture(scenic_composed,gl_FragCoord.xy/scenic.clock_encoding.zw).rgb;
    out_color = vec4(cubey_pbr_apply_display_transform(hdr,
        vec4(scenic.light_direction_exposure.w,1.0,scenic.clock_encoding.y,0.0)),1.0);
}
