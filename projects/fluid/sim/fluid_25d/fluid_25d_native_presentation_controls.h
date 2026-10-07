#pragma once

#include <cubey/engine/project_gpu_services.h>
#include <cubey/vulkan/device.h>

namespace cubey::projects::fluid::fluid_25d {

void validate_fluid_25d_native_cue_gpu_controls(cubey::vulkan::Device& device,
                                                cubey::ProjectGpuServices& gpu);

} // namespace cubey::projects::fluid::fluid_25d
