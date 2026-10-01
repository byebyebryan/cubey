// Read-only, committed-update accounting. Each high/low expansion represents
// the independently accumulated deltas, not a correction to hydraulic state.
// TwoSum and the explicit product residual require non-reassociated arithmetic.
struct MassAudit {
    vec4 water_hi[3];
    vec4 water_lo[3];
    vec4 tracer_hi[3];
    vec4 tracer_lo[3];
};

vec2 audit_add(vec2 a, vec2 b) {
    precise float s = a.x + b.x;
    precise float v = s - a.x;
    precise float e = (a.x - (s - v)) + (b.x - v);
    precise float tail = e + (a.y + b.y);
    precise float hi = s + tail;
    precise float lo = tail - (hi - s);
    return vec2(hi, lo);
}
vec2 audit_difference(float a, float b) {
    return audit_add(vec2(a, 0.0), vec2(-b, 0.0));
}
vec2 audit_scale(vec2 a, float b) {
    precise float p = a.x * b;
    precise float e = fma(a.x, b, -p);
    precise float tail = e + a.y * b;
    return audit_add(vec2(p, 0.0), vec2(tail, 0.0));
}
void audit_set_water(inout MassAudit a, uint term, vec2 value) {
    a.water_hi[term / 4u][term % 4u] = value.x;
    a.water_lo[term / 4u][term % 4u] = value.y;
}
void audit_set_tracer(inout MassAudit a, uint term, vec2 value) {
    a.tracer_hi[term / 4u][term % 4u] = value.x;
    a.tracer_lo[term / 4u][term % 4u] = value.y;
}
MassAudit audit_zero() {
    MassAudit a;
    for (uint i = 0u; i < 3u; ++i) {
        a.water_hi[i] = vec4(0.0);
        a.water_lo[i] = vec4(0.0);
        a.tracer_hi[i] = vec4(0.0);
        a.tracer_lo[i] = vec4(0.0);
    }
    return a;
}
MassAudit audit_accumulate(MassAudit a, MassAudit b) {
    for (uint term = 0u; term < 12u; ++term) {
        uint i = term / 4u;
        uint c = term % 4u;
        audit_set_water(a, term, audit_add(vec2(a.water_hi[i][c], a.water_lo[i][c]),
                                           vec2(b.water_hi[i][c], b.water_lo[i][c])));
        audit_set_tracer(a, term, audit_add(vec2(a.tracer_hi[i][c], a.tracer_lo[i][c]),
                                            vec2(b.tracer_hi[i][c], b.tracer_lo[i][c])));
    }
    return a;
}
// Bit-preserving observations keep audit NoContraction decorations local,
// rather than intentionally imposing a new arithmetic policy on the solver.
float audit_observe(float value) {
    return uintBitsToFloat(floatBitsToUint(value));
}
