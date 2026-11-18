vec2 fixedOpaqueMotion(vec3 hitPoint, vec3 rayDirection, bool hit,
                       mat4 currentViewProjection, mat4 previousViewProjection,
                       vec2 extent) {
    vec2 invalid = extent + 16.0;
    vec4 world = hit ? vec4(hitPoint, 1) : vec4(rayDirection, 0);
    vec4 current = currentViewProjection * world;
    vec4 previous = previousViewProjection * world;
    bool finite = !any(isnan(current)) && !any(isinf(current)) &&
                  !any(isnan(previous)) && !any(isinf(previous));
    bool currentDepth = !hit || (current.z >= 0.0 && current.z <= current.w);
    bool previousDepth = !hit || (previous.z >= 0.0 && previous.z <= previous.w);
    if (!finite || current.w <= 1e-8 || previous.w <= 1e-8 || !currentDepth || !previousDepth)
        return invalid;
    return (previous.xy / previous.w - current.xy / current.w) * (0.5 * extent);
}
