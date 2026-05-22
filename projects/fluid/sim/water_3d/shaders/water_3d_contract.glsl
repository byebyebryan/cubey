#ifndef CUBEY_WATER_3D_CONTRACT_GLSL
#define CUBEY_WATER_3D_CONTRACT_GLSL

#define WATER3D_BINDING_PARTICLE_POSITIONS 0
#define WATER3D_BINDING_PARTICLE_VELOCITIES 1
#define WATER3D_BINDING_PARTICLE_AFFINE 2
#define WATER3D_BINDING_U_FIELD 3
#define WATER3D_BINDING_U_PREVIOUS 4
#define WATER3D_BINDING_V_FIELD 5
#define WATER3D_BINDING_V_PREVIOUS 6
#define WATER3D_BINDING_W_FIELD 7
#define WATER3D_BINDING_W_PREVIOUS 8
#define WATER3D_BINDING_U_WEIGHT 9
#define WATER3D_BINDING_V_WEIGHT 10
#define WATER3D_BINDING_W_WEIGHT 11
#define WATER3D_BINDING_PRESSURE_A 12
#define WATER3D_BINDING_PRESSURE_B 13
#define WATER3D_BINDING_DIVERGENCE 14
#define WATER3D_BINDING_SOLID 15
#define WATER3D_BINDING_CELL_COUNTS 16
#define WATER3D_BINDING_CELL_PARTICLE_INDICES 17
#define WATER3D_BINDING_SIM_PARAMS 18

#define WATER3D_EMPTY_PARTICLE 0xffffffffu

#define WATER3D_SIMULATION_PARAMS                                                              \
    layout(set = 0, binding = WATER3D_BINDING_SIM_PARAMS, std140) uniform SimulationParams {    \
        vec4 grid_options;                                                                      \
        vec4 particle_options;                                                                  \
        vec4 fill_options;                                                                      \
        vec4 solve_options;                                                                     \
        vec4 lifecycle_options;                                                                 \
        vec4 render_options;                                                                    \
        vec4 reserved0;                                                                         \
        vec4 reserved1;                                                                         \
    } params;                                                                                   \
    layout(push_constant) uniform DispatchParams {                                              \
        vec4 dispatch_options;                                                                  \
    } dispatch_params

#define WATER3D_GRID_WIDTH params.grid_options.x
#define WATER3D_GRID_HEIGHT params.grid_options.y
#define WATER3D_GRID_DEPTH params.grid_options.z
#define WATER3D_ACTIVE_PARTICLE_COUNT params.grid_options.w
#define WATER3D_PARTICLE_CAPACITY params.particle_options.x
#define WATER3D_MAX_PARTICLES_PER_CELL params.particle_options.y
#define WATER3D_PARTICLES_PER_CELL params.particle_options.z
#define WATER3D_FLIP_RATIO params.particle_options.w
#define WATER3D_DT dispatch_params.dispatch_options.x
#define WATER3D_TIME dispatch_params.dispatch_options.y
#define WATER3D_PRESSURE_READ_B dispatch_params.dispatch_options.z
#define WATER3D_PARTICLE_SCAN_COUNT dispatch_params.dispatch_options.w
#define WATER3D_VELOCITY_LIMIT params.solve_options.x
#define WATER3D_PARTICLE_DAMPING params.solve_options.y
#define WATER3D_VOLUME_STRENGTH params.solve_options.z
#define WATER3D_TRANSFER_MODE params.solve_options.w
#define WATER3D_BOUNDARY_RESTITUTION params.lifecycle_options.z

#define WATER3D_RENDER_PARAMS                                                                   \
    layout(push_constant) uniform RenderParams {                                                \
        mat4 view_projection;                                                                   \
        vec4 camera_right_radius;                                                               \
        vec4 camera_up_debug;                                                                   \
        vec4 grid_slice;                                                                        \
        vec4 color_options;                                                                     \
    } params

#endif
