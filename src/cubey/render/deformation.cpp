#include <cubey/render/deformation.h>

#include <array>
#include <stdexcept>

namespace cubey::render {
namespace {

[[nodiscard]] std::uint32_t binding(GpuDeformationBinding value) {
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] vulkan::DescriptorSetBindingConfig storage_binding(GpuDeformationBinding value) {
    return {
        .binding = binding(value),
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
    };
}

} // namespace

vulkan::DescriptorSetInfo gpu_deformation_descriptor_set_info() {
    return gpu_deformation_descriptor_set_info(1);
}

vulkan::DescriptorSetInfo gpu_deformation_descriptor_set_info(std::uint32_t max_sets) {
    const std::array<vulkan::DescriptorSetBindingConfig, 6> bindings{
        storage_binding(GpuDeformationBinding::BaseVertices),
        storage_binding(GpuDeformationBinding::MorphTargets),
        storage_binding(GpuDeformationBinding::MorphWeights),
        storage_binding(GpuDeformationBinding::SkinInfluences),
        storage_binding(GpuDeformationBinding::JointPalette),
        storage_binding(GpuDeformationBinding::OutputVertices),
    };
    return vulkan::DescriptorSetInfo(bindings, max_sets);
}

ComputePipelineResourceConfig
gpu_deformation_pipeline_config(std::filesystem::path compute_shader) {
    static const std::array<VkDescriptorSetLayout, 0> kNoLayouts{};
    return gpu_deformation_pipeline_config(std::move(compute_shader), kNoLayouts);
}

ComputePipelineResourceConfig gpu_deformation_pipeline_config(
    std::filesystem::path compute_shader,
    std::span<const VkDescriptorSetLayout> descriptor_set_layouts) {
    static const std::array<VkPushConstantRange, 1> kPushConstants{
        VkPushConstantRange{
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset = 0,
            .size = sizeof(GpuDeformationPushConstants),
        },
    };
    return {
        .shader_stage = compute_shader_file(std::move(compute_shader)),
        .descriptor_set_layouts = descriptor_set_layouts,
        .push_constants = kPushConstants,
    };
}

GpuDeformationDispatch gpu_deformation_dispatch_groups(std::uint32_t vertex_count,
                                                       std::uint32_t group_size) {
    if (vertex_count == 0) {
        throw std::runtime_error("deformation dispatch requires vertices");
    }
    if (group_size == 0) {
        throw std::runtime_error("deformation dispatch group size must be positive");
    }
    return {
        .x = (vertex_count + group_size - 1U) / group_size,
        .y = 1,
        .z = 1,
    };
}

void record_gpu_deformation_dispatch(const vulkan::CommandRecorder& recorder,
                                     const ComputePipelineResource& pipeline,
                                     VkDescriptorSet descriptor_set,
                                     const GpuDeformationPushConstants& push_constants,
                                     std::uint32_t group_size) {
    if (descriptor_set == VK_NULL_HANDLE) {
        throw std::runtime_error("deformation dispatch requires a descriptor set");
    }
    const GpuDeformationDispatch dispatch =
        gpu_deformation_dispatch_groups(push_constants.vertex_count, group_size);
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0,
                                 descriptor_set);
    recorder.push_constants(pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, push_constants);
    recorder.dispatch(dispatch.x, dispatch.y, dispatch.z);
}

void record_gpu_deformation_dispatch(const vulkan::CommandRecorder& recorder,
                                     const GpuDeformationCommand& command,
                                     std::uint32_t group_size) {
    if (!command.mesh) {
        throw std::runtime_error("deformation command requires a mesh handle");
    }
    if (command.output_mesh == nullptr) {
        throw std::runtime_error("deformation command requires an output mesh");
    }
    if (command.pipeline == nullptr) {
        throw std::runtime_error("deformation command requires a pipeline");
    }
    record_gpu_deformation_dispatch(recorder, *command.pipeline, command.descriptor_set,
                                    command.push_constants, group_size);
}

void record_gpu_deformation_commands(const vulkan::CommandRecorder& recorder,
                                     std::span<const GpuDeformationCommand> commands,
                                     std::uint32_t group_size) {
    for (const GpuDeformationCommand& command : commands) {
        record_gpu_deformation_dispatch(recorder, command, group_size);
    }
}

} // namespace cubey::render
