
#define LOCAL_WORKGROUP_SIZE_X 8
#define LOCAL_WORKGROUP_SIZE_Y 8
#define LOCAL_WORKGROUP_SIZE_Z 8

layout (local_size_x = LOCAL_WORKGROUP_SIZE_X, local_size_y = LOCAL_WORKGROUP_SIZE_Y, local_size_z = LOCAL_WORKGROUP_SIZE_Z) in;

__CS_GAUSSIAN_BLUR_R__

layout (binding = 0) uniform sampler3D t_source;

layout (binding = 0, r16f) uniform image3D i_target;

uniform int u_pass_num;
uniform float u_weights[5] = float[](0.05399096651318985, 0.24197072451914536, 0.3989422804014327, 0.24197072451914536, 0.05399096651318985);

uniform ivec3 u_offset[15] = ivec3[](
	ivec3(-2,0,0), ivec3(-1,0,0), ivec3(0,0,0), ivec3(1,0,0), ivec3(2,0,0),
	ivec3(0,-2,0), ivec3(0,-1,0), ivec3(0,0,0), ivec3(0,1,0), ivec3(0,2,0),
	ivec3(0,0,-2), ivec3(0,0,-1), ivec3(0,0,0), ivec3(0,0,1), ivec3(0,0,2)
);

ivec3 clamp_i (ivec3 i_in) {
	ivec3 res;
	res.x = clamp(i_in.x, 0, int(gl_NumWorkGroups.x * gl_WorkGroupSize.x) -1);
	res.y = clamp(i_in.y, 0, int(gl_NumWorkGroups.y * gl_WorkGroupSize.y) -1);
	res.z = clamp(i_in.z, 0, int(gl_NumWorkGroups.z * gl_WorkGroupSize.z) -1);
	return res;
}

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);

	vec4 blur = vec4(0);
	for (int i=0; i<5; i++) {
		blur += texelFetch(t_source, clamp_i(pos + u_offset[u_pass_num*5 + i]), 0) * u_weights[i];
	}

	imageStore(i_target, pos, blur);
}

__CS_GAUSSIAN_BLUR_RGBA__

layout (binding = 0) uniform sampler3D t_source;

layout (binding = 0, rgba16f) uniform image3D i_target;

uniform int u_pass_num;
uniform float u_weights[5] = float[](0.05399096651318985, 0.24197072451914536, 0.3989422804014327, 0.24197072451914536, 0.05399096651318985);

uniform ivec3 u_offset[15] = ivec3[](
	ivec3(-2,0,0), ivec3(-1,0,0), ivec3(0,0,0), ivec3(1,0,0), ivec3(2,0,0),
	ivec3(0,-2,0), ivec3(0,-1,0), ivec3(0,0,0), ivec3(0,1,0), ivec3(0,2,0),
	ivec3(0,0,-2), ivec3(0,0,-1), ivec3(0,0,0), ivec3(0,0,1), ivec3(0,0,2)
);

ivec3 clamp_i (ivec3 i_in) {
	ivec3 res;
	res.x = clamp(i_in.x, 0, int(gl_NumWorkGroups.x * gl_WorkGroupSize.x) -1);
	res.y = clamp(i_in.y, 0, int(gl_NumWorkGroups.y * gl_WorkGroupSize.y) -1);
	res.z = clamp(i_in.z, 0, int(gl_NumWorkGroups.z * gl_WorkGroupSize.z) -1);
	return res;
}

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);

	vec4 blur = vec4(0);
	for (int i=0; i<5; i++) {
		blur += texelFetch(t_source, clamp_i(pos + u_offset[u_pass_num*5 + i]), 0) * u_weights[i];
	}

	imageStore(i_target, pos, blur);
}

__CS_SHADOW__

layout (binding = 0) uniform sampler3D t_density;
layout (binding = 1) uniform sampler3D t_obstacle;

layout (binding = 0, r16f) uniform image3D i_shadow;

uniform float u_step_size = 1.0/128;
uniform int u_max_steps = 1000;
uniform float u_density_factor = 10.0;

uniform vec3 u_light_position = vec3(1.5,1.0,1.5);

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
		
		vec4 d = texture(t_density, light_tracing_coord) * u_density_factor;
		float ad = d.x + d.y + d.z + d.w;
		
		if (ad  > 0) {
			acc_light *= exp( - ad * u_step_size * u_absorption);
		}			//continue;

		if (acc_light <= 0.01) break;
	}
	
	imageStore(i_shadow, pos, vec4(acc_light));
}

