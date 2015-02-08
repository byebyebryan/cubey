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

//layout (binding = 0, rgba32f) uniform image2D i_entry_tex_coord;
//layout (binding = 1, rgba32f) uniform image2D i_exit_tex_coord;

layout (binding = 0) uniform sampler3D t_density;

uniform float u_step_size = 0.01;
uniform int u_max_steps = 150;
uniform float u_density_factor = 10.0;
uniform int u_max_light_steps = 32;

uniform vec3 u_light_position = vec3(3.0,3.0,3.0);
uniform vec3 u_light_intensity = vec3(100.0);
uniform float u_absorption = 100.0;

uniform vec2 u_viewport_size;
uniform mat4 u_inverse_mvp;

out vec4 out_color;

const float pi = 3.1415926535897932384626433832795;

struct Ray {
	vec3 origin;
	vec3 dir;
};

struct AABB {
	vec3 min_pos;
	vec3 max_pos;
};

vec2 IntersectBox(Ray r, AABB aabb) {
	vec3 t_min = (aabb.min_pos - r.origin) / r.dir;
	vec3 t_max = (aabb.max_pos - r.origin) / r.dir;
	vec3 t0 = min(t_min, t_max);
	vec3 t1 = max(t_min, t_max);
	float t_near = max(t0.x, max(t0.y, t0.z));
	float t_far = min(t1.x, min(t1.y, t1.z));
	return vec2(t_near, t_far);
}

void main() {
	vec4 screen_pos;
	screen_pos.xy = 2.0 * gl_FragCoord.xy / u_viewport_size - 1.0;
	screen_pos.z = - 1 / tan(pi * 30.0 / 180.0);
	screen_pos.x *= u_viewport_size.x / u_viewport_size.y;
	screen_pos.w = 0.0;
	
	vec3 local_dir = normalize((u_inverse_mvp * screen_pos).xyz);
	vec3 local_origin = (u_inverse_mvp * vec4(0,0,0,1)).xyz + vec3(0.5);
	
	Ray eye_ray = Ray(local_origin, local_dir);
	AABB aabb = AABB(vec3(0), vec3(1));
	
	vec2 t = IntersectBox(eye_ray, aabb);
	if(t.x >= t.y) discard;
	//else {out_color = vec4(0.5,0.5,0.5, 1.0);}
	
	if(t.x < 0) t.x = 0;
	vec3 entry_coord = local_origin + local_dir * t.x;
	vec3 exit_coord = local_origin + local_dir * t.y;
	
	vec3 tracing_coord = entry_coord;

	float acc_alpha = 1.0;
	vec3 acc_color = vec3(0.25);

	for(int i=0; i< u_max_steps; i++) {

		tracing_coord += local_dir * u_step_size;
		if (tracing_coord.x < 0 || tracing_coord.y < 0 || tracing_coord.z < 0 ) break;
		if (tracing_coord.x > 1 || tracing_coord.y > 1 || tracing_coord.z > 1 ) break;

		float density = texture(t_density, tracing_coord).x * u_density_factor;
		if (density <= 0) continue;

		acc_alpha *= 1.0 - clamp(density * u_step_size * u_absorption, 0, 0.9);
		if (acc_alpha <= 0.01) break;

		vec3 light_dir = normalize(u_light_position - tracing_coord);
		float acc_light = 1.0;
		vec3 light_tracing_coord = tracing_coord;

		for (int j=0; j< u_max_light_steps; j++) {
			light_tracing_coord += light_dir * u_step_size;
			if (light_tracing_coord.x < 0 || light_tracing_coord.y < 0 || light_tracing_coord.z < 0 ) break;
			if (light_tracing_coord.x > 1 || light_tracing_coord.y > 1 || light_tracing_coord.z > 1 ) break;

			float ld = texture(t_density, light_tracing_coord).x * u_density_factor;
			if (ld  > 0) {
				acc_light *= 1.0 - clamp(ld * u_step_size * u_absorption, 0, 0.9);
			}			//continue;

			if (acc_light <= 0.1) break;
		}

		acc_color += u_light_intensity * acc_light * acc_alpha * density * u_step_size;

	}
	out_color = vec4(acc_color, 1 - acc_alpha);
}

#endif