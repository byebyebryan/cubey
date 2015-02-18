
#define LOCAL_WORKGROUP_SIZE_X 8
#define LOCAL_WORKGROUP_SIZE_Y 8
#define LOCAL_WORKGROUP_SIZE_Z 8

layout (local_size_x = LOCAL_WORKGROUP_SIZE_X, local_size_y = LOCAL_WORKGROUP_SIZE_Y, local_size_z = LOCAL_WORKGROUP_SIZE_Z) in;

__CS_INJECTION_SPLAT_V__

layout (binding = 0) uniform sampler3D t_source;

layout (binding = 0, rgba16f) uniform image3D i_target;

uniform float u_time_step;
uniform vec3 u_location;
uniform float u_sigma;
uniform float u_intensity;

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	vec3 coord = vec3(pos) / vec3(gl_NumWorkGroups * gl_WorkGroupSize - 1);
	float dist_sq = dot(u_location - coord, u_location - coord) * gl_NumWorkGroups.y * gl_WorkGroupSize.y* gl_NumWorkGroups.y * gl_WorkGroupSize.y;
	float gaussian = exp( - dist_sq / 2 * u_sigma * u_sigma) / u_sigma;
	vec3 dir = normalize(coord - u_location);
	
	vec4 source_value = texelFetch(t_source, pos, 0);
	vec4 target_value = source_value + u_intensity * gaussian * u_time_step * vec4(dir,0);
	imageStore(i_target, pos, target_value);
}

__CS_INJECTION_SPLAT_V_LINEAR__

layout (binding = 0) uniform sampler3D t_source;

layout (binding = 0, rgba16f) uniform image3D i_target;

uniform float u_time_step;
uniform vec3 u_location;
uniform float u_sigma;
uniform float u_intensity;
uniform vec3 u_direction;

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	vec3 coord = vec3(pos) / vec3(gl_NumWorkGroups * gl_WorkGroupSize - 1);
	float dist_sq = dot(u_location - coord, u_location - coord)  * gl_NumWorkGroups.y * gl_WorkGroupSize.y* gl_NumWorkGroups.y * gl_WorkGroupSize.y;
	float gaussian = exp( - dist_sq / 2 * u_sigma * u_sigma) / u_sigma;
	
	vec4 source_value = texelFetch(t_source, pos, 0);
	vec4 target_value = source_value + u_intensity * gaussian * u_time_step * vec4(normalize(u_direction),0);
	imageStore(i_target, pos, target_value);
}

__CS_INJECTION_SPLAT_RGBA__

layout (binding = 0) uniform sampler3D t_source;

layout (binding = 0, rgba16f) uniform image3D i_target;

uniform float u_time_step;
uniform vec3 u_location;
uniform float u_sigma;
uniform vec4 u_intensity;

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	vec3 coord = vec3(pos) / vec3(gl_NumWorkGroups * gl_WorkGroupSize - 1);
	float dist_sq = dot(u_location - coord, u_location - coord)  * gl_NumWorkGroups.y * gl_WorkGroupSize.y* gl_NumWorkGroups.y * gl_WorkGroupSize.y;
	float gaussian = exp( - dist_sq / 2 * u_sigma * u_sigma) / u_sigma;
	
	vec4 source_value = texelFetch(t_source, pos, 0);
	vec4 target_value = source_value + u_intensity * gaussian * u_time_step;
	imageStore(i_target, pos, target_value);
}

__CS_INJECTION_SPLAT_R__

layout (binding = 0) uniform sampler3D t_source;

layout (binding = 0, r16f) uniform image3D i_target;

uniform float u_time_step;
uniform vec3 u_location;
uniform float u_sigma;
uniform float u_intensity;

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	vec3 coord = vec3(pos) / vec3(gl_NumWorkGroups * gl_WorkGroupSize - 1);
	float dist_sq = dot(u_location - coord, u_location - coord)  * gl_NumWorkGroups.y * gl_WorkGroupSize.y* gl_NumWorkGroups.y * gl_WorkGroupSize.y;
	float gaussian = exp( - dist_sq / 2 * u_sigma * u_sigma) / u_sigma;
	
	vec4 source_value = texelFetch(t_source, pos, 0);
	vec4 target_value = source_value + u_intensity * gaussian * u_time_step;
	imageStore(i_target, pos, target_value);
}


__CS_BUOYANCY__

layout (binding = 0) uniform sampler3D t_velocity;
layout (binding = 1) uniform sampler3D t_temperature;
layout (binding = 2) uniform sampler3D t_density;

layout (binding = 0, rgba16f) uniform image3D i_velocity_target;

uniform float u_ambient_temperature;
uniform float u_time_step;
uniform float u_buoyancy;
uniform float u_weight;

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	vec4 vel = texelFetch(t_velocity, pos, 0);
	float temp = texelFetch(t_temperature, pos, 0).x;
	
	vec4 d = texelFetch(t_density, pos, 0);
	float dens = d.x + d.y + d.z + d.w;
	vel += u_time_step * ((temp - u_ambient_temperature) * u_buoyancy - dens * u_weight) * vec4(0,1,0,0);

	imageStore(i_velocity_target, pos, vel);
}

__CS_VORTICITY__

layout (binding = 0) uniform sampler3D t_velocity;

layout (binding = 0, rgba16f) uniform image3D i_vorticity;

ivec3 clamp_i (ivec3 i_in) {
	ivec3 res;
	res.x = clamp(i_in.x, 0, int(gl_NumWorkGroups.x * gl_WorkGroupSize.x) -1);
	res.y = clamp(i_in.y, 0, int(gl_NumWorkGroups.y * gl_WorkGroupSize.y) -1);
	res.z = clamp(i_in.z, 0, int(gl_NumWorkGroups.z * gl_WorkGroupSize.z) -1);
	return res;
}

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	
	vec4 v_r = texelFetch(t_velocity, clamp_i(pos +ivec3(1, 0, 0)), 0);
	vec4 v_l = texelFetch(t_velocity, clamp_i(pos +ivec3(-1, 0, 0)), 0);
	vec4 v_u = texelFetch(t_velocity, clamp_i(pos +ivec3(0, 1, 0)), 0);
	vec4 v_d = texelFetch(t_velocity, clamp_i(pos +ivec3(0, -1, 0)), 0);
	vec4 v_f = texelFetch(t_velocity, clamp_i(pos +ivec3(0, 0, 1)), 0);
	vec4 v_b = texelFetch(t_velocity, clamp_i(pos +ivec3(0, 0, -1)), 0);
	
	//vec3 vor = 0.5 * vec3((v_f.z - v_b.z) - (v_u.y - v_d.y), 
	//					  (v_u.x - v_d.x) - (v_r.z - v_l.z), 
	//					  (v_r.y - v_l.y) - (v_f.x - v_b.x));
						  
	vec3 vor = 0.5 * vec3((v_u.z - v_d.z) - (v_f.y - v_b.y), 
						  (v_f.x - v_b.x) - (v_r.z - v_l.z), 
						  (v_r.y - v_l.y) - (v_u.x - v_d.x));
						  
	imageStore(i_vorticity, pos, vec4(vor, 0));
}

__CS_CONFINEMENT__

layout (binding = 0) uniform sampler3D t_velocity;
layout (binding = 1) uniform sampler3D t_vorticity;

layout (binding = 0, rgba16f) uniform image3D i_velocity_target;

uniform float u_time_step;
uniform float u_vorticity_strength;

ivec3 clamp_i (ivec3 i_in) {
	ivec3 res;
	res.x = clamp(i_in.x, 0, int(gl_NumWorkGroups.x * gl_WorkGroupSize.x) -1);
	res.y = clamp(i_in.y, 0, int(gl_NumWorkGroups.y * gl_WorkGroupSize.y) -1);
	res.z = clamp(i_in.z, 0, int(gl_NumWorkGroups.z * gl_WorkGroupSize.z) -1);
	return res;
}

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	
	vec3 v_r = texelFetch(t_vorticity, clamp_i(pos +ivec3(1, 0, 0)), 0).xyz;
	vec3 v_l = texelFetch(t_vorticity, clamp_i(pos +ivec3(-1, 0, 0)), 0).xyz;
	vec3 v_u = texelFetch(t_vorticity, clamp_i(pos +ivec3(0, 1, 0)), 0).xyz;
	vec3 v_d = texelFetch(t_vorticity, clamp_i(pos +ivec3(0, -1, 0)), 0).xyz;
	vec3 v_f = texelFetch(t_vorticity, clamp_i(pos +ivec3(0, 0, 1)), 0).xyz;
	vec3 v_b = texelFetch(t_vorticity, clamp_i(pos +ivec3(0, 0, -1)), 0).xyz;
	
	vec3 vor = texelFetch(t_vorticity, pos, 0).xyz;
	vec3 eta = 0.5 * vec3(length(v_r) - length(v_l), length(v_u) - length(v_d), length(v_f) - length(v_b));
	eta = normalize(eta + vec3(0.001));
	
	vec3 dv = u_time_step * u_vorticity_strength * vec3(eta.y * vor.z - eta.z * vor.y, eta.z * vor.x - eta.x * vor.z, eta.x * vor.y - eta.y * vor.x);
	
	vec3 vel = texelFetch(t_velocity, pos, 0).xyz;
	vel += dv;
	imageStore(i_velocity_target, pos, vec4(vel, 0));
}

