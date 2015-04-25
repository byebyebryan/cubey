
#define MAX_PARTICLE_PACK_COUNT 10000
#define MAX_ATTRACTOR_COUNT 64 
#define MAX_STREAM_COUNT 128

#define PARTICLE_PACK_SIZE 128

struct Particle {
	vec4 position; //xyz = position, w = life
	vec4 velocity; //xyz = velocity, w = hue
};

layout (std430, binding=0) buffer b_particles_data {
	Particle particles[];
};

__CS_INIT__

uniform float u_particle_lifespan;
uniform float u_particle_initial_spread;
uniform float u_particle_initial_speed;

uniform float u_random_seed;

layout (local_size_x = PARTICLE_PACK_SIZE) in;

float rand_float(vec3 v_in) {
    return fract(sin(dot(v_in ,vec3(12.9898, 78.233, 56.8346))) * 43758.5453);
}

float rand_float_signed(vec3 v_in) {
	return mix(-1, 1, rand_float(v_in));
}

vec3 rand_vec3(vec3 v_in) {
	return vec3(rand_float_signed(v_in.yzx), rand_float_signed(v_in.yxz), rand_float_signed(v_in.xzy));
}

void main() {
	
	Particle p;

	p.position.x = gl_LocalInvocationID.x;
	p.position.y = gl_WorkGroupID.x;
	p.position.z = 0.0;

	p.position.w = 0.0;

	p.velocity.xyz = vec3(0.0);

	particles[gl_GlobalInvocationID.x] = p;
}

__CS_UPDATE__

uniform vec4 u_attractors[MAX_ATTRACTOR_COUNT]; //xyz = position, w = mass
uniform vec3 u_streams[MAX_STREAM_COUNT];

uniform vec3 u_center = vec3(0.0, 0.0, 0.0);

uniform float u_delta_time;

uniform float u_particle_lifespan;
uniform float u_particle_stream_ratio;
uniform float u_particle_initial_spread;
uniform float u_particle_initial_speed;
uniform float u_particle_bound_sq;
uniform float u_particle_stream_deviation;
uniform float u_particle_speed_decay;
uniform float u_particle_speed_randomizer;

uniform float u_attraction_force_multiplier;

uniform uint u_particle_pack_count;
uniform int u_attractor_count;
uniform int u_stream_count;

const float k_bound_decay = 0.9;

layout (local_size_x = PARTICLE_PACK_SIZE) in;

float rand_float(vec3 v_in) {
    return fract(sin(dot(v_in ,vec3(12.9898, 78.233, 56.8346))) * 43758.5453);
}

float rand_float_signed(vec3 v_in) {
	return mix(-1, 1, rand_float(v_in));
}

float rand_linear(vec3 v_in, float _min, float _max) {
	return mix(_min, _max, rand_float(v_in));
}

int rand_index(vec3 v_in, int _min, int _max) {
	return int(rand_linear(v_in, _min, _max));
}

vec3 rand_vec3(vec3 v_in) {
	return vec3(rand_float_signed(v_in.yzx), rand_float_signed(v_in.yxz), rand_float_signed(v_in.xzy));
}

void main() {
	
	Particle p = particles[gl_GlobalInvocationID.x];
	
	float seed = float(gl_GlobalInvocationID.x) / 1000.0;
	
	p.position.xyz += p.velocity.xyz * u_delta_time;
	p.position.w -= u_delta_time;
	
	if(p.position.w < 0) {
		
		if(gl_WorkGroupID.x < u_particle_pack_count) {
			p.position.w += u_particle_lifespan * rand_float(p.position.xzy * seed);
			
			int idx = rand_index(rand_vec3(p.velocity.zyx * seed) + p.position.xzy * seed, 0, int(u_stream_count / u_particle_stream_ratio));

			p.position.xyz = u_center + normalize(rand_vec3(p.position.zyx)) * rand_float(p.position.yzx) * u_particle_initial_spread;

			if (idx < u_stream_count) {
				p.velocity.xyz = u_streams[idx] - u_center + rand_vec3(p.position.xzy) * u_particle_stream_deviation;
			}
			else {
				p.velocity.xyz = normalize(rand_vec3(p.velocity.zxy)) * rand_float(p.velocity.yzx) * u_particle_initial_speed;
			}

			//p.velocity.w = u_particle_hue;
			particles[gl_GlobalInvocationID.x] = p;
		}
	}
	else {
		float dist2c = dot(p.position.xyz, p.position.xyz);
		
		if(dist2c >= u_particle_bound_sq) {
			vec3 normal = - normalize(p.position.xyz);
			p.velocity.xyz = p.velocity.xyz - 2 * dot(normal, p.velocity.xyz) * normal;
			p.velocity.xyz *= k_bound_decay;
		}
		else {
			for (int i =0; i< u_attractor_count; i++) {
				vec3 dist = u_attractors[i].xyz - p.position.xyz;
				p.velocity.xyz += u_attraction_force_multiplier * u_delta_time * u_attractors[i].w * normalize(dist) / dot(dist, dist);
				//p.velocity.xyz += u_attraction_force_multiplier * u_delta_time * u_attractors[i].w * dist / dot(dist, dist);
			}

			p.velocity.xyz += normalize(rand_vec3(p.position.zyx)) * rand_float(p.position.xzy) * u_particle_speed_randomizer;
			p.velocity.xyz -= u_delta_time * normalize(p.velocity.xyz) * dot(p.velocity.xyz, p.velocity.xyz) * u_particle_speed_decay;
		}

		particles[gl_GlobalInvocationID.x] = p;
	}
}
