#version 430

#ifdef _VERTEX_S_

layout(location = 0) in vec4 in_vertex_position;

void main() {
	gl_Position = in_vertex_position;
}

#endif

#ifdef _GEOMETRY_S_

vec3 mod289(vec3 x) {
	return x - floor(x * (1.0 / 289.0)) * 289.0;
}

vec4 mod289(vec4 x) {
	return x - floor(x * (1.0 / 289.0)) * 289.0;
}

vec4 permute(vec4 x) {
	return mod289(((x*34.0)+1.0)*x);
}

vec4 taylorInvSqrt(vec4 r) {
	return 1.79284291400159 - 0.85373472095314 * r;
}

float snoise(vec3 v) { 
	const vec2  C = vec2(1.0/6.0, 1.0/3.0) ;
	const vec4  D = vec4(0.0, 0.5, 1.0, 2.0);

	vec3 i  = floor(v + dot(v, C.yyy) );
	vec3 x0 =   v - i + dot(i, C.xxx) ;

	vec3 g = step(x0.yzx, x0.xyz);
	vec3 l = 1.0 - g;
	vec3 i1 = min( g.xyz, l.zxy );
	vec3 i2 = max( g.xyz, l.zxy );

	vec3 x1 = x0 - i1 + C.xxx;
	vec3 x2 = x0 - i2 + C.yyy;
	vec3 x3 = x0 - D.yyy;

	i = mod289(i); 
	vec4 p = permute( permute( permute( i.z + vec4(0.0, i1.z, i2.z, 1.0 )) + i.y + vec4(0.0, i1.y, i2.y, 1.0 )) + i.x + vec4(0.0, i1.x, i2.x, 1.0 ));

	float n_ = 0.142857142857; 
	vec3  ns = n_ * D.wyz - D.xzx;

	vec4 j = p - 49.0 * floor(p * ns.z * ns.z);  

	vec4 x_ = floor(j * ns.z);
	vec4 y_ = floor(j - 7.0 * x_ );

	vec4 x = x_ *ns.x + ns.yyyy;
	vec4 y = y_ *ns.x + ns.yyyy;
	vec4 h = 1.0 - abs(x) - abs(y);

	vec4 b0 = vec4( x.xy, y.xy );
	vec4 b1 = vec4( x.zw, y.zw );

	vec4 s0 = floor(b0)*2.0 + 1.0;
	vec4 s1 = floor(b1)*2.0 + 1.0;
	vec4 sh = -step(h, vec4(0.0));

	vec4 a0 = b0.xzyw + s0.xzyw*sh.xxyy ;
	vec4 a1 = b1.xzyw + s1.xzyw*sh.zzww ;

	vec3 p0 = vec3(a0.xy,h.x);
	vec3 p1 = vec3(a0.zw,h.y);
	vec3 p2 = vec3(a1.xy,h.z);
	vec3 p3 = vec3(a1.zw,h.w);

	vec4 norm = taylorInvSqrt(vec4(dot(p0,p0), dot(p1,p1), dot(p2, p2), dot(p3,p3)));
	p0 *= norm.x;
	p1 *= norm.y;
	p2 *= norm.z;
	p3 *= norm.w;

	vec4 m = max(0.6 - vec4(dot(x0,x0), dot(x1,x1), dot(x2,x2), dot(x3,x3)), 0.0);
	m = m * m;
	return 42.0 * dot( m*m, vec4( dot(p0,x0), dot(p1,x1), dot(p2,x2), dot(p3,x3) ) );
}

layout (points) in;
layout (triangle_strip, max_vertices = 15) out;

layout (std430, binding=0) buffer b_triangle_table {
	uint triangle_table[4096];
};

uniform float u_block_resolution;
uniform mat4 u_mvp_mat;
uniform mat3 u_normal_mat;

const vec3 corner_offset[8] = vec3[](
	vec3(0,0,0),
	vec3(0,1,0),
	vec3(1,1,0),
	vec3(1,0,0),
	vec3(0,0,1),
	vec3(0,1,1),
	vec3(1,1,1),
	vec3(1,0,1));

const ivec2 edge_to_corner[12] = ivec2[](
	ivec2(0, 1),
	ivec2(1, 2),
	ivec2(2, 3),
	ivec2(3, 0),
	ivec2(4, 5),
	ivec2(5, 6),
	ivec2(6, 7),
	ivec2(7, 4),
	ivec2(0, 4),
	ivec2(1, 5),
	ivec2(2, 6),
	ivec2(3, 7));

float sample_density(vec3 v) {
	return snoise(v) - v.y;
}

out vec3 var_normal;

void main() {

	float voxel_width = 1 / u_block_resolution;
	vec3 origin_pos = gl_in[0].gl_Position.xyz;
	
	vec3 corner_pos[8];
	float corner_density[8];
	
	uint case_id = 0;
	for(int i=0; i<8; ++i)
	{
		corner_pos[i] = origin_pos + corner_offset[i] * voxel_width;
		corner_density[i] = sample_density(corner_pos[i]);

		uint edge_case = uint(corner_density[i] > 0) << i;
		case_id = case_id | edge_case;
	}

	if(case_id==0||case_id==255)
		return;

	vec3 vert_pos[12];
	vec3 vert_normal[12];
	
	for(int i=0; i<12; ++i)
	{
		vec3 v0 = corner_pos[edge_to_corner[i].x];
		vec3 v1 = corner_pos[edge_to_corner[i].y];
		float d0 = corner_density[edge_to_corner[i].x];
		float d1 = corner_density[edge_to_corner[i].y];
		float d = (0 - d0)/(d1 - d0);
		vert_pos[i] = mix(v0, v1, d);
	
		vec3 offset;
		offset = vec3(voxel_width, 0, 0);
		vert_normal[i].x = sample_density(vert_pos[i]+offset) - sample_density(vert_pos[i]-offset);
		
		offset = vec3(0, voxel_width, 0);
		vert_normal[i].y = sample_density(vert_pos[i]+offset) - sample_density(vert_pos[i]-offset);
		
		offset = vec3(0, 0, voxel_width);
		vert_normal[i].z = sample_density(vert_pos[i]+offset) - sample_density(vert_pos[i]-offset);

		vert_normal[i] = normalize(vert_normal[i]);
	}

	int index = int(case_id*16);

	while(true)
	{
		if(triangle_table[index] == 12)
			break;
		
		gl_Position = u_mvp_mat * vec4(vert_pos[triangle_table[index]], 1);
		var_normal = normalize(u_normal_mat * vert_normal[triangle_table[index]]);
		EmitVertex();

		gl_Position = u_mvp_mat * vec4(vert_pos[triangle_table[index+2]], 1);
		var_normal = normalize(u_normal_mat * vert_normal[triangle_table[index+2]]);
		EmitVertex();

		gl_Position = u_mvp_mat * vec4(vert_pos[triangle_table[index+1]], 1);
		var_normal = normalize(u_normal_mat * vert_normal[triangle_table[index+1]]);
		EmitVertex();
		
		EndPrimitive();
		index+=3;
	}
}

#endif

#ifdef _FRAGMENT_S_

uniform vec3 u_ambient_light_color;
uniform vec3 u_directional_light_direction;
uniform vec3 u_directional_light_color;

in vec3 var_normal;

out vec4 out_color;

void main() {
	float diffuse = max(0.0, dot(var_normal, u_directional_light_direction));
	vec3 scattered_light = u_ambient_light_color + diffuse * u_directional_light_color;
	vec3 rgb = min(vec3(1.0), vec3(1.0, 1.0, 0.0) * scattered_light);

	out_color = vec4(rgb, 1);
}

#endif