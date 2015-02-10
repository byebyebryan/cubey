
__VS__

layout(location = 0) in vec4 in_vertex_position;

void main() {
	gl_Position = in_vertex_position;
}

__GS__

#include "simplex_noise_3d.glsl"

layout (points) in;
layout (triangle_strip, max_vertices = 15) out;

layout (std430, binding=0) buffer b_triangle_table {
	uint triangle_table[4096];
};

uniform float u_block_resolution;
uniform mat4 u_mvp_mat;
uniform mat3 u_normal_mat;
uniform float u_time;

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
	return snoise(v*10) - v.y*10;
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

__FS__

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
