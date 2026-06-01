#include <cubey/render/render_graph.h>

namespace cubey::render {

RenderGraphTextureState render_graph_undefined_texture_state() noexcept {
    return {
        .layout = VK_IMAGE_LAYOUT_UNDEFINED,
        .access_mask = 0,
        .stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    };
}

RenderGraphTextureState render_graph_present_texture_state() noexcept {
    return {
        .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .access_mask = 0,
        .stage_mask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    };
}

RenderGraphTextureState render_graph_color_attachment_texture_state() noexcept {
    return {
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
}

RenderGraphTextureState render_graph_sampled_depth_texture_state() noexcept {
    return {
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        .access_mask = VK_ACCESS_SHADER_READ_BIT,
        .stage_mask = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
    };
}

} // namespace cubey::render
