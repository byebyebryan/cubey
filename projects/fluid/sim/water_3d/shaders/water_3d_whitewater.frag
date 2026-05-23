#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec2 frag_local;
layout(location = 1) in float frag_kind;
layout(location = 2) in float frag_age;
layout(location = 3) in float frag_energy;
layout(location = 4) in float frag_linear_depth;
layout(location = 0) out vec4 out_color;

void main() {
    float radius = length(frag_local);
    if (radius > 1.0) {
        discard;
    }

    float core = smoothstep(1.0, 0.16, radius);
    float feather = smoothstep(1.0, 0.74, radius);
    float life = smoothstep(0.0, 0.10, frag_age) * (1.0 - smoothstep(0.68, 1.0, frag_age));
    float spray = step(0.5, frag_kind) * (1.0 - step(1.5, frag_kind));
    float bubble = step(1.5, frag_kind);
    float alpha = mix(0.10, 0.18, spray) * mix(feather, core, spray * 0.25) * life;
    alpha *= mix(0.65, 1.00, clamp(frag_energy, 0.0, 1.0));
    alpha *= mix(1.0, 0.55, bubble);

    float foam_weight = mix(1.0, 0.62, spray);
    foam_weight = mix(foam_weight, 0.42, bubble);
    out_color = vec4(alpha * foam_weight, alpha * frag_kind, alpha * frag_linear_depth, alpha);
}
