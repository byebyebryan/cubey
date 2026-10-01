// Local float high/low expansions retain discarded conserved increments.
// This is compensated storage/update arithmetic, not a mass redistribution.
vec2 conserved_add(vec2 a, vec2 b) {
    precise float s = a.x + b.x;
    precise float v = s - a.x;
    precise float e = (a.x - (s - v)) + (b.x - v);
    precise float tail = e + (a.y + b.y);
    precise float hi = s + tail;
    precise float lo = tail - (hi - s);
    return vec2(hi, lo);
}
vec2 conserved_scale(vec2 a, float b) {
    precise float p = a.x * b;
    precise float e = fma(a.x, b, -p);
    precise float tail = e + a.y * b;
    return conserved_add(vec2(p, 0.0), vec2(tail, 0.0));
}
vec2 conserved_nonnegative(vec2 a) {
    return a.x < 0.0 || (a.x == 0.0 && a.y < 0.0) ? vec2(0.0) : a;
}
struct ConservationResidual {
    // The canonical high fields remain h and tracer q; these are their tails.
    vec4 state;
    vec4 water_ledger;
    vec4 tracer_ledger;
};
