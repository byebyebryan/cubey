layout(push_constant) uniform SimulationParams {
    vec4 grid_dt_time;
    vec4 decay_force;
    vec4 options;
    vec4 shadow_options;
    vec4 fire_options;
    vec4 shaping_options;
    vec4 obstacle_options;
    uint fire_burner;
} params;

#define PYRO_DOMAIN_HEIGHT params.obstacle_options.w
#include "pyro_3d_domain.glsl"
