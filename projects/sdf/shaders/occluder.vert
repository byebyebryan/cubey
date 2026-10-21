#version 450
layout(push_constant) uniform Camera {
    mat4 inverseVP; vec4 eye; vec4 sunlight; vec4 options; vec4 forward;
} camera;
void main() {
    const vec2 vertices[6]=vec2[6](vec2(-1,-1),vec2(1,-1),vec2(-1,1),vec2(-1,1),vec2(1,-1),vec2(1,1));
    vec2 p=vertices[gl_VertexIndex]*vec2(0.42,0.11);
    gl_Position=inverse(camera.inverseVP)*vec4(p,p.x*2.0,1.0);
}
