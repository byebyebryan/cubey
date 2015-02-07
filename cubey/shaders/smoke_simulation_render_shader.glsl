#version 430

#define LOCAL_WORKGROUP_SIZE_X 8
#define LOCAL_WORKGROUP_SIZE_Y 8
#define LOCAL_WORKGROUP_SIZE_Z 8

#define GLOBAL_SIZE_X 64
#define GLOBAL_SIZE_Y 64
#define GLOBAL_SIZE_Z 64

#ifdef _VERTEX_INIT_

layout (local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

layout (std430, binding=0) buffer vert_data {
	vec3 positions[GLOBAL_SIZE_X * GLOBAL_SIZE_Y * GLOBAL_SIZE_Z];
};

void main() {
	vec3 p = vec3(gl_GlobalInvocationID);
	p = p / 10.0;
	uint id = gl_GlobalInvocationID.x * GLOBAL_SIZE_Y * GLOBAL_SIZE_Z + gl_GlobalInvocationID.y * GLOBAL_SIZE_Z + gl_GlobalInvocationID.z;
	positions[id] = p;

	//memoryBarrier();
}

#endif

#ifdef _VERTEX_S_

layout(location = 0) in vec3 in_vertex_position;

uniform mat4 u_mvp_mat;
uniform sampler3D u_temp_dens;

out vec4 var_color;

const vec4 hot_color = vec4(1.0, 0.0, 0.0, 1.0);
const vec4 cold_color = vec4(0.1, 0.1, 0.1, 0.05);

void main() {
	
	gl_Position = u_mvp_mat * vec4(in_vertex_position, 1);
	
	vec4 value = texelFetch(u_temp_dens, ivec3(in_vertex_position * 10.0), 0);

	var_color = mix(cold_color, hot_color, value.x * 10);
	//var_color = vec4(value.xyz, 1);
}

#endif

#ifdef _FRAGMENT_S_

in vec4 var_color;

out vec4 out_color;

void main() {
	out_color = var_color;
}

#endif