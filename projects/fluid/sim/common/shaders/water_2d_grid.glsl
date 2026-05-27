#ifndef CUBEY_WATER_2D_GRID_GLSL
#define CUBEY_WATER_2D_GRID_GLSL

uint cell_index(uint x, uint y, uint width) {
    return (y * width) + x;
}

uint u_index(uint x, uint y, uint width) {
    return (y * (width + 1u)) + x;
}

uint v_index(uint x, uint y, uint width) {
    return (y * width) + x;
}

uint cell_index(ivec2 coord, uint width, uint height) {
    ivec2 clamped = clamp(coord, ivec2(0), ivec2(int(width) - 1, int(height) - 1));
    return cell_index(uint(clamped.x), uint(clamped.y), width);
}

uint u_index(ivec2 coord, uint width, uint height) {
    ivec2 clamped = ivec2(clamp(coord.x, 0, int(width)), clamp(coord.y, 0, int(height) - 1));
    return u_index(uint(clamped.x), uint(clamped.y), width);
}

uint v_index(ivec2 coord, uint width, uint height) {
    ivec2 clamped = ivec2(clamp(coord.x, 0, int(width) - 1), clamp(coord.y, 0, int(height)));
    return v_index(uint(clamped.x), uint(clamped.y), width);
}

bool in_bounds(int x, int y, uint width, uint height) {
    return x >= 0 && y >= 0 && x < int(width) && y < int(height);
}

#endif
