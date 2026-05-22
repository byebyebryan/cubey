#ifndef CUBEY_WATER_2D_CONTRACT_GLSL
#define CUBEY_WATER_2D_CONTRACT_GLSL

#define WATER2D_BINDING_PARTICLE_POSITIONS 0
#define WATER2D_BINDING_PARTICLE_VELOCITIES 1
#define WATER2D_BINDING_U_FIELD 2
#define WATER2D_BINDING_U_PREVIOUS 3
#define WATER2D_BINDING_V_FIELD 4
#define WATER2D_BINDING_V_PREVIOUS 5
#define WATER2D_BINDING_U_WEIGHT 6
#define WATER2D_BINDING_V_WEIGHT 7
#define WATER2D_BINDING_PRESSURE_A 8
#define WATER2D_BINDING_PRESSURE_B 9
#define WATER2D_BINDING_DIVERGENCE 10
#define WATER2D_BINDING_SOLID 11
#define WATER2D_BINDING_CELL_COUNTS 12
#define WATER2D_BINDING_CELL_PARTICLE_INDICES 13

#define WATER2D_EMPTY_PARTICLE 0xffffffffu

#define WATER2D_SIMULATION_PARAMS                                                              \
    layout(push_constant) uniform SimulationParams {                                            \
        vec4 grid_dt_time;                                                                      \
        vec4 init_options;                                                                      \
        vec4 obstacle_options;                                                                  \
        vec4 particle_options;                                                                  \
        vec4 solve_options;                                                                     \
    } params

#define WATER2D_RENDER_PARAMS                                                                  \
    layout(push_constant) uniform RenderParams {                                                \
        vec4 grid_debug;                                                                        \
        vec4 particle_options;                                                                  \
    } params

#endif
