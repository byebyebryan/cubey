#version 450

struct ForcingCube {
    vec4 center_kind;
    vec4 neighbor_bed;
    vec4 neighbor_present;
};
layout(set = 0, binding = 7, std430) readonly buffer ForcingCubes {
    ForcingCube values[];
} cubes;

layout(push_constant) uniform CatchmentParams {
    mat4 view_projection;
    vec4 grid_cell;
    vec4 camera_wet;
    vec4 presentation;
    vec4 terrain_palette;
} params;

layout(location = 0) out vec2 face_uv;
layout(location = 1) flat out vec3 face_normal;
layout(location = 2) flat out float cube_kind;

const vec2 corners[4] = vec2[](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1));
const int triangles[6] = int[](0, 1, 2, 0, 2, 3);
const vec3 side_normals[4] = vec3[](vec3(-1, 0, 0), vec3(1, 0, 0),
                                     vec3(0, 0, -1), vec3(0, 0, 1));
const vec3 side_tangents[4] = vec3[](vec3(0, 0, 1), vec3(0, 0, -1),
                                      vec3(-1, 0, 0), vec3(1, 0, 0));

void main() {
    ForcingCube cube = cubes.values[gl_InstanceIndex];
    float half_side = 0.5 * params.grid_cell.z;
    vec3 center = cube.center_kind.xyz;
    center.y *= params.grid_cell.w;
    face_uv = corners[triangles[gl_VertexIndex % 6]];
    cube_kind = cube.center_kind.w;
    int face = gl_VertexIndex / 6;
    vec3 position;
    if (face < 2) {
        float sign_y = face == 0 ? -1.0 : 1.0;
        face_normal = vec3(0, sign_y, 0);
        position = center + vec3((face_uv.x - 0.5) * params.grid_cell.z,
                                sign_y * half_side,
                                -sign_y * (face_uv.y - 0.5) * params.grid_cell.z);
    } else {
        int side = (face - 2) / 2;
        int strip = (face - 2) % 2;
        float low = -half_side;
        float high = half_side;
        if (cube.neighbor_present[side] > 0.5) {
            float neighbor_offset = (cube.neighbor_bed[side] - cube.center_kind.y) *
                                    params.grid_cell.w;
            if (strip == 0)
                high = clamp(neighbor_offset - half_side, -half_side, half_side);
            else
                low = clamp(neighbor_offset + half_side, -half_side, half_side);
        } else if (strip == 1) {
            low = half_side;
        }
        face_normal = side_normals[side];
        position = center + half_side * face_normal +
                   (face_uv.x - 0.5) * params.grid_cell.z * side_tangents[side] +
                   vec3(0, mix(low, high, face_uv.y), 0);
    }
    gl_Position = params.view_projection * vec4(position, 1);
}
