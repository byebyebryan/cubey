#pragma once

#include <cubey/engine/project_gpu_services.h>
#include <cubey/vulkan/device.h>

namespace cubey::projects::fluid::fluid_25d {

// Exercises the production GLSL surface samplers on isolated synthetic fields.
// The controls only read their scratch depth/query buffers and do not dispatch
// a hydraulic solver or touch application state. B-spline results are compute
// controls, not a rasterized geometry or visibility acceptance test.
void validate_fluid_25d_bank_gpu_controls(cubey::vulkan::Device& device,
                                          cubey::ProjectGpuServices& gpu);

} // namespace cubey::projects::fluid::fluid_25d
