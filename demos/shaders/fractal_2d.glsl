
__VS_FRACTAL__

layout(location = 0) in vec4 in_position;
layout(location = 1) in vec2 in_tex_coord;

out vec2 var_tex_coord;

void main() {
	gl_Position = in_position;
	var_tex_coord = in_tex_coord;
}

__FS_FRACTAL__

uniform vec2 u_c = vec2(-0.75, 0.25);
uniform int u_max_iterations = 512;
uniform float u_zoom = 1.0;
uniform vec2 u_move = vec2(0, 0);
uniform int u_type = 1;
uniform int u_order = 2;

uniform float u_starting_hue = 0.0;

uniform float u_aspect_ratio;

in vec2 var_tex_coord;
out vec4 out_color;

vec3 hsl2rgb( in vec3 c )
{
    vec3 rgb = clamp( abs(mod(c.x*6.0+vec3(0.0,4.0,2.0),6.0)-3.0)-1.0, 0.0, 1.0 );

    return c.z + c.y * (rgb-0.5)*(1.0-abs(2.0*c.z-1.0));
}

vec3 hsv2rgb( in vec3 c )
{
    vec3 rgb = clamp( abs(mod(c.x*6.0+vec3(0.0,4.0,2.0),6.0)-3.0)-1.0, 0.0, 1.0 );
	
    return c.z * mix( vec3(1.0), rgb, c.y);
}

vec2 complex_multiply(vec2 a, vec2 b) {
	return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

void main() {
	
	int i;
	
	vec2 z,c,dz;
	
	vec2 p = (var_tex_coord * 2.0 - 1.0) * vec2(u_aspect_ratio, 1.0) * u_zoom + u_move * vec2(1, -1);
	
	if (u_type==0) {
		c = u_c;
		z = p;
		dz = vec2(1.0, 0.0);
	}
	else {
		c = p;
		z = vec2(0.0);
		dz = vec2(0.0);
	}
	
	for(i = 0; i< u_max_iterations; i++) {
		vec2 z_x = z;
		
		for (int j = 1; j < u_order - 1; j++) {
			z = complex_multiply(z, z_x);
		}
		
		if (u_type==0) {
			dz = u_order * complex_multiply(z, dz);
		}
		else {
			dz = u_order * complex_multiply(z, dz) + vec2(1.0, 0.0);
		}
		
		z = complex_multiply(z, z_x) + c;
		
		//if (dot(z, z) < 0.00001) break;
		if (dot(z, z) > 1024) break;
	}
	
	float d = 0.0;
	if (i < u_max_iterations) d = sqrt( dot(z, z) / dot(dz, dz) ) * log(dot(z,z));
	float c1 = clamp( 4.0*d/u_zoom, 0.0, 1.0 );
	c1 = pow(c1, 0.25);
	
	//vec3 hsv = vec3(u_starting_hue + float(i)/float(u_max_iterations), 1.0, 1.0);
	//if (i == u_max_iterations) hsv.z = 0.0;
	//vec3 rgb = hsv2rgb(hsv);
	//out_color = vec4(rgb, 1.0);
	out_color = vec4(vec3(1 - c1), 1);
}
