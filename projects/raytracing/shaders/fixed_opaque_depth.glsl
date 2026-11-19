// The hit was traced with the jittered lane. Projection jitter shifts clip X/Y only,
// so the unjittered matrix gives the same Vulkan forward Z at this hit.
float fixedOpaqueDepth(vec3 hitPoint, bool hit, mat4 viewProjection) {
    if (!hit)
        return 1.0;
    vec4 clip = viewProjection * vec4(hitPoint, 1.0);
    return clip.w > 0.0 ? clamp(clip.z / clip.w, 0.0, 1.0) : 1.0;
}
