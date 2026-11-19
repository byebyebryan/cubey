#pragma once

#include <cubey/host/vulkan_requirements.h>
#include <cubey/render/temporal_upscaler.h>

#include <memory>
#include <string_view>

namespace cubey::vulkan {
class GpuOwnerContext;
}

namespace cubey::host {

class DlssUpscaler final : public render::TemporalUpscaler {
  public:
    [[nodiscard]] static VulkanRequirements vulkan_requirements();

    DlssUpscaler(VkInstance instance, const vulkan::Device& device, std::string_view preset = "k");
    ~DlssUpscaler() override;

    // The target owner retires all previous frame submissions before replacing or destroying it.
    [[nodiscard]] VkExtent2D create_target(vulkan::GpuOwnerContext& context,
                                           VkExtent2D output_extent);
    void destroy_target() noexcept;

    [[nodiscard]] render::RenderGraphTextureHandle
    declare(render::RenderGraphBuilder& graph,
            const render::TemporalUpscaleInputs& inputs) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cubey::host
