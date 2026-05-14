#pragma once

#include <cubey/render/pipeline_resource.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/descriptors.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>

namespace cubey::render {

enum class GpuDeformationBinding : std::uint32_t {
    BaseVertices = 0,
    MorphTargets = 1,
    MorphWeights = 2,
    SkinInfluences = 3,
    JointPalette = 4,
    OutputVertices = 5,
};

enum class GpuDeformationFlags : std::uint32_t {
    Morph = 1U << 0U,
    Skin = 1U << 1U,
};

struct GpuDeformationPushConstants {
    std::uint32_t vertex_count = 0;
    std::uint32_t morph_target_count = 0;
    std::uint32_t joint_count = 0;
    std::uint32_t flags = 0;
};

static_assert(sizeof(GpuDeformationPushConstants) == sizeof(std::uint32_t) * 4U);

struct GpuDeformationDispatch {
    std::uint32_t x = 0;
    std::uint32_t y = 1;
    std::uint32_t z = 1;
};

[[nodiscard]] vulkan::DescriptorSetInfo gpu_deformation_descriptor_set_info();
[[nodiscard]] vulkan::DescriptorSetInfo
gpu_deformation_descriptor_set_info(std::uint32_t max_sets);
[[nodiscard]] ComputePipelineResourceConfig
gpu_deformation_pipeline_config(std::filesystem::path compute_shader);
[[nodiscard]] GpuDeformationDispatch
gpu_deformation_dispatch_groups(std::uint32_t vertex_count, std::uint32_t group_size = 64);

void record_gpu_deformation_dispatch(const vulkan::CommandRecorder& recorder,
                                     const ComputePipelineResource& pipeline,
                                     VkDescriptorSet descriptor_set,
                                     const GpuDeformationPushConstants& push_constants,
                                     std::uint32_t group_size = 64);

} // namespace cubey::render
