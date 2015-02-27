
__VS_SHADOW_PASS__

uniform mat4 u_mvp_mat;

layout(location = 0) in vec4 in_vertex_position;

void main() {
	gl_Position = u_mvp_mat * in_vertex_position;
}

__FS_SHADOW_PASS__

out vec4 out_color;

void main() {
	out_color = vec4(1.0);
}

__VS_FIRST_PASS__

uniform mat4 u_mvp_mat;
uniform mat4 u_mv_mat;
uniform mat4 u_m_mat;
uniform mat3 u_normal_mat;

layout(location = 0) in vec4 in_vertex_position;
layout(location = 1) in vec3 in_vertex_normal;

out vec3 var_normal;
out vec4 var_position;
//out vec4 var_ws_position;

void main() {
	gl_Position = u_mvp_mat * in_vertex_position;
	var_position = u_mv_mat * in_vertex_position;
	var_normal = normalize(u_normal_mat * in_vertex_normal);
	//var_ws_position = u_m_mat * in_vertex_position;
}

__FS_FIRST_PASS__

uniform vec3 u_ambient_light_color = vec3(0.1, 0.1, 0.1);
uniform vec4 u_light_position;
uniform vec3 u_light_color = vec3(1.0, 1.0, 1.0);
uniform float u_shininess = 100.0;
uniform float u_light_attenuation_constant = 1.0;
uniform float u_light_attenuation_linear = 0.1;
uniform float u_light_attenuation_quadratic = 0.25;

uniform vec4 u_light_position_ws;

uniform mat4 u_iv_mat;

layout (binding = 0) uniform samplerCubeShadow t_shadow_map;

in vec3 var_normal;
in vec4 var_position;
//in vec4 var_ws_position;

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
	vec3 light_direction = vec3(u_light_position) - vec3(var_position);
	float light_distance = length(light_direction);
	
	vec3 light_dir_ws = vec3(u_iv_mat * (var_position - u_light_position));
	
	float d = VectorToDepthValue(light_dir_ws);
	
	light_direction = light_direction / light_distance;
	
	float attenuation = 1.0 / (u_light_attenuation_constant + u_light_attenuation_linear * light_distance + u_light_attenuation_quadratic * light_distance * light_distance);
	
	vec3 half_vector = normalize(light_direction + normalize( - var_position.xyz));
	
	float s = texture(t_shadow_map, vec4(normalize(light_dir_ws), d));
	
	//float l = 0.0;
	//if (s + 0.0001 > d) l = 1.0;
	
	float diffuse = max(0.0, dot(var_normal, light_direction));
	float specular = max(0.0, dot(var_normal, half_vector));
	
	specular = pow(specular, u_shininess)*u_shininess / 10.0;
	if (diffuse == 0.0) specular = 0.0;
	
	vec3 scattered_light = u_ambient_light_color + s * diffuse * u_light_color * attenuation;
	vec3 reflected_light = s * u_light_color * specular * attenuation;
	//scattered_light = vec3(ivec3(8 * scattered_light))/8.0;
	//float luminance = dot(scattered_light, vec3(0.2126, 0.7152, 0.0722));
	vec3 rgb = min(vec3(1.0), scattered_light + reflected_light);

	out_color = vec4(rgb, 1.0);
	//out_color = vec4(d, 0.0, 0.0, 1.0);
	//out_color = vec4(vec3(light_dir_ws), 1.0);
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
