#version 430

#ifdef _VERTEX_S_

uniform mat4 u_mvp_mat;
uniform float lifespan;

layout(location = 0) in vec4 in_vertex_position;

out vec4 var_color;

const vec4 hot = vec4(1, 0.2, 0, 0.5);
const vec4 cold = vec4(0, 0, 1, 0.5);

void main() {
	gl_Position = u_mvp_mat * vec4(in_vertex_position.xyz, 1);
	var_color = mix(cold, hot, in_vertex_position.w / lifespan );
}

#endif

#ifdef _FRAGMENT_S_

in vec4 var_color;

out vec4 out_color;

void main() {
	out_color = var_color;
}

#endif