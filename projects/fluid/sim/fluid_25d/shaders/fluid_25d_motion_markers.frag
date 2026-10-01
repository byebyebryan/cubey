#version 450
#extension GL_GOOGLE_include_directive : require
#include "cubey/color_space.glsl"
layout(location=0) in vec2 local_position;
layout(location=1) in float opacity;
layout(location=2) flat in uint dot_shape;
layout(location=0) out vec4 out_color;
void main() {
    float shape=dot_shape==1u ? 1.0-smoothstep(0.45,1.0,length(local_position))
                             : 1.0-smoothstep(0.25,1.0,abs(local_position.y));
    float alpha=opacity*shape;
    if(alpha<0.005)discard;
    vec3 color=cubey_srgb_to_linear(vec3(1.0,0.96,0.68));
    out_color=vec4(color*alpha,alpha);
}
