#pragma once

#include <cubey/render/material.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/pipeline.h>

#include <vulkan/vulkan.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace cubey::vulkan {
class ShaderModule;
}

namespace cubey::render {

struct ShaderStageFile {
    VkShaderStageFlagBits stage = VK_SHADER_STAGE_VERTEX_BIT;
    std::filesystem::path path{};
    std::string entry_point = "main";
};

class ShaderProgram {
  public:
    ShaderProgram(const cubey::vulkan::Device& device, std::span<const ShaderStageFile> stages);
    ~ShaderProgram();

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    [[nodiscard]] std::span<const VkPipelineShaderStageCreateInfo> stages() const noexcept {
        return stages_;
    }

  private:
    std::vector<std::unique_ptr<cubey::vulkan::ShaderModule>> modules_{};
    std::vector<std::string> entry_points_{};
    std::vector<VkPipelineShaderStageCreateInfo> stages_{};
};

struct GraphicsPipelineResourceConfig {
    VkExtent2D extent{};
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    std::span<const VkPipelineShaderStageCreateInfo> shader_stages{};
    std::span<const VkVertexInputBindingDescription> vertex_bindings{};
    std::span<const VkVertexInputAttributeDescription> vertex_attributes{};
    std::span<const VkDescriptorSetLayout> descriptor_set_layouts{};
    MaterialPassInfo material_pass{};
};

struct GraphicsPipelineFileResourceConfig {
    VkExtent2D extent{};
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    std::span<const ShaderStageFile> shader_stage_files{};
    std::span<const VkVertexInputBindingDescription> vertex_bindings{};
    std::span<const VkVertexInputAttributeDescription> vertex_attributes{};
    std::span<const VkDescriptorSetLayout> descriptor_set_layouts{};
    MaterialPassInfo material_pass{};
};

[[nodiscard]] cubey::vulkan::PipelineLayoutInfo
graphics_pipeline_layout_info(const GraphicsPipelineResourceConfig& config);
[[nodiscard]] cubey::vulkan::DynamicGraphicsPipelineConfig
dynamic_graphics_pipeline_config(const GraphicsPipelineResourceConfig& config,
                                 VkPipelineLayout layout);

class GraphicsPipelineResource {
  public:
    GraphicsPipelineResource(const cubey::vulkan::Device& device,
                             const GraphicsPipelineResourceConfig& config);
    GraphicsPipelineResource(const cubey::vulkan::Device& device,
                             const GraphicsPipelineFileResourceConfig& config);

    GraphicsPipelineResource(const GraphicsPipelineResource&) = delete;
    GraphicsPipelineResource& operator=(const GraphicsPipelineResource&) = delete;

    [[nodiscard]] VkPipelineLayout layout() const;
    [[nodiscard]] VkPipeline pipeline() const;

  private:
    void create(const cubey::vulkan::Device& device, const GraphicsPipelineResourceConfig& config);

    std::optional<cubey::vulkan::PipelineLayout> layout_{};
    std::optional<cubey::vulkan::GraphicsPipeline> pipeline_{};
};

struct ComputePipelineResourceConfig {
    ShaderStageFile shader_stage{
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
    };
    std::span<const VkDescriptorSetLayout> descriptor_set_layouts{};
    std::span<const VkPushConstantRange> push_constants{};
};

[[nodiscard]] cubey::vulkan::PipelineLayoutInfo
compute_pipeline_layout_info(const ComputePipelineResourceConfig& config);
[[nodiscard]] cubey::vulkan::ComputePipelineConfig
compute_pipeline_config(const ComputePipelineResourceConfig& config, VkPipelineLayout layout,
                        VkPipelineShaderStageCreateInfo shader_stage);

class ComputePipelineResource {
  public:
    ComputePipelineResource(const cubey::vulkan::Device& device,
                            const ComputePipelineResourceConfig& config);

    ComputePipelineResource(const ComputePipelineResource&) = delete;
    ComputePipelineResource& operator=(const ComputePipelineResource&) = delete;

    [[nodiscard]] VkPipelineLayout layout() const;
    [[nodiscard]] VkPipeline pipeline() const;

  private:
    std::optional<cubey::vulkan::PipelineLayout> layout_{};
    std::optional<cubey::vulkan::ComputePipeline> pipeline_{};
};

} // namespace cubey::render
