#version 430 core

uniform Camera {
	mat4 u_projection_mat;
	mat4 u_view_mat;
};

uniform mat4 u_model_mat;

#ifdef _VERTEX_S_

in vec4 in_vertex_position;
in vec4 in_vertex_color;

out vec4 var_color;

void main()
{
	gl_Position = u_projection_mat * u_view_mat * u_model_mat * in_vertex_position;
	var_color = in_vertex_color;
}

#endif

#ifdef _FRAGMENT_S_

in vec4 var_color;

out vec4 out_color;

void main()
{
	out_color = var_color;
}

#endif
