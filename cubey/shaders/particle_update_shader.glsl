#version 430

struct Particle {
	vec4 position;
	vec4 velocity;
};

layout (std430, binding=0) buffer particles_buffer {
	Particle[] particles;
};

uniform vec4 attractors[16];
uniform float delta_time;
uniform float lifespan;

layout (local_size_x = 128) in;

const float bound = 100;

float rand_oneliner(vec3 v_in) {
    return fract(sin(dot(v_in ,vec3(12.9898, 78.233, 56.8346))) * 43758.5453);
}

void main() {

	Particle p = particles[gl_GlobalInvocationID.x];
	
	p.position.xyz += p.velocity.xyz * delta_time;
	p.position.w -= delta_time;
	
	if(p.position.w < 0) {
		p.position.xyz = - normalize(p.position.xyz) * rand_oneliner(p.position.xyz);
		p.position.w += lifespan;
		
		p.velocity.w = 1 + rand_oneliner(p.velocity.xyz);
		p.velocity.x = 1 - 2 * rand_oneliner(p.position.xyz);
		p.velocity.y = 1 - 2 * rand_oneliner(p.position.yzx);
		p.velocity.z = 1 - 2 * rand_oneliner(p.position.zxy);
		p.velocity.xyz *= lifespan;
	}
	else {
		float dist2c = dot(p.position.xyz, p.position.xyz);
		
		if(dist2c > bound) {
			p.velocity.xyz = -p.velocity.xyz;
		}
		else {
			for (int i =0; i< 16; i++) {
				vec3 dist = attractors[i].xyz - p.position.xyz;
				p.velocity.xyz += 0.1 * delta_time * attractors[i].w * normalize(dist) / dot(dist, dist);
			}
		}
	}
	
	particles[gl_GlobalInvocationID.x] = p;
}
