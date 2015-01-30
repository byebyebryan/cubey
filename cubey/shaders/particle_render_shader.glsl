#version 430

#ifdef _VERTEX_S_

uniform mat4 u_view_model_mat;

uniform float u_particle_lifespan;

uniform float u_particle_size;

uniform vec4 u_particle_color_cold;
uniform vec4 u_particle_color_hot;

layout(location = 0) in vec4 in_vertex_position;
//layout(location = 1) in float in_vertex_hue;

out float point_size;
out vec4 point_color;

vec3 hsv2rgb(vec3 c)
{
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main() {
	gl_Position = u_view_model_mat * vec4(in_vertex_position.xyz, 1);
	float ratio = in_vertex_position.w / u_particle_lifespan;
	point_color = mix(u_particle_color_cold, u_particle_color_hot, ratio );

	//vec3 hot = hsv2rgb(vec3(in_vertex_hue, 1, 1));
	//vec3 cold = hsv2rgb(vec3(fract(in_vertex_hue+0.5), 1, 1));
	//point_color = vec4 (hot, 0.1);

	point_size = u_particle_size * ratio;
}

#endif

#ifdef _GEOMETRY_S_

uniform mat4 u_projection_mat;

layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

in vec4 point_color[];
in float point_size[];

out vec4 var_color;
out vec2 var_uv;

void main() {
	vec4 pos = gl_in[0].gl_Position;

	vec2 right = vec2(0.5,0) * point_size[0];
	vec2 up = vec2(0,0.5) * point_size[0];

	gl_Position = u_projection_mat * vec4(pos.xy - right - up, pos.zw);
	var_color = point_color[0];
	var_uv = vec2(0, 0);
	EmitVertex();

	gl_Position = u_projection_mat * vec4(pos.xy + right - up, pos.zw);
	var_color = point_color[0];
	var_uv = vec2(1, 0);
	EmitVertex();

	gl_Position = u_projection_mat * vec4(pos.xy - right + up, pos.zw);
	var_color = point_color[0];
	var_uv = vec2(0, 1);
	EmitVertex();

	gl_Position = u_projection_mat * vec4(pos.xy + right + up, pos.zw);
	var_color = point_color[0];
	var_uv = vec2(1, 1);
	EmitVertex();
}

#endif

#ifdef _FRAGMENT_S_

uniform sampler2D u_particle_texture;

in vec4 var_color;
in vec2 var_uv;

out vec4 out_color;

void main() {
	out_color = var_color;
	out_color.a *= texture(u_particle_texture, var_uv).r;
}

#endif