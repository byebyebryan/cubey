#include <cubey/render/forward_pass.h>

#include <stdexcept>

namespace cubey::render {

namespace {

[[nodiscard]] GraphicsPipelineTargetInfo
pipeline_target_info(GraphicsPipelineTargetInfo target,
                     const cubey::vulkan::DepthAttachment& depth_attachment) {
    if (target.extent.width == 0 || target.extent.height == 0) {
        throw std::runtime_error("forward scene pass requires a nonzero target extent");
    }
    if (target.color_format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("forward scene pass requires a color format");
    }
    target.depth_format = depth_attachment.format();
    return target;
}

} // namespace

ForwardScenePass3D::ForwardScenePass3D(const cubey::vulkan::Device& device,
                                       GraphicsPipelineTargetInfo target,
                                       const ForwardScenePass3DConfig& config)
    : depth_attachment_(device, target.extent),
      pipeline_(device, graphics_pipeline_file_resource_config(
                            pipeline_target_info(target, depth_attachment_), config.pipeline)),
      clear_(config.clear) {}

DepthTargetView ForwardScenePass3D::depth_target() const {
    return depth_target_view(depth_attachment_);
}

RenderTargetView ForwardScenePass3D::target(ColorTargetView color_target) const {
    if (color_target.extent.width != depth_attachment_.extent().width ||
        color_target.extent.height != depth_attachment_.extent().height) {
        throw std::runtime_error("forward scene pass color target extent mismatch");
    }
    return render_target_view(color_target, depth_target());
}

} // namespace cubey::render
