#version 430

#ifdef _FIRST_PASS_VERTEX_S_

layout(location = 0) in vec4 in_vertex_position;
layout(location = 1) in vec3 in_vertex_tex_coord;

uniform mat4 u_mvp_mat;

out vec3 var_vertex_tex_coord;

void main() {
	gl_Position = u_mvp_mat * in_vertex_position;
	var_vertex_tex_coord = in_vertex_tex_coord;
}

#endif

#ifdef _FIRST_PASS_FRAGMENT_S_

in vec3 var_vertex_tex_coord;

out vec4 out_color;

void main() {
	out_color = vec4(var_vertex_tex_coord, 1);
}

#endif

#ifdef _SECOND_PASS_VERTEX_S_

layout(location = 0) in vec4 in_vertex_position;

void main() {
	gl_Position = in_vertex_position;
}

#endif

#ifdef _SECOND_PASS_FRAGMENT_S_

layout (binding = 0, rgba32f) uniform image2D i_entry_tex_coord;
layout (binding = 1, rgba32f) uniform image2D i_exit_tex_coord;

layout (binding = 0) uniform sampler3D t_density;

uniform float u_step_size;
uniform int u_max_steps;
uniform float u_density_factor;

const vec4 hot_color = vec4(0.8, 0.8, 0.8, 1.0);
const vec4 cold_color = vec4(0.1, 0.1, 0.1, 0.5);

out vec4 out_color;

void main() {
	ivec2 pos = ivec2(gl_FragCoord.xy);
	vec4 entry_tex_coord = imageLoad(i_entry_tex_coord, pos);
	vec3 tex_coord = entry_tex_coord.xyz;
	if (entry_tex_coord.w > 0) {
		vec4 exit_tex_coord = imageLoad(i_exit_tex_coord, pos);
		vec3 ray_dir = normalize(exit_tex_coord.xyz - entry_tex_coord.xyz);

		float d = 0;
		for(int i=0; i< u_max_steps; i++) {
			d += texture(t_density, tex_coord).x;
			tex_coord += ray_dir * u_step_size;
			if (tex_coord.x < 0 || tex_coord.y < 0 || tex_coord.z < 0 ) break;
			if (tex_coord.x > 1 || tex_coord.y > 1 || tex_coord.z > 1 ) break;
		}

		out_color = mix(cold_color, hot_color, d * u_density_factor);
	}
	else {
		discard;
	}
	//out_color = vec4(gl_FragCoord.x/ 1280.0, gl_FragCoord.y / 720.0, 0, 1);
}

#endif