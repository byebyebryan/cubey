#version 330 core

uniform sampler2DArray sky;
uniform mat4 clip_to_sky;
uniform float clock_progress;
in vec2 ndc;
out vec4 out_color;

void main() {
    vec4 projected = clip_to_sky * vec4(ndc, 1, 1);
    // The baked plate is stored top to bottom. OpenGL NDC has Y up.
    vec2 uv = projected.xy / projected.w * vec2(0.5, -0.5) + 0.5;
    float phase = clock_progress * 2.0;
    float before = min(floor(phase), 1.0);
    // The sRGB texture decodes the captures before blending adjacent times.
    vec3 linear = mix(texture(sky, vec3(uv, before)).rgb,
                      texture(sky, vec3(uv, before + 1.0)).rgb, phase - before);
    vec3 encoded = mix(12.92 * linear, 1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055,
                       step(0.0031308, linear));
    out_color = vec4(encoded, 1.0);
}
