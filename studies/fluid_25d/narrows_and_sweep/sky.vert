#version 330 core

out vec2 ndc;

void main() {
    const vec2 corners[3] = vec2[](vec2(-1, -1), vec2(3, -1), vec2(-1, 3));
    ndc = corners[gl_VertexID];
    gl_Position = vec4(ndc, 0, 1);
}
