#include "particle_cubes_app_internal.h"

#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/vk_check.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace cubey::examples::particle_cubes {

void ParticleCubesApp::record_particle_compute(const cubey::vulkan::CommandRecorder& recorder,
                                               const FrameTiming& timing) const {
    const float time = static_cast<float>(timing.elapsed_seconds);
    const float delta_seconds = std::min(static_cast<float>(timing.delta_seconds), 1.0F / 30.0F);
    const ComputePushConstants push_constants{
        .attractor_dt =
            {
                std::cos(time * 0.51F) * 1.15F,
                std::sin(time * 0.73F) * 0.55F,
                std::sin(time * 0.37F) * 1.10F,
                delta_seconds,
            },
        .bounds_damping_time =
            {
                2.75F,
                0.985F,
                time,
                0.0F,
            },
    };

    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline_resource().pipeline());
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE,
                                 compute_pipeline_resource().layout(), 0, descriptors().set());
    recorder.push_constants(compute_pipeline_resource().layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                            push_constants);
    recorder.dispatch((kParticleCubeCount + kComputeGroupSize - 1U) / kComputeGroupSize, 1, 1);

    const std::array<VkMemoryBarrier, 1> particle_barriers{{
        [&] {
            auto barrier =
                cubey::vulkan::vk_struct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            return barrier;
        }(),
    }};
    recorder.pipeline_barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                              VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, particle_barriers);
}

} // namespace cubey::examples::particle_cubes
