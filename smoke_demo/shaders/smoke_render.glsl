
__CS_GAUSSIAN_BLUR__

#define LOCAL_WORKGROUP_SIZE_X 8
#define LOCAL_WORKGROUP_SIZE_Y 8
#define LOCAL_WORKGROUP_SIZE_Z 8

layout (local_size_x = LOCAL_WORKGROUP_SIZE_X, local_size_y = LOCAL_WORKGROUP_SIZE_Y, local_size_z = LOCAL_WORKGROUP_SIZE_Z) in;

layout (binding = 0, r16f) uniform image3D i_source;
layout (binding = 1, r16f) uniform image3D i_target;

uniform int u_pass_num;
uniform float u_weights[5] = float[](0.05399096651318985, 0.24197072451914536, 0.3989422804014327, 0.24197072451914536, 0.05399096651318985);

uniform ivec3 u_offset[15] = ivec3[](
	ivec3(-2,0,0), ivec3(-1,0,0), ivec3(0,0,0), ivec3(1,0,0), ivec3(2,0,0),
	ivec3(0,-2,0), ivec3(0,-1,0), ivec3(0,0,0), ivec3(0,1,0), ivec3(0,2,0),
	ivec3(0,0,-2), ivec3(0,0,-1), ivec3(0,0,0), ivec3(0,0,1), ivec3(0,0,2)
);

ivec3 clamp_i (ivec3 i_in) {
	return clamp(i_in, ivec3(0), ivec3(gl_NumWorkGroups * gl_WorkGroupSize) - ivec3(1));
}

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);

	float blur = 0;
	for (int i=0; i<5; i++) {
		blur += imageLoad(i_source, clamp_i(pos + u_offset[u_pass_num*5 + i])).x * u_weights[i];
	}

	imageStore(i_target, pos, vec4(blur));
}


__CS_SHADOW__

#define LOCAL_WORKGROUP_SIZE_X 8
#define LOCAL_WORKGROUP_SIZE_Y 8
#define LOCAL_WORKGROUP_SIZE_Z 8

layout (local_size_x = LOCAL_WORKGROUP_SIZE_X, local_size_y = LOCAL_WORKGROUP_SIZE_Y, local_size_z = LOCAL_WORKGROUP_SIZE_Z) in;

layout (binding = 0) uniform sampler3D t_density;
layout (binding = 1) uniform sampler3D t_obstacle;

layout (binding = 0, r16f) uniform image3D i_shadow;

uniform float u_step_size = 1.0/128;
uniform int u_max_steps = 1000;
uniform float u_density_factor = 10.0;

uniform vec3 u_light_position = vec3(1.5,1.5,1.5);

uniform float u_absorption = 100.0;

uniform float u_jittering = 0.5;

float rand_float(vec3 v_in) {
    return fract(sin(dot(v_in ,vec3(12.9898, 78.233, 56.8346))) * 43758.5453);
}

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	
	vec3 coord = vec3(pos / vec3(gl_NumWorkGroups * gl_WorkGroupSize));
	
	vec3 light_dir = normalize(u_light_position - coord);
	float acc_light = 1.0;
	vec3 light_tracing_coord = coord + mix(-u_jittering/2, u_jittering, rand_float(coord)) * light_dir * u_step_size;
	
	for (int j=0; j< u_max_steps; j++) {
		light_tracing_coord += light_dir * u_step_size;
		if (light_tracing_coord.x < 0 || light_tracing_coord.y < 0 || light_tracing_coord.z < 0 ) break;
		if (light_tracing_coord.x > 1 || light_tracing_coord.y > 1 || light_tracing_coord.z > 1 ) break;
		
		float ob = texture(t_obstacle, light_tracing_coord).x;
		if (ob >0.5) {
			imageStore(i_shadow, pos, vec4(0));
			return;
		}
		
		float ld = texture(t_density, light_tracing_coord).x * u_density_factor;
		if (ld  > 0) {
			acc_light *= exp( - ld * u_step_size * u_absorption);
		}			//continue;

		if (acc_light <= 0.01) break;
	}
	
	imageStore(i_shadow, pos, vec4(acc_light));
}

__VS__

layout(location = 0) in vec4 in_vertex_position;

void main() {
	gl_Position = in_vertex_position;
}

__FS__

layout (binding = 0) uniform sampler3D t_density;
layout (binding = 1) uniform sampler3D t_shadow;
layout (binding = 2) uniform sampler3D t_obstacle;

uniform float u_step_size = 1.0/128;
uniform int u_max_steps = 1000;
uniform float u_density_factor = 10.0;

uniform vec3 u_smoke_color = vec3(1.0, 0.5, 0.0);

uniform vec3 u_light_color = vec3(1.0);
uniform float u_light_intensity = 100.0;
uniform float u_absorption = 100.0;

uniform float u_jittering = 0.5;

uniform float u_ambient = 1.0f;

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

float rand_float(vec3 v_in) {
    return fract(sin(dot(v_in ,vec3(12.9898, 78.233, 56.8346))) * 43758.5453);
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
	
	vec3 tracing_coord = entry_coord + mix(-u_jittering/2.0, u_jittering/2.0, rand_float(entry_coord)) * u_step_size * local_dir;

	float acc_alpha = 1.0;
	vec3 acc_color = vec3(0);

	for(int i=0; i< u_max_steps; i++) {

		tracing_coord += local_dir * u_step_size;
		if (tracing_coord.x < 0 || tracing_coord.y < 0 || tracing_coord.z < 0 ) break;
		if (tracing_coord.x > 1 || tracing_coord.y > 1 || tracing_coord.z > 1 ) break;

		float density = texture(t_density, tracing_coord).x * u_density_factor;
		
		float acc_light = texture(t_shadow, tracing_coord).x;
		
		if (density <= 0) {
			float ob = texture(t_obstacle, tracing_coord).x;
			if (ob >0.5) {
				acc_color += vec3(0.0, 0.5, 1.0) * u_light_color * u_light_intensity * acc_light * acc_alpha * u_step_size;
				acc_alpha = 0;
				break;
			}
			
			
			acc_color += u_light_color * u_light_intensity * acc_light * acc_alpha * 0.01 * u_step_size * u_ambient;
			acc_alpha *= 1.0 - u_step_size*u_ambient;
		}
		else {
			
			acc_color += u_smoke_color * u_light_color * u_light_intensity * acc_light * acc_alpha * density * u_step_size;
			acc_alpha *= exp( - density * u_step_size * u_absorption);
		}
		
		
		if (acc_alpha <= 0.01) break;

	}
	out_color = vec4(acc_color, 1 - acc_alpha);
}
