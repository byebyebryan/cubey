
#define LOCAL_WORKGROUP_SIZE_X 8
#define LOCAL_WORKGROUP_SIZE_Y 8
#define LOCAL_WORKGROUP_SIZE_Z 8

layout (local_size_x = LOCAL_WORKGROUP_SIZE_X, local_size_y = LOCAL_WORKGROUP_SIZE_Y, local_size_z = LOCAL_WORKGROUP_SIZE_Z) in;

__CS_FILL_RGBA__

layout (binding = 0, rgba16f) uniform image3D i_target;

uniform vec4 u_fill_value = vec4(0);

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	imageStore(i_target, pos, u_fill_value);
}

__CS_FILL_R__

layout (binding = 0, r16f) uniform image3D i_target;

uniform float u_fill_value = 0;

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	imageStore(i_target, pos, vec4(u_fill_value));
}

__CS_FILL_BOUNDARY__

layout (binding = 0, r16f) uniform image3D i_obstacle;

uniform float u_boundary_value = 0.1f;

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	vec4 target_value = vec4(0);

	if (pos.x == 0 || pos.x == gl_NumWorkGroups.x * gl_WorkGroupSize.x - 1) target_value.x = u_boundary_value;
	if (pos.y == 0 || pos.y == gl_NumWorkGroups.y * gl_WorkGroupSize.y - 1) target_value.x = u_boundary_value;
	if (pos.z == 0 || pos.z == gl_NumWorkGroups.z * gl_WorkGroupSize.z - 1) target_value.x = u_boundary_value;
	
	imageStore(i_obstacle, pos, target_value);
}

__CS_FILL_OBSTACLE_BALL__

layout (binding = 0, r16f) uniform image3D i_obstacle;

uniform vec3 u_location;
uniform float u_radius;

uniform float u_boundary_value = 0.1f;

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	vec3 coord = vec3(pos) / vec3(gl_NumWorkGroups * gl_WorkGroupSize - 1);
	vec4 target_value = vec4(0);
	
	if (pos.x == 0 || pos.x == gl_NumWorkGroups.x * gl_WorkGroupSize.x - 1) target_value.x = u_boundary_value;
	if (pos.y == 0 || pos.y == gl_NumWorkGroups.y * gl_WorkGroupSize.y - 1) target_value.x = u_boundary_value;
	if (pos.z == 0 || pos.z == gl_NumWorkGroups.z * gl_WorkGroupSize.z - 1) target_value.x = u_boundary_value;
	
	if (distance(u_location, coord) < u_radius) target_value.x = 1;
	
	imageStore(i_obstacle, pos, target_value);
}

__CS_FILL_OBSTACLE_BOX__

layout (binding = 0, r16f) uniform image3D i_obstacle;

uniform vec3 u_location;
uniform vec3 u_extend;

uniform float u_boundary_value = 0.1f;

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID);
	vec3 coord = vec3(pos) / vec3(gl_NumWorkGroups * gl_WorkGroupSize - 1);
	vec4 target_value = vec4(0);

	if (pos.x == 0 || pos.x == gl_NumWorkGroups.x * gl_WorkGroupSize.x - 1) target_value.x = u_boundary_value;
	if (pos.y == 0 || pos.y == gl_NumWorkGroups.y * gl_WorkGroupSize.y - 1) target_value.x = u_boundary_value;
	if (pos.z == 0 || pos.z == gl_NumWorkGroups.z * gl_WorkGroupSize.z - 1) target_value.x = u_boundary_value;
	
	if (abs(coord.x - u_location.x) < u_extend.x && abs(coord.y - u_location.y) < u_extend.y && abs(coord.z - u_location.z) < u_extend.z) target_value.x = 1;
	
	imageStore(i_obstacle, pos, target_value);
}

