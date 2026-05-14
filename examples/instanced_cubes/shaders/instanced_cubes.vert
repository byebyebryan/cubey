#version 450

layout(push_constant) uniform PushConstants {
    mat4 view_projection;
    mat4 cube_spin;
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec3 in_normal;
layout(location = 3) in vec4 in_model_col0;
layout(location = 4) in vec4 in_model_col1;
layout(location = 5) in vec4 in_model_col2;
layout(location = 6) in vec4 in_model_col3;
layout(location = 7) in vec4 in_instance_color;

layout(location = 0) out vec3 frag_color;
layout(location = 1) out vec3 frag_normal;

void main() {
    mat4 model = mat4(in_model_col0, in_model_col1, in_model_col2, in_model_col3);
    mat4 world_model = model * pc.cube_spin;
    gl_Position = pc.view_projection * world_model * vec4(in_position, 1.0);
    frag_color = in_color * in_instance_color.rgb;
    frag_normal = normalize(mat3(world_model) * in_normal);
}
