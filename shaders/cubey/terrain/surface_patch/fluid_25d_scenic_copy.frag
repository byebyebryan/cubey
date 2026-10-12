#version 450
// Shared paired terrain/water renderer; retained names preserve shader ABI.
#extension GL_GOOGLE_include_directive : require
#include "fluid_25d_scenic.glsl"
layout(location=0) out vec4 out_color;
void main() { out_color = texture(scenic_opaque,gl_FragCoord.xy/scenic.clock_encoding.zw); }
