#ifndef CUBEY_WATER_3D_GRID_GLSL
#define CUBEY_WATER_3D_GRID_GLSL

uint cell_index(uint x, uint y, uint z, uint width, uint height) {
    return ((z * height) + y) * width + x;
}

uint u_index(uint x, uint y, uint z, uint width, uint height) {
    return ((z * height) + y) * (width + 1u) + x;
}

uint v_index(uint x, uint y, uint z, uint width, uint height) {
    return ((z * (height + 1u)) + y) * width + x;
}

uint w_index(uint x, uint y, uint z, uint width, uint height) {
    return ((z * height) + y) * width + x;
}

uint cell_index(ivec3 coord, uint width, uint height, uint depth) {
    ivec3 clamped =
        clamp(coord, ivec3(0), ivec3(int(width) - 1, int(height) - 1, int(depth) - 1));
    return cell_index(uint(clamped.x), uint(clamped.y), uint(clamped.z), width, height);
}

uint u_index(ivec3 coord, uint width, uint height, uint depth) {
    ivec3 clamped = ivec3(clamp(coord.x, 0, int(width)),
                          clamp(coord.y, 0, int(height) - 1),
                          clamp(coord.z, 0, int(depth) - 1));
    return u_index(uint(clamped.x), uint(clamped.y), uint(clamped.z), width, height);
}

uint v_index(ivec3 coord, uint width, uint height, uint depth) {
    ivec3 clamped = ivec3(clamp(coord.x, 0, int(width) - 1),
                          clamp(coord.y, 0, int(height)),
                          clamp(coord.z, 0, int(depth) - 1));
    return v_index(uint(clamped.x), uint(clamped.y), uint(clamped.z), width, height);
}

uint w_index(ivec3 coord, uint width, uint height, uint depth) {
    ivec3 clamped = ivec3(clamp(coord.x, 0, int(width) - 1),
                          clamp(coord.y, 0, int(height) - 1),
                          clamp(coord.z, 0, int(depth)));
    return w_index(uint(clamped.x), uint(clamped.y), uint(clamped.z), width, height);
}

uint u_face_count(uint width, uint height, uint depth) {
    return (width + 1u) * height * depth;
}

uint v_face_count(uint width, uint height, uint depth) {
    return width * (height + 1u) * depth;
}

uint w_face_count(uint width, uint height, uint depth) {
    return width * height * (depth + 1u);
}

uint total_face_count(uint width, uint height, uint depth) {
    return u_face_count(width, height, depth) + v_face_count(width, height, depth) +
           w_face_count(width, height, depth);
}

bool in_bounds(int x, int y, int z, uint width, uint height, uint depth) {
    return x >= 0 && y >= 0 && z >= 0 && x < int(width) && y < int(height) && z < int(depth);
}

#endif
