#version 450
#extension GL_GOOGLE_include_directive : require
#include "fluid_25d_scenic.glsl"
#include "cubey/atmosphere/sky_background_direction.glsl"
layout(location=0) out vec4 out_color;
void main() {
    vec2 uv = gl_FragCoord.xy/scenic.clock_encoding.zw;
    vec3 ray = normalize(scenic_unproject(uv,0.9)-scenic_unproject(uv,0.0));
    if (scenic.environment_mode.x>0.5)
        ray=sky_background_sample_direction(ray,vec3(0,1,0),vec3(0));
    out_color = vec4(textureLod(scenic_environment,ray,0.0).rgb,1.0);
}
