#ifndef SDF_CACHED_CELL_MATH
#define SDF_CACHED_CELL_MATH
// Exact trilinear reconstruction and a conservative cell-local gradient bound.
float cachedCellValue(float v[8],vec3 f) {
    return mix(mix(mix(v[0],v[1],f.x),mix(v[2],v[3],f.x),f.y),
               mix(mix(v[4],v[5],f.x),mix(v[6],v[7],f.x),f.y),f.z);
}
float cachedCellLipschitz(float v[8],float spacing) {
    vec3 edge=vec3(max(max(abs(v[1]-v[0]),abs(v[3]-v[2])),max(abs(v[5]-v[4]),abs(v[7]-v[6]))),
                   max(max(abs(v[2]-v[0]),abs(v[3]-v[1])),max(abs(v[6]-v[4]),abs(v[7]-v[5]))),
                   max(max(abs(v[4]-v[0]),abs(v[5]-v[1])),max(abs(v[6]-v[2]),abs(v[7]-v[3]))));
    return max(length(edge)/spacing,1e-5);
}
#endif
