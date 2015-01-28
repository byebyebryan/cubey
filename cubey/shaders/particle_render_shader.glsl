#version 430

#ifdef _VERTEX_S_

uniform mat4 u_mvp_mat;
uniform float u_particle_lifespan;

uniform vec4 u_particle_color_cold;
uniform vec4 u_particle_color_hot;

layout(location = 0) in vec4 in_vertex_position;

out vec4 var_color;

void main() {
	gl_Position = u_mvp_mat * vec4(in_vertex_position.xyz, 1);
	var_color = mix(u_particle_color_cold, u_particle_color_hot, in_vertex_position.w / u_particle_lifespan );
}

#endif

#ifdef _FRAGMENT_S_

in vec4 var_color;

out vec4 out_color;

void main() {
	out_color = var_color;
}

#endif