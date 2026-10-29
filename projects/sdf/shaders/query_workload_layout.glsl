// Exactly 256 bytes per sampled pixel; CPU aggregation uses 64-bit integers.
struct QueryWork {
    uvec4 totals[3];uvec4 walk[3];uvec4 runs[2];uvec4 stats;
    vec4 hit;vec4 normal;vec4 radiance;vec4 shadingNormal;
    uvec4 identity;uvec4 reserved[2];
};
