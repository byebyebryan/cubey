
__VS_2D_SHADOW_PASS__

uniform mat4 u_mvp_mat;

layout(location = 0) in vec4 in_vertex_position;

void main() {
	gl_Position = u_mvp_mat * in_vertex_position;
}

__FS_2D_SHADOW_PASS__

out vec4 out_color;

void main() {
	out_color = vec4(1.0);
}

__VS_CUBE_SHADOW_PASS__

layout(location = 0) in vec4 in_vertex_position;

void main() {
	gl_Position = in_vertex_position;
}

__GS_CUBE_SHADOW_PASS__

layout (triangles, invocations = 6) in;
layout (triangle_strip, max_vertices = 3) out;

uniform mat4 u_mvp_mats[6];

void main() {
	for (int i=0; i< 3; i++) {
		gl_Layer = gl_InvocationID;
		gl_Position = u_mvp_mats[gl_InvocationID]* gl_in[i].gl_Position;
		EmitVertex();
	}
	EndPrimitive();
}

__FS_CUBE_SHADOW_PASS__

out vec4 out_color;

void main() {
	out_color = vec4(1.0);
}

__VS_CUBE_REFLECTION_PASS__

uniform mat4 u_model_mat;
uniform mat3 u_normal_mat;

layout(location = 0) in vec4 in_vertex_position;
layout(location = 1) in vec3 in_vertex_normal;

out vec3 pass_normal;
out vec4 pass_position;

void main() {
	gl_Position = in_vertex_position;
	pass_position = u_model_mat * in_vertex_position;
	pass_normal = normalize(u_normal_mat * in_vertex_normal);
	pass_normal.x = - pass_normal.x;
}

__GS_CUBE_REFLECTION_PASS__

layout (triangles, invocations = 6) in;
layout (triangle_strip, max_vertices = 3) out;

uniform mat4 u_mvp_mats[6];

in vec3 pass_normal[];
in vec4 pass_position[];

out vec3 var_normal;
out vec4 var_position;

void main() {
	for (int i=0; i< 3; i++) {
		gl_Layer = gl_InvocationID;
		gl_Position = u_mvp_mats[gl_InvocationID] * gl_in[i].gl_Position;
		var_normal = pass_normal[i];
		var_position = pass_position[i];
		EmitVertex();
	}
	EndPrimitive();
}

__FS_CUBE_REFLECTION_PASS__

struct PointLight {
	vec3 position;
	vec3 ambient;
	vec3 color;
	vec3 attenuation;
	samplerCubeShadow t_shadow_map;
};

struct Material {
	vec3 emission;
	vec3 diffuse;
	vec3 specular;
	float shininess;
	float alpha;
};

uniform PointLight u_light;
uniform Material u_material;
uniform vec3 u_eye_position;

in vec3 var_normal;
in vec4 var_position;

out vec4 out_color;

float VectorToDepthValue(vec3 Vec)
{
    vec3 AbsVec = abs(Vec);
    float LocalZcomp = max(AbsVec.x, max(AbsVec.y, AbsVec.z));

    const float f = 100.0;
    const float n = 0.1;
    float NormZComp = (f+n) / (f-n) - (2*f*n)/(f-n)/LocalZcomp;
    return (NormZComp + 1.0) * 0.5;
}

void main() {
	vec3 light_direction = u_light.position - var_position.xyz;
	float light_distance = length(light_direction);
	
	float shadow_depth = VectorToDepthValue(light_direction);
	
	light_direction = light_direction / light_distance;
	light_direction.x = -light_direction.x;
	
	float attenuation = 1.0 / (u_light.attenuation.x + u_light.attenuation.y * light_distance + u_light.attenuation.z * light_distance * light_distance);
	
	vec3 eye_direction = normalize(u_eye_position - var_position.xyz);
	//eye_direction.x = - eye_direction.x;
	vec3 half_vector = normalize(eye_direction + light_direction);
	
	float light_amount = texture(u_light.t_shadow_map, vec4(-light_direction, shadow_depth - 0.001));
	
	float diffuse = max(0.0, dot(var_normal, light_direction));
	float specular = max(0.0, dot(var_normal, half_vector));
	
	specular = diffuse * pow(specular, u_material.shininess)*u_material.shininess / 10.0;
	if (diffuse == 0.0) specular = 0.0;
	
	vec3 scattered_light = (u_light.ambient + light_amount * diffuse * u_light.color * attenuation) * u_material.diffuse;
	vec3 reflected_light = light_amount * u_light.color * specular * attenuation * u_material.specular;
	vec3 rgb = min(vec3(1.0), scattered_light + reflected_light + u_material.emission);
	const vec3 gamma = vec3(1.0/2.2);
	
	out_color = vec4(pow(rgb, gamma), u_material.alpha);
}

__VS_RENDER_PASS__

uniform mat4 u_mvp_mat;
uniform mat4 u_model_mat;
uniform mat3 u_normal_mat;

layout(location = 0) in vec4 in_vertex_position;
layout(location = 1) in vec3 in_vertex_normal;

out vec3 var_normal;
out vec4 var_position;

void main() {
	gl_Position = u_mvp_mat * in_vertex_position;
	var_position = u_model_mat * in_vertex_position;
	var_normal = normalize(u_normal_mat * in_vertex_normal);
	var_normal.x = - var_normal.x;
}

__FS_RENDER_PASS__

struct PointLight {
	vec3 position;
	vec3 ambient;
	vec3 color;
	vec3 attenuation;
	samplerCubeShadow t_shadow_map;
};

struct Material {
	vec3 emission;
	vec3 diffuse;
	vec3 specular;
	float shininess;
	float alpha;
};

uniform PointLight u_light;
uniform Material u_material;
uniform vec3 u_eye_position;

uniform samplerCube t_reflection_map;
uniform int u_reflection_enabled;

in vec3 var_normal;
in vec4 var_position;

out vec4 out_color;

float VectorToDepthValue(vec3 Vec)
{
    vec3 AbsVec = abs(Vec);
    float LocalZcomp = max(AbsVec.x, max(AbsVec.y, AbsVec.z));

    const float f = 100.0;
    const float n = 0.1;
    float NormZComp = (f+n) / (f-n) - (2*f*n)/(f-n)/LocalZcomp;
    return (NormZComp + 1.0) * 0.5;
}

void main() {
	vec3 light_direction = u_light.position - var_position.xyz;
	float light_distance = length(light_direction);
	
	float shadow_depth = VectorToDepthValue(light_direction);
	
	light_direction = light_direction / light_distance;
	light_direction.x = -light_direction.x;
	
	float attenuation = 1.0 / (u_light.attenuation.x + u_light.attenuation.y * light_distance + u_light.attenuation.z * light_distance * light_distance);
	
	vec3 var_p = var_position.xyz;
	var_p.x = - var_p.x;
	vec3 eye_direction = normalize(u_eye_position - var_p);
	//eye_direction.x = - eye_direction.x;
	vec3 half_vector = normalize(eye_direction + light_direction);
	
	float light_amount = texture(u_light.t_shadow_map, vec4(light_direction, shadow_depth - 0.001));
	
	float diffuse = max(0.0, dot(var_normal, light_direction));
	float specular = max(0.0, dot(var_normal, half_vector));
	
	specular = diffuse * pow(specular, u_material.shininess)*u_material.shininess / 10.0;
	//if (diffuse == 0.0) specular = 0.0;
	
	//eye_direction.x = - eye_direction.x;
	vec3 diffuse_color = u_material.diffuse;
	if (u_reflection_enabled == 1) {
		vec3 reflection_direction = reflect(-eye_direction, var_normal);
		
		//reflection_direction = reflection_direction + var_position.xyz;
		
		//reflection_direction.y = -reflection_direction.y;
		diffuse_color = texture(t_reflection_map, -reflection_direction).xyz * 0.8 + diffuse_color * 0.2;
	}
	
	vec3 scattered_light = (u_light.ambient + light_amount * diffuse * u_light.color * attenuation) * diffuse_color;
	vec3 reflected_light = light_amount * u_light.color * specular * attenuation * u_material.specular;
	vec3 rgb = min(vec3(1.0), scattered_light + reflected_light + u_material.emission);
	const vec3 gamma = vec3(1.0/2.2);
	
	out_color = vec4(pow(rgb, gamma), u_material.alpha);
}

__VS_SECOND_PASS__

layout(location = 0) in vec4 in_position;
layout(location = 1) in vec4 in_tex_coord;

out vec4 var_tex_coord;

void main() {
	gl_Position = in_position;
	var_tex_coord = in_tex_coord;
}

__FS_SECOND_PASS__

layout (binding = 0) uniform sampler2D t_tex;

in vec4 var_tex_coord;

out vec4 out_color;

void main() {
	out_color = texture(t_tex, var_tex_coord.xy);
}
