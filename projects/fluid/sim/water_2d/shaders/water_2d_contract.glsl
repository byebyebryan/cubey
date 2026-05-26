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
#define WATER2D_BINDING_SIM_PARAMS 14
#define WATER2D_BINDING_PARTICLE_AFFINE 15

#define WATER2D_EMPTY_PARTICLE 0xffffffffu

#define WATER2D_SIMULATION_PARAMS                                                              \
    layout(set = 0, binding = WATER2D_BINDING_SIM_PARAMS, std140) uniform SimulationParams {    \
        vec4 grid_options;                                                                      \
        vec4 init_options;                                                                      \
        vec4 obstacle_options;                                                                  \
        vec4 obstacle_extents;                                                                  \
        vec4 particle_options;                                                                  \
        vec4 solve_options;                                                                     \
        vec4 lifecycle_options;                                                                 \
        vec4 hose_options0;                                                                     \
        vec4 hose_options1;                                                                     \
        vec4 hose_options2;                                                                     \
        vec4 drain_options;                                                                     \
        vec4 drain_extents;                                                                     \
    } params;                                                                                   \
    layout(push_constant) uniform DispatchParams {                                              \
        vec4 dispatch_options;                                                                  \
        vec4 emit_options;                                                                      \
    } dispatch_params

#define WATER2D_GRID_WIDTH params.grid_options.x
#define WATER2D_GRID_HEIGHT params.grid_options.y
#define WATER2D_ACTIVE_PARTICLE_COUNT params.grid_options.z
#define WATER2D_PARTICLE_CAPACITY params.lifecycle_options.x
#define WATER2D_HOSE_POOL_START params.lifecycle_options.y
#define WATER2D_HOSE_POOL_CAPACITY params.lifecycle_options.z
#define WATER2D_TRANSFER_MODE params.lifecycle_options.w
#define WATER2D_DT dispatch_params.dispatch_options.x
#define WATER2D_TIME dispatch_params.dispatch_options.y
#define WATER2D_PRESSURE_READ_B dispatch_params.dispatch_options.z
#define WATER2D_PARTICLE_SCAN_COUNT dispatch_params.dispatch_options.w
#define WATER2D_EMIT_CURSOR dispatch_params.emit_options.x
#define WATER2D_EMIT_COUNT dispatch_params.emit_options.y
#define WATER2D_VOLUME_STRENGTH params.hose_options2.y

#define WATER2D_RENDER_PARAMS                                                                  \
    layout(push_constant) uniform RenderParams {                                                \
        vec4 grid_debug;                                                                        \
        vec4 particle_options;                                                                  \
        vec4 surface_options;                                                                   \
    } params

#endif
