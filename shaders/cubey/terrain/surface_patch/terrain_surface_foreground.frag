#version 450
// Merge opaque/masked linear-HDR geometry before water refraction. Input depth
// shares the landscape view pose/FOV; its perspective Z range can differ.
layout(set=0,binding=0) uniform sampler2D foreground_color;
layout(set=0,binding=1) uniform sampler2D foreground_depth;
layout(location=0) out vec4 out_color;
layout(push_constant) uniform DepthRemap { vec4 scale_bias; } remap;
void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    float depth = texelFetch(foreground_depth,pixel,0).r;
    if (depth >= 1.0) discard;
    gl_FragDepth = clamp(fma(depth,remap.scale_bias.x,remap.scale_bias.y),0.0,1.0);
    out_color = texelFetch(foreground_color,pixel,0);
}
