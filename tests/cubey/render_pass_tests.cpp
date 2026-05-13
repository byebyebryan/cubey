#include <cubey/render/pass.h>

#include <vulkan/vulkan.h>

#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_throws(auto&& action, const char* message) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

} // namespace

void test_render_pass_helpers_describe_clear_values_and_fullscreen_triangle() {
    const VkClearValue color_clear = cubey::render::color_clear_value(0.2F, 0.4F, 0.6F, 0.8F);
    require(color_clear.color.float32[0] == 0.2F, "color clear helper should preserve red");
    require(color_clear.color.float32[1] == 0.4F, "color clear helper should preserve green");
    require(color_clear.color.float32[2] == 0.6F, "color clear helper should preserve blue");
    require(color_clear.color.float32[3] == 0.8F, "color clear helper should preserve alpha");

    const VkClearValue depth_clear = cubey::render::depth_clear_value(0.75F, 3);
    require(depth_clear.depthStencil.depth == 0.75F, "depth clear helper should preserve depth");
    require(depth_clear.depthStencil.stencil == 3, "depth clear helper should preserve stencil");
    require(cubey::render::fullscreen_triangle_vertex_count() == 3,
            "fullscreen triangle should use one vertexless triangle");

    cubey::render::ComputePipelineDispatchInfo dispatch_info{
        .group_count_x = 8,
        .group_count_y = 4,
        .group_count_z = 2,
    };
    require_throws(
        [dispatch_info] { cubey::render::validate_compute_dispatch_info(dispatch_info); },
        "compute dispatch helper should reject a missing pipeline");
    dispatch_info.pipeline = reinterpret_cast<const cubey::render::ComputePipelineResource*>(0x10);
    cubey::render::validate_compute_dispatch_info(dispatch_info);
    dispatch_info.group_count_y = 0;
    require_throws(
        [dispatch_info] { cubey::render::validate_compute_dispatch_info(dispatch_info); },
        "compute dispatch helper should reject zero group counts");
}
