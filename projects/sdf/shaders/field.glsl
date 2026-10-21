#ifndef SDF_FIELD_GLSL
#define SDF_FIELD_GLSL
const uint NEW_EDIT = 0x80000000u;
const uint OVERWRITTEN = 0x40000000u;
const uint SUBDIVIDED = 0x20000000u;
const uint FILLED = 0x10000000u;
const uint POINTER = 0x00ffffffu;
const uint LEVEL[9] = uint[9](0, 1, 9, 73, 585, 4681, 37449, 299593, 2396745);
struct Edit { vec4 center_operation; vec4 inverse_rotation; vec4 extent_rounding; };
uint morton(ivec3 p) {
    uint result = 0;
    for (uint i = 0; i < 8; ++i)
        result |= ((uint(p.x) >> i) & 1u) << (3u*i) |
                  ((uint(p.y) >> i) & 1u) << (3u*i+1u) |
                  ((uint(p.z) >> i) & 1u) << (3u*i+2u);
    return result;
}
ivec3 unmorton(uint code) {
    uvec3 p = uvec3(0);
    for (uint i = 0; i < 8; ++i)
        p |= uvec3((code >> (3u*i)) & 1u, (code >> (3u*i+1u)) & 1u,
                   (code >> (3u*i+2u)) & 1u) << i;
    return ivec3(p);
}
float boxDistance(vec3 p, vec3 extent) {
    vec3 q = abs(p) - extent;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}
float editDistance(vec3 p, Edit edit) {
    vec3 v = p - edit.center_operation.xyz;
    vec4 q = edit.inverse_rotation;
    v += 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w*v);
    return max(boxDistance(v, edit.extent_rounding.xyz) - edit.extent_rounding.w,
               boxDistance(p, vec3(0.499)));
}
float applyEdit(float a, float b, Edit edit) {
    return edit.center_operation.w < 1.5 ? min(a,b) : max(a,-b);
}
ivec3 brickOrigin(uint pointer, uint axis) {
    return ivec3(pointer % axis, (pointer / axis) % axis, pointer / (axis*axis)) * 8;
}
#endif
