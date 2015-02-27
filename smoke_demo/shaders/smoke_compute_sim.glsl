
#define LOCAL_WORKGROUP_SIZE_X 8
#define LOCAL_WORKGROUP_SIZE_Y 8
#define LOCAL_WORKGROUP_SIZE_Z 8

layout (local_size_x = LOCAL_WORKGROUP_SIZE_X, local_size_y = LOCAL_WORKGROUP_SIZE_Y, local_size_z = LOCAL_WORKGROUP_SIZE_Z) in;

__CS_ADVECT_RGBA__

layout (binding = 0) uniform sampler3D t_velocity;
layout (binding = 1) uniform sampler3D t_obstacle;
layout (binding = 2) uniform sampler3D t_source;

layout (binding = 0, rgba16f) uniform image3D i_target;

uniform float u_time_step;
uniform float u_dissipation;

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	float obstacle = texelFetch(t_obstacle, pos, 0).r;
	vec4 target_value;
	if (obstacle > 0) {
		target_value = vec4(0);
	}
	else {
		vec3 vel = texelFetch(t_velocity, pos, 0).xyz;
		vec3 back_track_pos = pos - vel * u_time_step;
		vec3 back_track_coord = (back_track_pos + 0.5) / vec3(gl_NumWorkGroups * gl_WorkGroupSize);
		vec4 source_value = texture(t_source, back_track_coord);
		target_value = source_value * (1 - u_dissipation);
	}
	
	imageStore(i_target, pos, target_value);
}

__CS_ADVECT_R__

layout (binding = 0) uniform sampler3D t_velocity;
layout (binding = 1) uniform sampler3D t_obstacle;
layout (binding = 2) uniform sampler3D t_source;

layout (binding = 0, r16f) uniform image3D i_target;

uniform float u_time_step;
uniform float u_dissipation;

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	float obstacle = texelFetch(t_obstacle, pos, 0).r;
	vec4 target_value;
	if (obstacle > 0) {
		target_value = vec4(0);
	}
	else {
		vec3 vel = texelFetch(t_velocity, pos, 0).xyz;
		vec3 back_track_pos = vec3(pos) - vel * u_time_step;
		vec3 back_track_coord = (back_track_pos + 0.5) / vec3(gl_NumWorkGroups * gl_WorkGroupSize);
		vec4 source_value = texture(t_source, back_track_coord);
		target_value = source_value * (1 - u_dissipation);
	}
	
	imageStore(i_target, pos, target_value);
}

__CS_ADVECT_MAC_CORMACK_RGBA__

layout (binding = 0) uniform sampler3D t_velocity;
layout (binding = 1) uniform sampler3D t_obstacle;
layout (binding = 2) uniform sampler3D t_phi_n_1_hat;
layout (binding = 3) uniform sampler3D t_phi_n_hat;
layout (binding = 4) uniform sampler3D t_source;

layout (binding = 0, rgba16f) uniform image3D i_target;

uniform float u_time_step;
uniform float u_dissipation;
uniform float u_decay;

uniform vec4 u_minimum = vec4(0);

ivec3 clamp_i (ivec3 i_in) {
	ivec3 res;
	res.x = clamp(i_in.x, 0, int(gl_NumWorkGroups.x * gl_WorkGroupSize.x) -1);
	res.y = clamp(i_in.y, 0, int(gl_NumWorkGroups.y * gl_WorkGroupSize.y) -1);
	res.z = clamp(i_in.z, 0, int(gl_NumWorkGroups.z * gl_WorkGroupSize.z) -1);
	return res;
}

void mac_cormack_bound(sampler3D i, vec3 p, out vec4 m0, out vec4 m1) {
	ivec3 p_f = ivec3(p);

	vec4 v[8];

	v[0] = texelFetch(i, p_f, 0);
	v[1] = texelFetch(i, clamp_i(p_f + ivec3(0,1,0)), 0);
	v[2] = texelFetch(i, clamp_i(p_f + ivec3(1,0,0)), 0);
	v[3] = texelFetch(i, clamp_i(p_f + ivec3(1,1,0)), 0);
	
	v[4] = texelFetch(i, clamp_i(p_f + ivec3(0,0,1)), 0);
	v[5] = texelFetch(i, clamp_i(p_f + ivec3(0,1,1)), 0);
	v[6] = texelFetch(i, clamp_i(p_f + ivec3(1,0,1)), 0);
	v[7] = texelFetch(i, clamp_i(p_f + ivec3(1,1,1)), 0);
	
	
	m0.x = min(v[0].x, min(v[1].x, min(v[2].x, min(v[3].x, min(v[4].x, min(v[5].x, min(v[6].x, v[7].x)))))));
	m1.x = max(v[0].x, max(v[1].x, max(v[2].x, max(v[3].x, max(v[4].x, max(v[5].x, max(v[6].x, v[7].x)))))));
	
	m0.y = min(v[0].y, min(v[1].y, min(v[2].y, min(v[3].y, min(v[4].y, min(v[5].y, min(v[6].y, v[7].y)))))));
	m1.y = max(v[0].y, max(v[1].y, max(v[2].y, max(v[3].y, max(v[4].y, max(v[5].y, max(v[6].y, v[7].y)))))));
	
	m0.z = min(v[0].z, min(v[1].z, min(v[2].z, min(v[3].z, min(v[4].z, min(v[5].z, min(v[6].z, v[7].z)))))));
	m1.z = max(v[0].z, max(v[1].z, max(v[2].z, max(v[3].z, max(v[4].z, max(v[5].z, max(v[6].z, v[7].z)))))));
	
	m0.w = min(v[0].w, min(v[1].w, min(v[2].w, min(v[3].w, min(v[4].w, min(v[5].w, min(v[6].w, v[7].w)))))));
	m1.w = max(v[0].w, max(v[1].w, max(v[2].w, max(v[3].w, max(v[4].w, max(v[5].w, max(v[6].w, v[7].w)))))));
}

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	float obstacle = texelFetch(t_obstacle, pos, 0).r;
	vec4 target_value;
	if (obstacle > 0) {
		target_value = vec4(0);
	}
	else {
		vec3 vel = texelFetch(t_velocity, pos, 0).xyz;
		vec3 back_track_pos = pos - vel * u_time_step;
		vec3 back_track_coord = (back_track_pos + 0.5) / vec3(gl_NumWorkGroups * gl_WorkGroupSize);
	
		ivec3 diff = ivec3(gl_NumWorkGroups * gl_WorkGroupSize) - 1 - pos;
		int dist_2_bound = min(pos.x, min(pos.y, min(pos.z, min(diff.x, min(diff.y, diff.z)))));
		
		if (dist_2_bound <= 3) {
			vec4 source_value = texture(t_source, back_track_coord);
			target_value.x = max(u_minimum.x, source_value.x * (1 - u_dissipation) - u_time_step * u_decay);
			target_value.y = max(u_minimum.y, source_value.y * (1 - u_dissipation) - u_time_step * u_decay);
			target_value.z = max(u_minimum.z, source_value.z * (1 - u_dissipation) - u_time_step * u_decay);
			target_value.w = max(u_minimum.w, source_value.w * (1 - u_dissipation) - u_time_step * u_decay);
		}
		else {
			vec4 source_value = texture(t_phi_n_1_hat, back_track_coord) + 0.5 * (texelFetch(t_source, pos, 0) - texelFetch(t_phi_n_hat, pos, 0));
			vec4 bound_min, bound_max;
			mac_cormack_bound(t_source, back_track_pos, bound_min, bound_max);
			source_value.x = clamp(source_value.x, bound_min.x, bound_max.x);
			source_value.y = clamp(source_value.y, bound_min.y, bound_max.y);
			source_value.z = clamp(source_value.z, bound_min.z, bound_max.z);
			source_value.w = clamp(source_value.w, bound_min.w, bound_max.w);
			target_value.x = max(u_minimum.x, source_value.x * (1 - u_time_step) - u_time_step * u_decay);
			target_value.y = max(u_minimum.y, source_value.y * (1 - u_time_step) - u_time_step * u_decay);
			target_value.z = max(u_minimum.z, source_value.z * (1 - u_time_step) - u_time_step * u_decay);
			target_value.w = max(u_minimum.w, source_value.w * (1 - u_time_step) - u_time_step * u_decay);
		}
	}
	
	imageStore(i_target, pos, target_value);
}

__CS_ADVECT_MAC_CORMACK_R__

layout (binding = 0) uniform sampler3D t_velocity;
layout (binding = 1) uniform sampler3D t_obstacle;
layout (binding = 2) uniform sampler3D t_phi_n_1_hat;
layout (binding = 3) uniform sampler3D t_phi_n_hat;
layout (binding = 4) uniform sampler3D t_source;

layout (binding = 0, r16f) uniform image3D i_target;

uniform float u_time_step;
uniform float u_dissipation;
uniform float u_decay;

uniform vec4 u_minimum = vec4(0);

ivec3 clamp_i (ivec3 i_in) {
	ivec3 res;
	res.x = clamp(i_in.x, 0, int(gl_NumWorkGroups.x * gl_WorkGroupSize.x) -1);
	res.y = clamp(i_in.y, 0, int(gl_NumWorkGroups.y * gl_WorkGroupSize.y) -1);
	res.z = clamp(i_in.z, 0, int(gl_NumWorkGroups.z * gl_WorkGroupSize.z) -1);
	return res;
}

void mac_cormack_bound(sampler3D i, vec3 p, out vec4 m0, out vec4 m1) {
	ivec3 p_f = ivec3(p);

	vec4 v[8];

	v[0] = texelFetch(i, p_f, 0);
	v[1] = texelFetch(i, clamp_i(p_f + ivec3(0,1,0)), 0);
	v[2] = texelFetch(i, clamp_i(p_f + ivec3(1,0,0)), 0);
	v[3] = texelFetch(i, clamp_i(p_f + ivec3(1,1,0)), 0);
	
	v[4] = texelFetch(i, clamp_i(p_f + ivec3(0,0,1)), 0);
	v[5] = texelFetch(i, clamp_i(p_f + ivec3(0,1,1)), 0);
	v[6] = texelFetch(i, clamp_i(p_f + ivec3(1,0,1)), 0);
	v[7] = texelFetch(i, clamp_i(p_f + ivec3(1,1,1)), 0);
	
	
	m0.x = min(v[0].x, min(v[1].x, min(v[2].x, min(v[3].x, min(v[4].x, min(v[5].x, min(v[6].x, v[7].x)))))));
	m1.x = max(v[0].x, max(v[1].x, max(v[2].x, max(v[3].x, max(v[4].x, max(v[5].x, max(v[6].x, v[7].x)))))));
	
	m0.y = min(v[0].y, min(v[1].y, min(v[2].y, min(v[3].y, min(v[4].y, min(v[5].y, min(v[6].y, v[7].y)))))));
	m1.y = max(v[0].y, max(v[1].y, max(v[2].y, max(v[3].y, max(v[4].y, max(v[5].y, max(v[6].y, v[7].y)))))));
	
	m0.z = min(v[0].z, min(v[1].z, min(v[2].z, min(v[3].z, min(v[4].z, min(v[5].z, min(v[6].z, v[7].z)))))));
	m1.z = max(v[0].z, max(v[1].z, max(v[2].z, max(v[3].z, max(v[4].z, max(v[5].z, max(v[6].z, v[7].z)))))));
	
	m0.w = min(v[0].w, min(v[1].w, min(v[2].w, min(v[3].w, min(v[4].w, min(v[5].w, min(v[6].w, v[7].w)))))));
	m1.w = max(v[0].w, max(v[1].w, max(v[2].w, max(v[3].w, max(v[4].w, max(v[5].w, max(v[6].w, v[7].w)))))));
}

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	float obstacle = texelFetch(t_obstacle, pos, 0).r;
	vec4 target_value;
	if (obstacle > 0) {
		target_value = vec4(0);
	}
	else {
		vec3 vel = texelFetch(t_velocity, pos, 0).xyz;
		vec3 back_track_pos = pos - vel * u_time_step;
		vec3 back_track_coord = (back_track_pos + 0.5) / vec3(gl_NumWorkGroups * gl_WorkGroupSize);
	
		ivec3 diff = ivec3(gl_NumWorkGroups * gl_WorkGroupSize) - 1 - pos;
		int dist_2_bound = min(pos.x, min(pos.y, min(pos.z, min(diff.x, min(diff.y, diff.z)))));
		
		if (dist_2_bound <= 3) {
			vec4 source_value = texture(t_source, back_track_coord);
			target_value.x = max(u_minimum.x, source_value.x * (1 - u_dissipation) - u_time_step * u_decay);
		}
		else {
			vec4 source_value = texture(t_phi_n_1_hat, back_track_coord) + 0.5 * (texelFetch(t_source, pos, 0) - texelFetch(t_phi_n_hat, pos, 0));
			vec4 bound_min, bound_max;
			mac_cormack_bound(t_source, back_track_pos, bound_min, bound_max);
			source_value.x = clamp(source_value.x, bound_min.x, bound_max.x);
			target_value.x = max(u_minimum.x, source_value.x * (1 - u_dissipation) - u_time_step * u_decay);
		}
	}
	
	imageStore(i_target, pos, target_value);
}

__CS_DIVERGENCE__

layout (binding = 0) uniform sampler3D t_velocity;
layout (binding = 1) uniform sampler3D t_obstacle;

layout (binding = 0, r16f) uniform image3D i_divergence;

ivec3 clamp_i (ivec3 i_in) {
	ivec3 res;
	res.x = clamp(i_in.x, 0, int(gl_NumWorkGroups.x * gl_WorkGroupSize.x) -1);
	res.y = clamp(i_in.y, 0, int(gl_NumWorkGroups.y * gl_WorkGroupSize.y) -1);
	res.z = clamp(i_in.z, 0, int(gl_NumWorkGroups.z * gl_WorkGroupSize.z) -1);
	return res;
}

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	
	vec4 v_f = texelFetch(t_velocity, clamp_i(pos +ivec3(0, 0, 1)), 0);
	vec4 v_b = texelFetch(t_velocity, clamp_i(pos +ivec3(0, 0, -1)), 0);
	vec4 v_r = texelFetch(t_velocity, clamp_i(pos +ivec3(1, 0, 0)), 0);
	vec4 v_l = texelFetch(t_velocity, clamp_i(pos +ivec3(-1, 0, 0)), 0);
	vec4 v_u = texelFetch(t_velocity, clamp_i(pos +ivec3(0, 1, 0)), 0);
	vec4 v_d = texelFetch(t_velocity, clamp_i(pos +ivec3(0, -1, 0)), 0);
	
	vec4 o_f = texelFetch(t_obstacle, clamp_i(pos +ivec3(0, 0, 1)), 0);
	vec4 o_b = texelFetch(t_obstacle, clamp_i(pos +ivec3(0, 0, -1)), 0);
	vec4 o_r = texelFetch(t_obstacle, clamp_i(pos +ivec3(1, 0, 0)), 0);
	vec4 o_l = texelFetch(t_obstacle, clamp_i(pos +ivec3(-1, 0, 0)), 0);
	vec4 o_u = texelFetch(t_obstacle, clamp_i(pos +ivec3(0, 1, 0)), 0);
	vec4 o_d = texelFetch(t_obstacle, clamp_i(pos +ivec3(0, -1, 0)), 0);
	
	if (o_f.x > 0) v_f.xyz = vec3(0);
	if (o_b.x > 0) v_b.xyz = vec3(0);
	if (o_r.x > 0) v_r.xyz = vec3(0);
	if (o_l.x > 0) v_l.xyz = vec3(0);
	if (o_u.x > 0) v_u.xyz = vec3(0);
	if (o_d.x > 0) v_d.xyz = vec3(0);
	
	float div = 0.5 * (v_f.z - v_b.z + v_r.x - v_l.x + v_u.y - v_d.y);
	imageStore(i_divergence, pos, vec4(div));
}

__CS_JACOBI__

layout (binding = 0) uniform sampler3D t_divergence;
layout (binding = 1) uniform sampler3D t_obstacle;
layout (binding = 2) uniform sampler3D t_pressure;

layout (binding = 0, r16f) uniform image3D i_pressure_target;

ivec3 clamp_i (ivec3 i_in) {
	ivec3 res;
	res.x = clamp(i_in.x, 0, int(gl_NumWorkGroups.x * gl_WorkGroupSize.x) -1);
	res.y = clamp(i_in.y, 0, int(gl_NumWorkGroups.y * gl_WorkGroupSize.y) -1);
	res.z = clamp(i_in.z, 0, int(gl_NumWorkGroups.z * gl_WorkGroupSize.z) -1);
	return res;
}

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);

	float p_f = texelFetch(t_pressure, clamp_i(pos +ivec3(0, 0, 1)), 0).r;
	float p_b = texelFetch(t_pressure, clamp_i(pos +ivec3(0, 0, -1)), 0).r;
	float p_r = texelFetch(t_pressure, clamp_i(pos +ivec3(1, 0, 0)), 0).r;
	float p_l = texelFetch(t_pressure, clamp_i(pos +ivec3(-1, 0, 0)), 0).r;
	float p_u = texelFetch(t_pressure, clamp_i(pos +ivec3(0, 1, 0)), 0).r;
	float p_d = texelFetch(t_pressure, clamp_i(pos +ivec3(0, -1, 0)), 0).r;
	float p_c = texelFetch(t_pressure, pos, 0).r;
	
	vec4 o_f = texelFetch(t_obstacle, clamp_i(pos +ivec3(0, 0, 1)), 0);
	vec4 o_b = texelFetch(t_obstacle, clamp_i(pos +ivec3(0, 0, -1)), 0);
	vec4 o_r = texelFetch(t_obstacle, clamp_i(pos +ivec3(1, 0, 0)), 0);
	vec4 o_l = texelFetch(t_obstacle, clamp_i(pos +ivec3(-1, 0, 0)), 0);
	vec4 o_u = texelFetch(t_obstacle, clamp_i(pos +ivec3(0, 1, 0)), 0);
	vec4 o_d = texelFetch(t_obstacle, clamp_i(pos +ivec3(0, -1, 0)), 0);
	
	if (o_f.r > 0) p_f = p_c;
	if (o_b.r > 0) p_b = p_c;
	if (o_r.r > 0) p_r = p_c;
	if (o_l.r > 0) p_l = p_c;
	if (o_u.r > 0) p_u = p_c;
	if (o_d.r > 0) p_d = p_c;
	
	float div = texelFetch(t_divergence, pos, 0).r;
	float p = (p_f + p_b + p_r + p_l + p_u + p_d - div) / 6.0;
	imageStore(i_pressure_target, pos, vec4(p));
}

__CS_PROJECTION__

layout (binding = 0) uniform sampler3D t_velocity;
layout (binding = 1) uniform sampler3D t_obstacle;
layout (binding = 2) uniform sampler3D t_pressure;

layout (binding = 0, rgba16f) uniform image3D i_velocity_target;

uniform float u_gradient_scale = 1.1;

ivec3 clamp_i (ivec3 i_in) {
	ivec3 res;
	res.x = clamp(i_in.x, 0, int(gl_NumWorkGroups.x * gl_WorkGroupSize.x) -1);
	res.y = clamp(i_in.y, 0, int(gl_NumWorkGroups.y * gl_WorkGroupSize.y) -1);
	res.z = clamp(i_in.z, 0, int(gl_NumWorkGroups.z * gl_WorkGroupSize.z) -1);
	return res;
}

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	vec4 target_vel;
	
	vec4 o_c = texelFetch(t_obstacle, pos, 0);
	if(o_c.r > 0) {
		target_vel.xyz = vec3(0);
	}
	else {
		float p_f = texelFetch(t_pressure, clamp_i(pos +ivec3(0, 0, 1)), 0).r;
		float p_b = texelFetch(t_pressure, clamp_i(pos +ivec3(0, 0, -1)), 0).r;
		float p_r = texelFetch(t_pressure, clamp_i(pos +ivec3(1, 0, 0)), 0).r;
		float p_l = texelFetch(t_pressure, clamp_i(pos +ivec3(-1, 0, 0)), 0).r;
		float p_u = texelFetch(t_pressure, clamp_i(pos +ivec3(0, 1, 0)), 0).r;
		float p_d = texelFetch(t_pressure, clamp_i(pos +ivec3(0, -1, 0)), 0).r;
		float p_c = texelFetch(t_pressure, pos, 0).r;
		
		vec4 o_f = texelFetch(t_obstacle, clamp_i(pos +ivec3(0, 0, 1)), 0);
		vec4 o_b = texelFetch(t_obstacle, clamp_i(pos +ivec3(0, 0, -1)), 0);
		vec4 o_r = texelFetch(t_obstacle, clamp_i(pos +ivec3(1, 0, 0)), 0);
		vec4 o_l = texelFetch(t_obstacle, clamp_i(pos +ivec3(-1, 0, 0)), 0);
		vec4 o_u = texelFetch(t_obstacle, clamp_i(pos +ivec3(0, 1, 0)), 0);
		vec4 o_d = texelFetch(t_obstacle, clamp_i(pos +ivec3(0, -1, 0)), 0);
		
		vec3 o_v = vec3(0);
		vec3 v_mask = vec3(1);
		
		if (o_f.r > 0) {p_f = p_c; o_v.z = 0; v_mask.z = 0;}
		if (o_b.r > 0) {p_b = p_c; o_v.z = 0; v_mask.z = 0;}
		if (o_r.r > 0) {p_r = p_c; o_v.x = 0; v_mask.x = 0;}
		if (o_l.r > 0) {p_l = p_c; o_v.x = 0; v_mask.x = 0;}
		if (o_u.r > 0) {p_u = p_c; o_v.y = 0; v_mask.y = 0;}
		if (o_d.r > 0) {p_d = p_c; o_v.y = 0; v_mask.y = 0;}
		
		vec3 source_vel = texelFetch(t_velocity, pos, 0).xyz;
		vec3 grad = vec3(p_r - p_l, p_u - p_d, p_f - p_b) * 0.5;
		vec3 v = source_vel - grad*u_gradient_scale;
		target_vel.xyz = (v * v_mask) + o_v;
	}
	imageStore(i_velocity_target, pos, target_vel);

}

