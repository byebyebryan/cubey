#version 430

#ifdef _VERTEX_S_

uniform mat4 u_mvp_mat;

layout(location = 0) in vec4 in_vertex_position;
layout(location = 1) in vec4 in_vertex_color;

out vec4 var_color;

void main() {
	gl_Position = u_mvp_mat * in_vertex_position;
	var_color = in_vertex_color;
}

#endif

#ifdef _FRAGMENT_S_

in vec4 var_color;

out vec4 out_color;

void main() {
	out_color = var_color;
}

#endif
