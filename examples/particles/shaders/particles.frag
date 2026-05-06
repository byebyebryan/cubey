#version 450

layout(location = 0) in vec2 frag_uv;
layout(location = 1) in vec4 frag_color;

layout(location = 0) out vec4 out_color;

void main() {
    float distance2 = dot(frag_uv, frag_uv);
    float alpha = exp(-distance2 * 3.6) * frag_color.a;
    if (alpha < 0.004) {
        discard;
    }

    out_color = vec4(frag_color.rgb * alpha, alpha);
}
