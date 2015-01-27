#version 430

#ifdef _VERTEX_S_

uniform mat4 u_mvp_mat;
uniform mat3 u_normal_mat;

layout(location = 0) in vec4 in_vertex_position;
layout(location = 1) in vec4 in_vertex_color;
layout(location = 2) in vec3 in_vertex_normal;

out vec4 var_color;
out vec3 var_normal;

void main() {
	gl_Position = u_mvp_mat * in_vertex_position;
	var_color = in_vertex_color;
	var_normal = normalize(u_normal_mat * in_vertex_normal);
}

#endif

#ifdef _FRAGMENT_S_

uniform vec3 u_ambient_light_color;
uniform vec3 u_directional_light_direction;
uniform vec3 u_directional_light_color;

in vec4 var_color;
in vec3 var_normal;

out vec4 out_color;

void main() {
	float diffuse = max(0.0, dot(var_normal, u_directional_light_direction));
	vec3 scattered_light = u_ambient_light_color + diffuse * u_directional_light_color;
	vec3 rgb = min(vec3(1.0), var_color.rgb * scattered_light);

	out_color = vec4(rgb, var_color.a);
}

#endif
