#version 430

#define MAX_PARTICLE_PACK_COUNT 1000
#define MAX_ATTRACTOR_COUNT 64 
#define MAX_STREAM_COUNT 64

struct Particle {
	vec4 position; //xyz = position, w = life
	vec4 velocity; //xyz = velocity, w = hue
};

layout (std430, binding=0) buffer b_particles_data {
	Particle particles[MAX_PARTICLE_PACK_COUNT * 128];
};

uniform float u_particle_lifespan;
uniform float u_particle_initial_spread;
uniform float u_particle_initial_speed;

uniform float u_random_seed;

layout (local_size_x = 128) in;

// A single iteration of Bob Jenkins' One-At-A-Time hashing algorithm.
uint hash( uint x ) {
    x += ( x << 10u );
    x ^= ( x >>  6u );
    x += ( x <<  3u );
    x ^= ( x >> 11u );
    x += ( x << 15u );
    return x;
}

// Construct a float with half-open range [0:1] using low 23 bits.
// All zeroes yields 0.0, all ones yields the next smallest representable value below 1.0.
float floatConstruct( uint m ) {
    const uint ieeeMantissa = 0x007FFFFFu; // binary32 mantissa bitmask
    const uint ieeeOne      = 0x3F800000u; // 1.0 in IEEE binary32

    m &= ieeeMantissa;                     // Keep only mantissa bits (fractional part)
    m |= ieeeOne;                          // Add fractional part to 1.0

    float  f = uintBitsToFloat( m );       // Range [1:2]
    return f - 1.0;                        // Range [0:1]
}

// Pseudo-random value in half-open range [0:1].
float random_from_int ( uint x ) { return floatConstruct(hash(x)); }

float random_from_float ( float x ) { return floatConstruct(hash(floatBitsToUint(x))); }

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

	float x = random_from_int(gl_GlobalInvocationID.x);
	float y = random_from_float(float(gl_GlobalInvocationID.x) * u_random_seed);
	float z = random_from_float(x * u_random_seed);
	
	vec3 seed = vec3(x, y, z);

	p.position.xyz = normalize(rand_vec3(seed.yxz) + rand_vec3(seed.yzx)) * rand_float(rand_vec3(seed.xzy)) * u_particle_initial_spread;
	p.position.w = rand_float(rand_vec3(seed)) * u_particle_lifespan;
	p.velocity.xyz = normalize(rand_vec3(seed.zxy) - rand_vec3(seed.yzx)) * rand_float(rand_vec3(seed.xzy)) * u_particle_initial_speed;

	particles[gl_GlobalInvocationID.x] = p;
}
