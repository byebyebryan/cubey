#include "gltf_viewer_app_internal.h"

#include <cubey/render/primitive_mesh.h>
#include <cubey/scene/transform_3d.h>

#include <vulkan/vulkan.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <filesystem>
#include <span>
#include <utility>
#include <vector>

#ifndef CUBEY_GLTF_VIEWER_SHADER_DIR
#error "CUBEY_GLTF_VIEWER_SHADER_DIR must be defined by the gltf_viewer CMake target"
#endif

namespace cubey::projects::gltf_viewer {

using cubey::host::FrameStatsSample;

const cubey::math::Vec3 kLightDirection = glm::normalize(cubey::math::Vec3{0.45F, 0.82F, 0.35F});

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_GLTF_VIEWER_SHADER_DIR) / filename;
}

std::filesystem::path bundled_sample_asset_path() {
#ifdef CUBEY_GLTF_SAMPLE_ASSETS_DIR
    return std::filesystem::path(CUBEY_GLTF_SAMPLE_ASSETS_DIR) / "Models" / "DamagedHelmet" /
           "glTF-Binary" / "DamagedHelmet.glb";
#else
    return {};
#endif
}

std::filesystem::path bundled_sample_environment_path() {
#ifdef CUBEY_HDR_SAMPLE_ASSETS_DIR
    return std::filesystem::path(CUBEY_HDR_SAMPLE_ASSETS_DIR) / "lightroom_14b.hdr";
#else
    return {};
#endif
}

cubey::ForwardPbrRenderer3DConfig forward_pbr_renderer_3d_config() {
    return cubey::forward_pbr_renderer_3d_config_from_shader_directory(
        CUBEY_GLTF_VIEWER_SHADER_DIR, {.shadow_extent = kShadowMapSize});
}

cubey::Transform3D look_at_transform(cubey::math::Vec3 eye, cubey::math::Vec3 target) {
    const cubey::math::Vec3 forward = glm::normalize(target - eye);
    cubey::math::Vec3 up{0.0F, 1.0F, 0.0F};
    if (std::abs(glm::dot(forward, up)) > 0.95F) {
        up = {0.0F, 0.0F, 1.0F};
    }
    return {
        .translation = eye,
        .rotation = glm::quatLookAtRH(forward, up),
    };
}

std::vector<cubey::render::PbrVertex> fallback_cube_vertices() {
    const cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormalUv> cube =
        cubey::render::make_cube_position_color_normal_uv_mesh();
    std::vector<cubey::render::PbrVertex> vertices;
    vertices.reserve(cube.vertices.size());
    for (const cubey::render::VertexPositionColorNormalUv& vertex : cube.vertices) {
        vertices.push_back({
            .position = {vertex.position[0], vertex.position[1], vertex.position[2]},
            .normal = {vertex.normal[0], vertex.normal[1], vertex.normal[2]},
            .tangent = {1.0F, 0.0F, 0.0F, 1.0F},
            .uv0 = {vertex.uv[0], vertex.uv[1]},
        });
    }
    return vertices;
}

std::vector<std::uint32_t> fallback_cube_indices() {
    const cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormalUv> cube =
        cubey::render::make_cube_position_color_normal_uv_mesh();
    std::vector<std::uint32_t> indices;
    indices.reserve(cube.indices.size());
    for (const std::uint16_t index : cube.indices) {
        indices.push_back(index);
    }
    return indices;
}

GltfViewerApp::GltfViewerApp(RunConfig config)
    : config_(std::move(config)),
      debug_view_(render::pbr_debug_view_from_name(config_.debug_view)) {}

int GltfViewerApp::run() {
    if (config_.headless) {
        return run_headless();
    }
    return run_windowed();
}

int GltfViewerApp::run_windowed() {
    cubey::host::WindowedAppCallbacks callbacks;
    callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
        create_global_resources_if_needed(context.device(), context.gpu(),
                                          context.frame_slot_count());
        create_frame_resources(context.device(), context.swapchain().extent(),
                               context.swapchain().format());
    };
    callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
        (void)context;
        destroy_swapchain_resources();
    };
    callbacks.update = [this](cubey::host::WindowedAppContext& context, const FrameTiming& timing) {
        update_animation(static_cast<float>(timing.delta_seconds));
        if (context.input().key_pressed(cubey::input::Key::D)) {
            debug_view_ = render::next_pbr_debug_view(debug_view_);
        }
        orbit_controller_.update_from_input(context.input(), timing.delta_seconds);
        update_camera_transform();
    };
    callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                    const cubey::host::WindowedRenderFrame& frame) {
        record_viewer_frame(context, frame);
    };
    callbacks.frame_stats_sample =
        [this](cubey::host::WindowedAppContext& context,
               const FrameTiming& timing) -> std::optional<FrameStatsSample> {
        const VkExtent2D extent = context.swapchain().extent();
        return FrameStatsSample{
            .delta_seconds = timing.delta_seconds,
            .width = extent.width,
            .height = extent.height,
            .triangles = triangle_count_,
        };
    };
    callbacks.shutdown = [this](cubey::host::WindowedAppContext& context) {
        (void)context;
        destroy_all_resources();
    };

    return cubey::host::run_windowed_app(
        {
            .run_config = config_,
            .app_name = "gltf_viewer",
            .ready_status = "rendering glTF/PBR viewer",
            .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
            .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .require_dynamic_rendering = true,
            .close_on_escape = true,
        },
        std::move(callbacks));
}

int GltfViewerApp::run_headless() {
    cubey::host::HeadlessPngHostConfig host_config;
    host_config.run_config = config_;
    host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
    host_config.output_format = VK_FORMAT_R8G8B8A8_UNORM;
    host_config.require_dynamic_rendering = true;

    cubey::host::HeadlessPngHostCallbacks callbacks;
    callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
        create_global_resources_if_needed(context.device(), context.gpu(), 1);
        create_frame_resources(context.device(), context.render_target().extent,
                               context.render_target().format);
    };
    callbacks.record_capture = [this](cubey::host::HeadlessPngContext& context,
                                      VkCommandBuffer command_buffer,
                                      const cubey::host::HeadlessRenderTarget& target) {
        record_viewer_capture(context, command_buffer, target);
    };
    callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) { destroy_all_resources(); };

    cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
    return host.run();
}

int run_gltf_viewer(const RunConfig& config) {
    GltfViewerApp app(config);
    return app.run();
}

} // namespace cubey::projects::gltf_viewer
