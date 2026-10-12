#version 450
struct Vertex { vec4 position_wet; vec4 normal; vec4 surface; };
layout(set=0,binding=0,std430) readonly buffer Vertices { Vertex values[]; } vertices;
layout(push_constant) uniform Params {
    mat4 view_projection; vec4 grid_cell; vec4 camera_wet; vec4 presentation; vec4 terrain_palette;
} params;
layout(location=0) out vec3 world_position;
layout(location=1) out vec3 world_normal;
layout(location=2) out vec2 world_xz;
layout(location=3) out vec4 context_surface;
layout(location=4) out float context_wet;
void main() {
    Vertex v=vertices.values[gl_VertexIndex];
    world_position=v.position_wet.xyz;
    world_normal=v.normal.xyz;
    world_xz=world_position.xz;
    context_surface=v.surface;
    context_wet=v.position_wet.w;
    gl_Position=params.view_projection*vec4(world_position,1);
}
