#include <cubey/render/pipeline_resource.h>

#include <cubey/vulkan/shader_bytecode.h>
#include <cubey/vulkan/shader_module.h>

#include <array>
#include <stdexcept>

namespace cubey::render {
namespace {

void validate_compute_pipeline_resource_config(const ComputePipelineResourceConfig& config) {
    if (config.shader_stage.stage != VK_SHADER_STAGE_COMPUTE_BIT) {
        throw std::runtime_error("compute pipeline resource requires a compute shader stage");
    }
}

} // namespace

ShaderProgram::ShaderProgram(const cubey::vulkan::Device& device,
                             std::span<const ShaderStageFile> stages) {
    if (stages.empty()) {
        throw std::runtime_error("shader program requires at least one stage");
    }

    modules_.reserve(stages.size());
    entry_points_.reserve(stages.size());
    stages_.reserve(stages.size());

    for (const ShaderStageFile& stage : stages) {
        if (stage.path.empty()) {
            throw std::runtime_error("shader program stage requires a SPIR-V path");
        }
        if (stage.entry_point.empty()) {
            throw std::runtime_error("shader program stage requires a shader entry point");
        }

        const std::vector<std::uint32_t> code = cubey::vulkan::read_spirv_file(stage.path);
        modules_.push_back(std::make_unique<cubey::vulkan::ShaderModule>(device, code));
        entry_points_.push_back(stage.entry_point);
        stages_.push_back(cubey::vulkan::shader_stage(stage.stage, modules_.back()->handle(),
                                                      entry_points_.back().c_str()));
    }
}

ShaderProgram::~ShaderProgram() = default;

cubey::vulkan::PipelineLayoutInfo
graphics_pipeline_layout_info(const GraphicsPipelineResourceConfig& config) {
    validate_material_pass_info(config.material_pass);
    return cubey::vulkan::PipelineLayoutInfo(cubey::vulkan::PipelineLayoutConfig{
        .set_layouts = config.descriptor_set_layouts,
        .push_constants = config.material_pass.push_constants,
    });
}

cubey::vulkan::DynamicGraphicsPipelineConfig
dynamic_graphics_pipeline_config(const GraphicsPipelineResourceConfig& config,
                                 VkPipelineLayout layout) {
    validate_material_pass_info(config.material_pass);
    if (layout == VK_NULL_HANDLE) {
        throw std::runtime_error("graphics pipeline resource requires a pipeline layout");
    }

    cubey::vulkan::DynamicGraphicsPipelineConfig pipeline_config;
    pipeline_config.layout = layout;
    pipeline_config.extent = config.extent;
    pipeline_config.color_format = config.color_format;
    pipeline_config.depth_format = config.depth_format;
    pipeline_config.shader_stages = config.shader_stages;
    pipeline_config.vertex_bindings = config.vertex_bindings;
    pipeline_config.vertex_attributes = config.vertex_attributes;
    apply_material_pass_state(config.material_pass, pipeline_config);
    return pipeline_config;
}

GraphicsPipelineResource::GraphicsPipelineResource(const cubey::vulkan::Device& device,
                                                   const GraphicsPipelineResourceConfig& config) {
    const cubey::vulkan::PipelineLayoutInfo layout_info = graphics_pipeline_layout_info(config);
    layout_.emplace(device, layout_info.create_info());

    const cubey::vulkan::DynamicGraphicsPipelineConfig pipeline_config =
        dynamic_graphics_pipeline_config(config, layout());
    const cubey::vulkan::DynamicGraphicsPipelineInfo pipeline_info(pipeline_config);
    pipeline_.emplace(device, pipeline_info.create_info());
}

VkPipelineLayout GraphicsPipelineResource::layout() const {
    if (!layout_.has_value()) {
        throw std::runtime_error("graphics pipeline resource layout is not initialized");
    }
    return layout_->handle();
}

VkPipeline GraphicsPipelineResource::pipeline() const {
    if (!pipeline_.has_value()) {
        throw std::runtime_error("graphics pipeline resource pipeline is not initialized");
    }
    return pipeline_->handle();
}

cubey::vulkan::PipelineLayoutInfo
compute_pipeline_layout_info(const ComputePipelineResourceConfig& config) {
    validate_compute_pipeline_resource_config(config);
    return cubey::vulkan::PipelineLayoutInfo(cubey::vulkan::PipelineLayoutConfig{
        .set_layouts = config.descriptor_set_layouts,
        .push_constants = config.push_constants,
    });
}

cubey::vulkan::ComputePipelineConfig
compute_pipeline_config(const ComputePipelineResourceConfig& config, VkPipelineLayout layout,
                        VkPipelineShaderStageCreateInfo shader_stage) {
    validate_compute_pipeline_resource_config(config);
    if (layout == VK_NULL_HANDLE) {
        throw std::runtime_error("compute pipeline resource requires a pipeline layout");
    }
    return cubey::vulkan::ComputePipelineConfig{
        .layout = layout,
        .shader_stage = shader_stage,
    };
}

ComputePipelineResource::ComputePipelineResource(const cubey::vulkan::Device& device,
                                                 const ComputePipelineResourceConfig& config) {
    validate_compute_pipeline_resource_config(config);

    const cubey::vulkan::PipelineLayoutInfo layout_info = compute_pipeline_layout_info(config);
    layout_.emplace(device, layout_info.create_info());

    const std::array<ShaderStageFile, 1> shader_stages{config.shader_stage};
    const ShaderProgram shader_program(device, shader_stages);
    const cubey::vulkan::ComputePipelineInfo pipeline_info(
        compute_pipeline_config(config, layout(), shader_program.stages().front()));
    pipeline_.emplace(device, pipeline_info.create_info());
}

VkPipelineLayout ComputePipelineResource::layout() const {
    if (!layout_.has_value()) {
        throw std::runtime_error("compute pipeline resource layout is not initialized");
    }
    return layout_->handle();
}

VkPipeline ComputePipelineResource::pipeline() const {
    if (!pipeline_.has_value()) {
        throw std::runtime_error("compute pipeline resource pipeline is not initialized");
    }
    return pipeline_->handle();
}

} // namespace cubey::render
