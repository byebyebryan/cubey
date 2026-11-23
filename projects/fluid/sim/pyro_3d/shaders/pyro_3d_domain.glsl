// World-space height is independent of material and flow grid resolution.
vec3 pyro_domain_size() {
    return vec3(1.0, PYRO_DOMAIN_HEIGHT, 1.0);
}

// A tall column vents through its top. The cube damps both horizontal faces.
float pyro_wall_y(float y) {
    return PYRO_DOMAIN_HEIGHT > 1.0 ? y : min(y, 1.0 - y);
}
