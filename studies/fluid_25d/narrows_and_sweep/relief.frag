#version 330 core
uniform sampler2D depth;
uniform sampler2D bed;
uniform int surface;
uniform sampler2D idle_stage;
uniform sampler2D channel_floor;
uniform vec3 eye;
uniform vec3 sun_direction;
uniform vec3 sun_tint;
in vec3 world_position;
in float physical_height;
out vec4 out_color;

vec3 ink(vec3 rgb) { return rgb / 255.0; }

float band(float value, float level, float span) {
    float aa = max(span, fwidth(value) * 0.55);
    return smoothstep(level - aa, level + aa, value);
}

float line(float value, float level, float span) {
    float aa = max(span, fwidth(value) * 0.60);
    return 1.0 - smoothstep(aa * 0.24, aa, abs(value - level));
}

vec3 paper(float relief) {
    vec3 color = ink(vec3(239,226,193));
    color = mix(color, ink(vec3(224,206,167)), 1.0 - band(relief, 3.38, 0.010));
    color = mix(color, ink(vec3(205,183,146)), 1.0 - band(relief, 3.30, 0.010));
    color = mix(color, ink(vec3(233,212,167)), 1.0 - band(relief, 3.10, 0.010));
    color = mix(color, ink(vec3(194,180,149)), 1.0 - band(relief, 3.02, 0.010));
    color = mix(color, ink(vec3(241,225,188)), 1.0 - band(relief, 2.97, 0.010));
    color = mix(color, ink(vec3(182,183,154)), 1.0 - band(relief, 2.87, 0.010));
    float contour = 0.0;
    float levels[8] = float[](2.78, 2.86, 3.02, 3.10, 3.16, 3.22, 3.28, 3.34);
    for (int i = 0; i < 8; i++) {
        contour = max(contour, line(relief, levels[i], 0.009));
    }
    return mix(color, ink(vec3(103,112,110)), contour * 0.58);
}

// Palette coordinates are metres of surface rise above the initial wet stage
// in this cross-section. The knots and both endpoints are fixed in time.
vec3 stage_pigment(float rise) {
    const int count = 7;
    float stops[count] = float[](-0.35, 0.0, 0.20, 0.50, 0.85, 1.15, 1.55);
    vec3 colors[count] = vec3[](vec3(228,239,225), vec3(160,217,187),
                                vec3(73,183,167), vec3(24,143,158),
                                vec3(32,110,154), vec3(38,63,116),
                                vec3(37,37,64));
    vec3 color = colors[0];
    for (int i = 1; i < count; ++i) {
        float blend = clamp((rise-stops[i-1])/(stops[i]-stops[i-1]),0.0,1.0);
        color = mix(color, colors[i], blend);
    }
    return ink(color);
}

// Mark only the three principal stages. Measure the stroke in pixels so
// a nearly level surface at a threshold never becomes a dark patch.
float stage_lines(float rise) {
    float separation = min(abs(rise - 0.30), min(abs(rise - 0.60), abs(rise - 0.90)));
    float per_pixel = length(vec2(dFdx(rise),dFdy(rise)));
    // Physical mark thickness would turn a flat plateau near a threshold
    // into a solid patch. Keep a three-pixel stroke in the master image.
    float distance_px = separation / max(per_pixel,0.00005);
    return (1.0-smoothstep(0.90,2.10,distance_px))*smoothstep(0.00005,0.00035,per_pixel);
}

float bank_light(vec3 point, vec3 light) {
    float block = 0.0;
    for (int step = 1; step <= 6; step++) {
        float distance = float(step) * 1.25;
        vec3 probe = point + light * distance;
        vec2 field = (probe.xy + vec2(0.5)) / vec2(256.0,96.0);
        float elevation = 6.5 + (texture(bed, field).r - 6.5) * 1.72;
        float obstruction = elevation - probe.z - 0.16;
        block = max(block, smoothstep(0.0, 0.55 + 0.09 * distance, obstruction));
    }
    return 1.0 - block;
}

void main() {
    vec3 raw_normal = cross(dFdx(world_position), dFdy(world_position));
    vec3 normal = normalize(raw_normal);
    if (normal.z < 0.0) {
        normal = -normal;
    }
    vec3 light = sun_direction;
    float incidence = max(dot(normal, light), 0.0);
    vec2 xy = world_position.xy;
    vec2 uv = (xy + vec2(0.5)) / vec2(256.0,96.0);
    float h = texture(depth, uv).r;
    float texture_scale = max(length(dFdx(xy)), length(dFdy(xy)));
    float texture_fade = 1.0 - smoothstep(0.08,0.19,texture_scale);
    float grain = (sin(xy.x*31.3+xy.y*39.6) * sin(xy.x*53.4-xy.y*21.5)) * texture_fade;
    float flecks = (sin(xy.x*13.76+xy.y*9.92) + sin(xy.x*7.4-xy.y*15.8)) * texture_fade;

    if (surface == 2) {
        float striation = 0.84 + 0.11 * sin(physical_height * 3.8);
        vec3 flank = ink(vec3(160,140,108)) * striation * (0.78 + 0.22 * incidence * sun_tint);
        out_color = vec4(flank, 1.0);
        return;
    }
    if (surface == 0) {
        float relative_relief = physical_height - texture(channel_floor,uv).r;
        vec3 dry = paper(relative_relief);
        // Shading is applied to the actual slope, with a restrained carved-paper contrast.
        dry *= 0.67 + 0.46 * incidence * bank_light(world_position, light) * sun_tint;
        dry += (grain * 0.013 + flecks * 0.004);
        out_color = vec4(clamp(dry, 0.0, 1.0), 1.0);
        return;
    }

    float wet = smoothstep(0.026, max(0.065, min(0.095, 0.035 + fwidth(h))), h);
    if (wet <= 0.0) discard;
    // A continuous normal for the optical surface, independent of the rendering triangles.
    vec2 dx = vec2(0.52/256.0,0.0);
    vec2 dy = vec2(0.0,0.52/96.0);
    vec2 grad = (vec2(texture(bed,uv+dx).r-texture(bed,uv-dx).r,
                      texture(bed,uv+dy).r-texture(bed,uv-dy).r) +
                 vec2(texture(depth,uv+dx).r-texture(depth,uv-dx).r,
                      texture(depth,uv+dy).r-texture(depth,uv-dy).r)) / 1.04;
    vec3 water_normal = surface == 1 ? normalize(vec3(-1.72*grad,1.0)) : normal;
    vec3 to_eye = normalize(eye-world_position);
    float visibility = max(0.05, dot(water_normal,to_eye));
    float path = h/max(0.46,visibility);
    vec2 under_xy = xy - to_eye.xy*(0.34*h);
    float under_z = texture(bed,(under_xy+0.5)/vec2(256,96)).r;
    float floor_z = texture(channel_floor,(under_xy+0.5)/vec2(256,96)).r;
    float under_relief = under_z-floor_z;
    // Sample the actual displaced bed color along a short refracted sightline.
    vec3 substrate = paper(under_relief) * ink(vec3(183,220,219));
    float bedding = max(line(under_relief,0.19,0.022),line(under_relief,0.40,0.022));
    substrate *= 1.0-0.15*bedding;
    float rise = texture(bed,uv).r + h - texture(idle_stage,uv).r;
    vec3 body = stage_pigment(rise);
    if (surface == 1) {
        body = mix(body, body * 0.42, 0.58 * stage_lines(rise));
    }
    float transmission = exp(-1.63 * path);
    vec3 volume = mix(body, substrate, transmission);
    float sunlit = bank_light(world_position,light);
    vec3 reflected = reflect(-to_eye,water_normal);
    float glancing = pow(1.0-clamp(visibility,0.0,1.0),3.0);
    float skylight = smoothstep(-0.3,0.75,reflected.z);
    vec3 env = mix(ink(vec3(73,119,149)),ink(vec3(197,220,218)),skylight);
    vec3 color = volume*(0.845+0.22*(0.52+max(0.,dot(water_normal,light)))*sunlit);
    color = mix(color,env,(0.018+0.38*glancing)*(0.60+0.40*sunlit));
    if (surface == 1) {
        float slope = smoothstep(0.037,0.118,-grad.x);
        float dx_l = 1.55/256.0;
        vec2 l = uv-vec2(dx_l,0), r = uv+vec2(dx_l,0);
        float convex = texture(bed,uv).r+h-0.5*(texture(bed,l).r+texture(depth,l).r+
                                                  texture(bed,r).r+texture(depth,r).r);
        float above_stage = rise;
        float crest = slope*smoothstep(-0.004,0.031,convex)*smoothstep(0.28,0.72,h)
                      *smoothstep(0.16,0.40,above_stage);
        // The descending face reflects a long soft light, dimming toward the foot.
        color = mix(color,ink(vec3(137,192,205)),0.12*slope*smoothstep(0.15,0.4,above_stage));
        color = mix(color,ink(vec3(218,241,237)),0.57*crest);
        color += ink(vec3(20,25,22))*pow(max(0.0,dot(reflected,light)),20.)*sunlit*sun_tint;
    } else color *= 0.77;
    color += (grain*0.007+flecks*0.002);
    out_color = vec4(clamp(color,0.,1.),wet);
}
