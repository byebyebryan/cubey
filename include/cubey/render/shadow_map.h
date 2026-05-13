#pragma once

#include <cubey/render/material.h>
#include <cubey/render/pass.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/target.h>
#include <cubey/render/texture.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/sampler.h>

#include <vulkan/vulkan.h>

#include <optional>
#include <span>
#include <string>
#include <utility>

namespace cubey::render {

struct ShadowDepthPassInfoConfig {
    std::string label = "shadow.depth";
    std::span<const VkPushConstantRange> push_constants{};
    VkCullModeFlags cull_mode = VK_CULL_MODE_NONE;
    VkCompareOp depth_compare_op = VK_COMPARE_OP_LESS;
};

struct ShadowMapPass3DConfig {
    VkExtent2D extent{1024, 1024};
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    GraphicsPipelineFileRecipe pipeline{};
    std::optional<cubey::vulkan::SamplerConfig> sampler{};
};

[[nodiscard]] cubey::vulkan::SamplerConfig shadow_map_sampler_config() noexcept;
[[nodiscard]] DepthTextureConfig
shadow_map_depth_texture_config(const ShadowMapPass3DConfig& config, VkFormat depth_format);
[[nodiscard]] MaterialPassInfo
shadow_depth_pass_info(const ShadowDepthPassInfoConfig& config = {});

class ShadowMapPass3D {
  public:
    ShadowMapPass3D(const cubey::vulkan::Device& device, const ShadowMapPass3DConfig& config);

    ShadowMapPass3D(const ShadowMapPass3D&) = delete;
    ShadowMapPass3D& operator=(const ShadowMapPass3D&) = delete;

    [[nodiscard]] const DepthTexture& depth_texture() const noexcept {
        return depth_texture_;
    }
    [[nodiscard]] DepthTargetView depth_target() const {
        return depth_target_view(depth_texture_);
    }
    [[nodiscard]] const MaterialPassInfo& material_pass() const noexcept {
        return material_pass_;
    }
    [[nodiscard]] const GraphicsPipelineResource& pipeline() const noexcept {
        return pipeline_;
    }

    template <typename RecordCallback>
    void record(const cubey::vulkan::CommandRecorder& recorder, VkClearValue clear,
                RecordCallback&& record_callback) const {
        record_depth_only_pass(recorder, depth_target(), clear,
                               std::forward<RecordCallback>(record_callback));
    }

  private:
    MaterialPassInfo material_pass_;
    DepthTexture depth_texture_;
    GraphicsPipelineResource pipeline_;
};

} // namespace cubey::render
