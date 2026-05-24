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
#define WATER3D_BINDING_SIM_PARAMS 18
#define WATER3D_BINDING_U_SCRATCH 19
#define WATER3D_BINDING_U_PREVIOUS_SCRATCH 20
#define WATER3D_BINDING_V_SCRATCH 21
#define WATER3D_BINDING_V_PREVIOUS_SCRATCH 22
#define WATER3D_BINDING_W_SCRATCH 23
#define WATER3D_BINDING_W_PREVIOUS_SCRATCH 24
#define WATER3D_BINDING_U_WEIGHT_SCRATCH 25
#define WATER3D_BINDING_V_WEIGHT_SCRATCH 26
#define WATER3D_BINDING_W_WEIGHT_SCRATCH 27
#define WATER3D_BINDING_WHITEWATER_POSITIONS 28
#define WATER3D_BINDING_WHITEWATER_VELOCITIES 29
#define WATER3D_BINDING_WHITEWATER_STATE 30
#define WATER3D_BINDING_WHITEWATER_COUNTERS 31
#define WATER3D_BINDING_WHITEWATER_ACTIVE_INDICES 32
#define WATER3D_BINDING_WHITEWATER_DRAW_ARGS 33
#define WATER3D_BINDING_ACTIVE_WORK_COUNTS 34
#define WATER3D_BINDING_ACTIVE_FACE_FLAGS 35
#define WATER3D_BINDING_ACTIVE_FACE_INDICES 36
#define WATER3D_BINDING_ACTIVE_FACE_DISPATCH_ARGS 37
#define WATER3D_BINDING_DIAGNOSTICS 38
#define WATER3D_BINDING_SORTED_PARTICLE_INDICES 39
#define WATER3D_BINDING_CELL_OFFSETS 42
#define WATER3D_BINDING_CELL_WRITE_COUNTS 43
#define WATER3D_BINDING_SORT_SCAN_LEVEL0_SUMS 44
#define WATER3D_BINDING_SORT_SCAN_LEVEL1_OFFSETS 45
#define WATER3D_BINDING_SORT_SCAN_LEVEL1_SUMS 46
#define WATER3D_BINDING_SORT_SCAN_LEVEL2_OFFSETS 47
#define WATER3D_BINDING_SORT_SCAN_LEVEL2_SUMS 48
#define WATER3D_BINDING_ACTIVE_TILE_FLAGS 49
#define WATER3D_BINDING_ACTIVE_TILE_INDICES 50
#define WATER3D_BINDING_ACTIVE_TILE_DISPATCH_ARGS 51

#define WATER3D_EMPTY_PARTICLE 0xffffffffu

const float WATER3D_PARTICLE_STATE_INACTIVE = 0.0;
const float WATER3D_PARTICLE_STATE_LIQUID = 1.0;
const float WATER3D_PARTICLE_STATE_RAIN = 2.0;
const float WATER3D_RAIN_RENDER_RADIUS_SCALE = 0.26;

#define WATER3D_SIMULATION_PARAMS                                                              \
    layout(set = 0, binding = WATER3D_BINDING_SIM_PARAMS, std140) uniform SimulationParams {    \
        vec4 grid_options;                                                                      \
        vec4 particle_options;                                                                  \
        vec4 fill_options;                                                                      \
        vec4 fill_placement_options;                                                            \
        vec4 solve_options;                                                                     \
        vec4 lifecycle_options;                                                                 \
        vec4 render_options;                                                                    \
        vec4 whitewater_options;                                                                \
        vec4 whitewater_lifecycle;                                                              \
        vec4 emitter_lifecycle;                                                                 \
        vec4 hose_options0;                                                                     \
        vec4 hose_options1;                                                                     \
        vec4 hose_options2;                                                                     \
        vec4 drain_options;                                                                     \
        vec4 drain_extents;                                                                     \
        vec4 drain_flow;                                                                        \
        vec4 wave_options0;                                                                     \
        vec4 wave_options1;                                                                     \
        vec4 wave_options2;                                                                     \
        vec4 rain_options0;                                                                     \
        vec4 rain_options1;                                                                     \
        vec4 rain_options2;                                                                     \
    } params;                                                                                   \
    layout(push_constant) uniform DispatchParams {                                              \
        vec4 dispatch_options;                                                                  \
        vec4 emit_options;                                                                      \
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
#define WATER3D_EXTRAPOLATE_READ_SCRATCH dispatch_params.dispatch_options.z
#define WATER3D_BUILD_ACTIVE_FACES dispatch_params.dispatch_options.z
#define WATER3D_PARTICLE_SCAN_COUNT dispatch_params.dispatch_options.w
#define WATER3D_DISPATCH_COUNT dispatch_params.dispatch_options.w
#define WATER3D_EMIT_CURSOR dispatch_params.emit_options.x
#define WATER3D_EMIT_COUNT dispatch_params.emit_options.y
#define WATER3D_EMITTER_KIND dispatch_params.emit_options.z
#define WATER3D_EMITTER_SEED dispatch_params.emit_options.w
#define WATER3D_VELOCITY_LIMIT params.solve_options.x
#define WATER3D_PARTICLE_DAMPING params.solve_options.y
#define WATER3D_VOLUME_STRENGTH params.solve_options.z
#define WATER3D_TRANSFER_MODE params.solve_options.w
#define WATER3D_BOUNDARY_RESTITUTION params.lifecycle_options.z
#define WATER3D_EMITTER_POOL_START params.emitter_lifecycle.x
#define WATER3D_EMITTER_POOL_CAPACITY params.emitter_lifecycle.y
#define WATER3D_WHITEWATER_CAPACITY params.whitewater_options.x
#define WATER3D_WHITEWATER_MAX_EMIT_PER_FRAME params.whitewater_options.y
#define WATER3D_WHITEWATER_INTENSITY params.whitewater_options.z
#define WATER3D_WHITEWATER_RADIUS params.whitewater_options.w
#define WATER3D_WHITEWATER_SPEED_THRESHOLD params.whitewater_lifecycle.x
#define WATER3D_WHITEWATER_LIFETIME params.whitewater_lifecycle.y
#define WATER3D_WHITEWATER_DRAG params.whitewater_lifecycle.z
#define WATER3D_WHITEWATER_GRAVITY_SCALE params.whitewater_lifecycle.w

#define WATER3D_RENDER_PARAMS                                                                   \
    layout(push_constant) uniform RenderParams {                                                \
        mat4 view_projection;                                                                   \
        vec4 camera_right_radius;                                                               \
        vec4 camera_up_debug;                                                                   \
        vec4 grid_slice;                                                                        \
        vec4 color_options;                                                                     \
        vec4 domain_options;                                                                    \
    } params

#endif
