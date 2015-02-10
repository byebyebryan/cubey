
#define LOCAL_WORKGROUP_SIZE_X 8
#define LOCAL_WORKGROUP_SIZE_Y 8
#define LOCAL_WORKGROUP_SIZE_Z 8

layout (local_size_x = LOCAL_WORKGROUP_SIZE_X, local_size_y = LOCAL_WORKGROUP_SIZE_Y, local_size_z = LOCAL_WORKGROUP_SIZE_Z) in;

__CS_FILL_RGBA__

layout (binding = 0, rgba16f) uniform image3D i_target;

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	imageStore(i_target, pos, vec4(0));
}

__CS_FILL_R__

layout (binding = 0, r16f) uniform image3D i_target;

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	imageStore(i_target, pos, vec4(0));
}

__CS_FILL_OBSTACLE__

layout (binding = 0, r16f) uniform image3D i_obstacle;

uniform vec3 u_location;
uniform float u_radius;

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	vec4 target_value = vec4(0);

	if (pos.x == 0 || pos.x == gl_NumWorkGroups.x * gl_WorkGroupSize.x - 1) target_value.x = 0.5;
	if (pos.y == 0 || pos.y == gl_NumWorkGroups.y * gl_WorkGroupSize.y - 1) target_value.x = 0.5;
	if (pos.z == 0 || pos.z == gl_NumWorkGroups.z * gl_WorkGroupSize.z - 1) target_value.x = 0.5;

	if (distance(u_location, vec3(pos)) < u_radius) target_value.x = 1;
	
	imageStore(i_obstacle, pos, target_value);
}

__CS_ADVECT_RGBA__

layout (binding = 0, rgba16f) uniform image3D i_velocity;
layout (binding = 1, r16f) uniform image3D i_obstacle;

layout (binding = 2, rgba16f) uniform image3D i_source;
layout (binding = 3, rgba16f) uniform image3D i_target;

uniform float u_time_step;
uniform float u_dissipation;

ivec3 clamp_i (ivec3 i_in) {
	return clamp(i_in, ivec3(0), ivec3(gl_NumWorkGroups * gl_WorkGroupSize) - ivec3(1));
}

vec4 sample_trilinear(image3D i, vec3 p) {
	ivec3 p_f = ivec3(p);
	vec3 r = fract(p);
	
	vec4 xyz0 = imageLoad(i, p_f);
	vec4 xyz1 = imageLoad(i, clamp_i(p_f + ivec3(0,1,0)));
	vec4 xyz2 = imageLoad(i, clamp_i(p_f + ivec3(1,0,0)));
	vec4 xyz3 = imageLoad(i, clamp_i(p_f + ivec3(1,1,0)));
	
	vec4 xyz4 = imageLoad(i, clamp_i(p_f + ivec3(0,0,1)));
	vec4 xyz5 = imageLoad(i, clamp_i(p_f + ivec3(0,1,1)));
	vec4 xyz6 = imageLoad(i, clamp_i(p_f + ivec3(1,0,1)));
	vec4 xyz7 = imageLoad(i, clamp_i(p_f + ivec3(1,1,1)));
	
	vec4 xy0 = mix(xyz0, xyz4, r.z);
	vec4 xy1 = mix(xyz1, xyz5, r.z);
	vec4 xy2 = mix(xyz2, xyz6, r.z);
	vec4 xy3 = mix(xyz3, xyz7, r.z);
	
	vec4 x0 = mix(xy0, xy1, r.y);
	vec4 x1 = mix(xy2, xy3, r.y);
	
	return mix(x0, x1, r.x);
}

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	vec4 obstacle = imageLoad(i_obstacle, pos);
	vec4 target_value;
	if (obstacle.r > 0) {
		target_value = vec4(0);
	}
	else {
		vec3 vel = imageLoad(i_velocity, pos).xyz;
		vec3 back_track_pos = pos - vel * u_time_step;
		vec4 source_value = sample_trilinear(i_source, back_track_pos);
		target_value = source_value * u_dissipation;
	}
	
	imageStore(i_target, pos, target_value);
}

__CS_ADVECT_R__

layout (binding = 0, rgba16f) uniform image3D i_velocity;
layout (binding = 1, r16f) uniform image3D i_obstacle;

layout (binding = 2, r16f) uniform image3D i_source;
layout (binding = 3, r16f) uniform image3D i_target;

uniform float u_time_step;
uniform float u_dissipation;

ivec3 clamp_i (ivec3 i_in) {
	return clamp(i_in, ivec3(0), ivec3(gl_NumWorkGroups * gl_WorkGroupSize) - ivec3(1));
}

vec4 sample_trilinear(image3D i, vec3 p) {
	ivec3 p_f = ivec3(p);
	vec3 r = fract(p);
	
	vec4 xyz0 = imageLoad(i, p_f);
	vec4 xyz1 = imageLoad(i, clamp_i(p_f + ivec3(0,1,0)));
	vec4 xyz2 = imageLoad(i, clamp_i(p_f + ivec3(1,0,0)));
	vec4 xyz3 = imageLoad(i, clamp_i(p_f + ivec3(1,1,0)));
	
	vec4 xyz4 = imageLoad(i, clamp_i(p_f + ivec3(0,0,1)));
	vec4 xyz5 = imageLoad(i, clamp_i(p_f + ivec3(0,1,1)));
	vec4 xyz6 = imageLoad(i, clamp_i(p_f + ivec3(1,0,1)));
	vec4 xyz7 = imageLoad(i, clamp_i(p_f + ivec3(1,1,1)));
	
	vec4 xy0 = mix(xyz0, xyz4, r.z);
	vec4 xy1 = mix(xyz1, xyz5, r.z);
	vec4 xy2 = mix(xyz2, xyz6, r.z);
	vec4 xy3 = mix(xyz3, xyz7, r.z);
	
	vec4 x0 = mix(xy0, xy1, r.y);
	vec4 x1 = mix(xy2, xy3, r.y);
	
	return mix(x0, x1, r.x);
}

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	vec4 obstacle = imageLoad(i_obstacle, pos);
	vec4 target_value;
	if (obstacle.r > 0) {
		target_value = vec4(0);
	}
	else {
		vec3 vel = imageLoad(i_velocity, pos).xyz;
		vec3 back_track_pos = pos - vel * u_time_step;
		vec4 source_value = sample_trilinear(i_source, back_track_pos);
		target_value = source_value * u_dissipation;
	}
	
	imageStore(i_target, pos, target_value);
}

__CS_ADVECT_R_MAC_CORMACK__

layout (binding = 0, rgba16f) uniform image3D i_velocity;
layout (binding = 1, r16f) uniform image3D i_obstacle;

layout (binding = 2, r16f) uniform image3D i_source;
layout (binding = 3, r16f) uniform image3D i_target;

layout (binding = 4, r16f) uniform image3D i_phi_n_1_hat;
layout (binding = 5, r16f) uniform image3D i_phi_n_hat;

uniform float u_time_step;
uniform float u_dissipation;
uniform float u_decay;

ivec3 clamp_i (ivec3 i_in) {
	return clamp(i_in, ivec3(0), ivec3(gl_NumWorkGroups * gl_WorkGroupSize) - ivec3(1));
}

vec4 sample_trilinear(image3D i, vec3 p) {
	ivec3 p_f = ivec3(p);
	vec3 r = fract(p);
	
	vec4 xyz0 = imageLoad(i, p_f);
	vec4 xyz1 = imageLoad(i, clamp_i(p_f + ivec3(0,1,0)));
	vec4 xyz2 = imageLoad(i, clamp_i(p_f + ivec3(1,0,0)));
	vec4 xyz3 = imageLoad(i, clamp_i(p_f + ivec3(1,1,0)));
	
	vec4 xyz4 = imageLoad(i, clamp_i(p_f + ivec3(0,0,1)));
	vec4 xyz5 = imageLoad(i, clamp_i(p_f + ivec3(0,1,1)));
	vec4 xyz6 = imageLoad(i, clamp_i(p_f + ivec3(1,0,1)));
	vec4 xyz7 = imageLoad(i, clamp_i(p_f + ivec3(1,1,1)));
	
	vec4 xy0 = mix(xyz0, xyz4, r.z);
	vec4 xy1 = mix(xyz1, xyz5, r.z);
	vec4 xy2 = mix(xyz2, xyz6, r.z);
	vec4 xy3 = mix(xyz3, xyz7, r.z);
	
	vec4 x0 = mix(xy0, xy1, r.y);
	vec4 x1 = mix(xy2, xy3, r.y);
	
	return mix(x0, x1, r.x);
}

vec2 mac_cormack_bound(image3D i, vec3 p) {
	ivec3 p_f = ivec3(p);

	float v[8];

	v[0] = imageLoad(i, p_f).x;
	v[1] = imageLoad(i, clamp_i(p_f + ivec3(0,1,0))).x;
	v[2] = imageLoad(i, clamp_i(p_f + ivec3(1,0,0))).x;
	v[3] = imageLoad(i, clamp_i(p_f + ivec3(1,1,0))).x;
	
	v[4] = imageLoad(i, clamp_i(p_f + ivec3(0,0,1))).x;
	v[5] = imageLoad(i, clamp_i(p_f + ivec3(0,1,1))).x;
	v[6] = imageLoad(i, clamp_i(p_f + ivec3(1,0,1))).x;
	v[7] = imageLoad(i, clamp_i(p_f + ivec3(1,1,1))).x;

	float v_min = v[0];
	for(int i=1; i< 8; i++) {
		v_min = min(v_min, v[i]);
	}
	float v_max = v[0];
	for(int i=1; i<8; i++) {
		v_max = max(v_max, v[i]);
	}
	return vec2(v_min, v_max);
}

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	vec4 obstacle = imageLoad(i_obstacle, pos);
	vec4 target_value;
	if (obstacle.r > 0) {
		target_value = vec4(0);
	}
	else {
		vec3 vel = imageLoad(i_velocity, pos).xyz;
		vec3 back_track_pos = pos - vel * u_time_step;
		vec4 source_value = sample_trilinear(i_phi_n_1_hat, back_track_pos) + 0.5 * (imageLoad(i_source, pos) - imageLoad(i_phi_n_hat, pos));
		vec2 bound = mac_cormack_bound(i_source, back_track_pos);
		source_value.x = clamp(source_value.x, bound.x, bound.y);
		target_value = source_value * u_dissipation - u_time_step * u_decay;
	}
	
	imageStore(i_target, pos, target_value);
}

__CS_SPLAT__

layout (binding = 0, r16f) uniform image3D i_source;
layout (binding = 1, r16f) uniform image3D i_target;

uniform float u_time_step;
uniform vec3 u_location;
uniform float u_radius;
uniform float u_intensity;

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	float dist_sq = dot(u_location - pos, u_location - pos);
	float gaussian = exp( - dist_sq / 2 * u_radius * u_radius) / u_radius;
	
	vec4 source_value = imageLoad(i_source, pos);
	vec4 target_value = source_value + u_intensity * gaussian * u_time_step;
	imageStore(i_target, pos, target_value);
}

__CS_EXPLOSION__

layout (binding = 0, rgba16f) uniform image3D i_source;
layout (binding = 1, rgba16f) uniform image3D i_target;

uniform float u_time_step;
uniform vec3 u_location;
uniform float u_radius;
uniform float u_intensity;

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	float dist_sq = dot(u_location - pos, u_location - pos);
	float gaussian = exp( - dist_sq / 2 * u_radius * u_radius) / u_radius;
	
	vec4 source_value = imageLoad(i_source, pos);
	vec3 dir = normalize(pos - u_location);
	vec3 target_value = source_value.xyz + u_intensity * gaussian * u_time_step * dir;
	imageStore(i_target, pos, vec4(target_value,0));
}

__CS_BUOYANCY__

layout (binding = 0, rgba16f) uniform image3D i_velocity;
layout (binding = 1, r16f) uniform image3D i_temperature;
layout (binding = 2, r16f) uniform image3D i_density;
layout (binding = 3, rgba16f) uniform image3D i_velocity_target;

uniform float u_ambient_temperature;
uniform float u_time_step;
uniform float u_buoyancy;
uniform float u_weight;

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	vec4 vel = imageLoad(i_velocity, pos);
	float temp = imageLoad(i_temperature, pos).x;
	
	if(temp > u_ambient_temperature) {
		float dens = imageLoad(i_density, pos).x;
		vel += u_time_step * ((temp - u_ambient_temperature) * u_buoyancy - dens * u_weight) * vec4(0,1,0,0);
	}
	
	imageStore(i_velocity_target, pos, vel);
}

__CS_VORTICITY__

layout (binding = 0, rgba16f) uniform image3D i_velocity;
layout (binding = 1, rgba16f) uniform image3D i_vorticity;

ivec3 clamp_i (ivec3 i_in) {
	return clamp(i_in, ivec3(0), ivec3(gl_NumWorkGroups * gl_WorkGroupSize) - ivec3(1));
}

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	
	vec4 v_r = imageLoad(i_velocity, clamp_i(pos +ivec3(1, 0, 0)));
	vec4 v_l = imageLoad(i_velocity, clamp_i(pos +ivec3(-1, 0, 0)));
	vec4 v_u = imageLoad(i_velocity, clamp_i(pos +ivec3(0, 1, 0)));
	vec4 v_d = imageLoad(i_velocity, clamp_i(pos +ivec3(0, -1, 0)));
	vec4 v_f = imageLoad(i_velocity, clamp_i(pos +ivec3(0, 0, 1)));
	vec4 v_b = imageLoad(i_velocity, clamp_i(pos +ivec3(0, 0, -1)));
	
	vec3 vor = 0.5 * vec3((v_u.z - v_d.z) - (v_f.y - v_b.y), (v_f.x - v_b.x) - (v_r.z - v_l.z), (v_r.y - v_l.y) - (v_u.x - v_d.x));
	imageStore(i_vorticity, pos, vec4(vor, 0));
}

__CS_CONFINEMENT__

layout (binding = 0, rgba16f) uniform image3D i_velocity;
layout (binding = 1, rgba16f) uniform image3D i_vorticity;
layout (binding = 2, rgba16f) uniform image3D i_velocity_target;

uniform float u_time_step;
uniform float u_vorticity_strength;

ivec3 clamp_i (ivec3 i_in) {
	return clamp(i_in, ivec3(0), ivec3(gl_NumWorkGroups * gl_WorkGroupSize) - ivec3(1));
}

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	
	vec3 v_r = imageLoad(i_vorticity, clamp_i(pos +ivec3(1, 0, 0))).xyz;
	vec3 v_l = imageLoad(i_vorticity, clamp_i(pos +ivec3(-1, 0, 0))).xyz;
	vec3 v_u = imageLoad(i_vorticity, clamp_i(pos +ivec3(0, 1, 0))).xyz;
	vec3 v_d = imageLoad(i_vorticity, clamp_i(pos +ivec3(0, -1, 0))).xyz;
	vec3 v_f = imageLoad(i_vorticity, clamp_i(pos +ivec3(0, 0, 1))).xyz;
	vec3 v_b = imageLoad(i_vorticity, clamp_i(pos +ivec3(0, 0, -1))).xyz;
	
	vec3 vor = imageLoad(i_vorticity, pos).xyz;
	vec3 eta = 0.5 * vec3(length(v_r)- length(v_l), length(v_u) - length(v_d), length(v_f) - length(v_b));
	eta = normalize(eta + vec3(0.001));
	
	vec3 dv = u_time_step * u_vorticity_strength * vec3(eta.y * vor.z - eta.z * vor.y, eta.z * vor.x - eta.x * vor.z, eta.x * vor.y - eta.y * vor.x);
	
	vec3 vel = imageLoad(i_velocity, pos).xyz;
	vel += dv;
	imageStore(i_velocity_target, pos, vec4(vel, 0));
}

__CS_DIVERGENCE__

layout (binding = 0, rgba16f) uniform image3D i_velocity;
layout (binding = 1, r16f) uniform image3D i_obstacle;
layout (binding = 2, r16f) uniform image3D i_divergence;

ivec3 clamp_i (ivec3 i_in) {
	return clamp(i_in, ivec3(0), ivec3(gl_NumWorkGroups * gl_WorkGroupSize) - ivec3(1));
}

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	
	vec4 v_f = imageLoad(i_velocity, clamp_i(pos +ivec3(0, 0, 1)));
	vec4 v_b = imageLoad(i_velocity, clamp_i(pos +ivec3(0, 0, -1)));
	vec4 v_r = imageLoad(i_velocity, clamp_i(pos +ivec3(1, 0, 0)));
	vec4 v_l = imageLoad(i_velocity, clamp_i(pos +ivec3(-1, 0, 0)));
	vec4 v_u = imageLoad(i_velocity, clamp_i(pos +ivec3(0, 1, 0)));
	vec4 v_d = imageLoad(i_velocity, clamp_i(pos +ivec3(0, -1, 0)));
	
	vec4 o_f = imageLoad(i_obstacle, clamp_i(pos +ivec3(0, 0, 1)));
	vec4 o_b = imageLoad(i_obstacle, clamp_i(pos +ivec3(0, 0, -1)));
	vec4 o_r = imageLoad(i_obstacle, clamp_i(pos +ivec3(1, 0, 0)));
	vec4 o_l = imageLoad(i_obstacle, clamp_i(pos +ivec3(-1, 0, 0)));
	vec4 o_u = imageLoad(i_obstacle, clamp_i(pos +ivec3(0, 1, 0)));
	vec4 o_d = imageLoad(i_obstacle, clamp_i(pos +ivec3(0, -1, 0)));
	
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

layout (binding = 0, r16f) uniform image3D i_divergence;
layout (binding = 1, r16f) uniform image3D i_obstacle;
layout (binding = 2, r16f) uniform image3D i_pressure;
layout (binding = 3, r16f) uniform image3D i_pressure_target;

ivec3 clamp_i (ivec3 i_in) {
	return clamp(i_in, ivec3(0), ivec3(gl_NumWorkGroups * gl_WorkGroupSize) - ivec3(1));
}

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);

	float p_f = imageLoad(i_pressure, clamp_i(pos +ivec3(0, 0, 1))).r;
	float p_b = imageLoad(i_pressure, clamp_i(pos +ivec3(0, 0, -1))).r;
	float p_r = imageLoad(i_pressure, clamp_i(pos +ivec3(1, 0, 0))).r;
	float p_l = imageLoad(i_pressure, clamp_i(pos +ivec3(-1, 0, 0))).r;
	float p_u = imageLoad(i_pressure, clamp_i(pos +ivec3(0, 1, 0))).r;
	float p_d = imageLoad(i_pressure, clamp_i(pos +ivec3(0, -1, 0))).r;
	float p_c = imageLoad(i_pressure, pos).r;
	
	vec4 o_f = imageLoad(i_obstacle, clamp_i(pos +ivec3(0, 0, 1)));
	vec4 o_b = imageLoad(i_obstacle, clamp_i(pos +ivec3(0, 0, -1)));
	vec4 o_r = imageLoad(i_obstacle, clamp_i(pos +ivec3(1, 0, 0)));
	vec4 o_l = imageLoad(i_obstacle, clamp_i(pos +ivec3(-1, 0, 0)));
	vec4 o_u = imageLoad(i_obstacle, clamp_i(pos +ivec3(0, 1, 0)));
	vec4 o_d = imageLoad(i_obstacle, clamp_i(pos +ivec3(0, -1, 0)));
	
	if (o_f.r > 0) p_f = p_c;
	if (o_b.r > 0) p_b = p_c;
	if (o_r.r > 0) p_r = p_c;
	if (o_l.r > 0) p_l = p_c;
	if (o_u.r > 0) p_u = p_c;
	if (o_d.r > 0) p_d = p_c;
	
	float div = imageLoad(i_divergence, pos).r;
	float p = (p_f + p_b + p_r + p_l + p_u + p_d - div) / 6.0;
	imageStore(i_pressure_target, pos, vec4(p));
}

__CS_PROJECTION__

layout (binding = 0, rgba16f) uniform image3D i_velocity;
layout (binding = 1, r16f) uniform image3D i_obstacle;
layout (binding = 2, r16f) uniform image3D i_pressure;
layout (binding = 3, rgba16f) uniform image3D i_velocity_target;

ivec3 clamp_i (ivec3 i_in) {
	return clamp(i_in, ivec3(0), ivec3(gl_NumWorkGroups * gl_WorkGroupSize) - ivec3(1));
}

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	vec4 target_vel;
	
	vec4 o_c = imageLoad(i_obstacle, pos);
	if(o_c.r > 0) {
		target_vel.xyz = o_c.yzw;
	}
	else {
		float p_f = imageLoad(i_pressure, clamp_i(pos +ivec3(0, 0, 1))).r;
		float p_b = imageLoad(i_pressure, clamp_i(pos +ivec3(0, 0, -1))).r;
		float p_r = imageLoad(i_pressure, clamp_i(pos +ivec3(1, 0, 0))).r;
		float p_l = imageLoad(i_pressure, clamp_i(pos +ivec3(-1, 0, 0))).r;
		float p_u = imageLoad(i_pressure, clamp_i(pos +ivec3(0, 1, 0))).r;
		float p_d = imageLoad(i_pressure, clamp_i(pos +ivec3(0, -1, 0))).r;
		float p_c = imageLoad(i_pressure, pos).r;
		
		vec4 o_f = imageLoad(i_obstacle, clamp_i(pos +ivec3(0, 0, 1)));
		vec4 o_b = imageLoad(i_obstacle, clamp_i(pos +ivec3(0, 0, -1)));
		vec4 o_r = imageLoad(i_obstacle, clamp_i(pos +ivec3(1, 0, 0)));
		vec4 o_l = imageLoad(i_obstacle, clamp_i(pos +ivec3(-1, 0, 0)));
		vec4 o_u = imageLoad(i_obstacle, clamp_i(pos +ivec3(0, 1, 0)));
		vec4 o_d = imageLoad(i_obstacle, clamp_i(pos +ivec3(0, -1, 0)));
		
		vec3 o_v = vec3(0);
		vec3 v_mask = vec3(1);
		
		if (o_f.r > 0) {p_f = p_c; o_v.z = 0; v_mask.z = 0;}
		if (o_b.r > 0) {p_b = p_c; o_v.z = 0; v_mask.z = 0;}
		if (o_r.r > 0) {p_r = p_c; o_v.x = 0; v_mask.x = 0;}
		if (o_l.r > 0) {p_l = p_c; o_v.x = 0; v_mask.x = 0;}
		if (o_u.r > 0) {p_u = p_c; o_v.y = 0; v_mask.y = 0;}
		if (o_d.r > 0) {p_d = p_c; o_v.y = 0; v_mask.y = 0;}
		
		vec3 source_vel = imageLoad(i_velocity, pos).xyz;
		vec3 grad = vec3(p_r - p_l, p_u - p_d, p_f - p_b) * 0.5;
		vec3 v = source_vel - grad;
		target_vel.xyz = (v * v_mask) + o_v;
	}
	imageStore(i_velocity_target, pos, target_vel);

}

