// Preserve the legacy full-grid operator. Split grids use the 128-wide reference
// spacing so increasing only material resolution does not double the flow forcing.
float pyro_stencil_scale(ivec3 flow_size) {
    return float(flow_size.x) == params.grid_dt_time.x ? 1.0 : float(flow_size.x) / 128.0;
}
