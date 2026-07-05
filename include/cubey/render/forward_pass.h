#pragma once

#include <cubey/render/pass.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/target.h>
#include <cubey/vulkan/image.h>

#include <vulkan/vulkan.h>

#include <utility>

namespace cubey::render {

struct ForwardScenePass3DConfig {
    GraphicsPipelineFileRecipe pipeline{};
    RenderClearValues clear{
        .color = color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
        .depth = depth_clear_value(),
    };
    bool sampled_depth = false;
};

class ForwardScenePass3D {
  public:
    ForwardScenePass3D(const cubey::vulkan::Device& device, GraphicsPipelineTargetInfo target,
                       const ForwardScenePass3DConfig& config);

    ForwardScenePass3D(const ForwardScenePass3D&) = delete;
    ForwardScenePass3D& operator=(const ForwardScenePass3D&) = delete;
    ForwardScenePass3D(ForwardScenePass3D&& other) = delete;
    ForwardScenePass3D& operator=(ForwardScenePass3D&& other) = delete;

    [[nodiscard]] const GraphicsPipelineResource& pipeline() const noexcept {
        return pipeline_;
    }
    [[nodiscard]] const cubey::vulkan::DepthAttachment& depth_attachment() const noexcept {
        return depth_attachment_;
    }
    [[nodiscard]] DepthTargetView depth_target() const;
    [[nodiscard]] RenderTargetView target(ColorTargetView color_target) const;
    [[nodiscard]] const RenderClearValues& clear_values() const noexcept {
        return clear_;
    }

    template <typename RecordCallback>
    void record_to_prepared_target(const cubey::vulkan::CommandRecorder& recorder,
                                   ColorTargetView color_target,
                                   RecordCallback&& record_callback) const {
        record_render_target_pass(recorder, target(color_target), clear_,
                                  std::forward<RecordCallback>(record_callback));
    }

    template <typename RecordCallback>
    void record_to_present_target(const cubey::vulkan::CommandRecorder& recorder,
                                  ColorTargetView color_target,
                                  RecordCallback&& record_callback) const {
        record_present_render_target_pass(recorder, target(color_target), clear_,
                                          std::forward<RecordCallback>(record_callback));
    }

  private:
    cubey::vulkan::DepthAttachment depth_attachment_;
    GraphicsPipelineResource pipeline_;
    RenderClearValues clear_{};
};

} // namespace cubey::render
