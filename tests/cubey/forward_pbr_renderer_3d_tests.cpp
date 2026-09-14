#include "../../src/cubey/engine/forward_pbr_renderer_3d_internal.h"
#include "source_file_test_helpers.h"

#include <cubey/engine/forward_pbr_renderer_3d.h>
#include <cubey/render/hdr_color_pyramid.h>
#include <cubey/render/texture.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(float actual, float expected, const char* message) {
    if (std::abs(actual - expected) > 0.0001F) {
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

using cubey::tests::read_source_file;
using cubey::tests::require_contains;
using cubey::tests::require_not_contains;

cubey::ForwardPbrRenderer3DConfig valid_config() {
    return {
        .pbr_vertex_shader = "pbr.vert.spv",
        .pbr_fragment_shader = "pbr.frag.spv",
        .skybox_vertex_shader = "skybox.vert.spv",
        .skybox_fragment_shader = "skybox.frag.spv",
        .post_vertex_shader = "post.vert.spv",
        .post_fragment_shader = "post.frag.spv",
        .refraction_pyramid_fragment_shader = "hdr_color_pyramid.frag.spv",
        .shadow_depth_vertex_shader = "shadow.vert.spv",
        .shadow_depth_fragment_shader = "shadow.frag.spv",
    };
}

const cubey::scene::FrameRenderPlan3D& valid_frame_plan() {
    static const cubey::scene::FrameRenderPlan3D plan({
        cubey::scene::RenderPassPlan3D{
            .label = "shadow",
            .kind = cubey::scene::RenderPassKind3D::DepthOnly,
            .frame_plan =
                cubey::scene::RenderFramePlan3D{
                    .view_projection_matrix = cubey::math::Mat4{2.0F},
                },
        },
        cubey::scene::RenderPassPlan3D{
            .label = "scene",
            .kind = cubey::scene::RenderPassKind3D::Color,
            .frame_plan =
                cubey::scene::RenderFramePlan3D{
                    .view_projection_matrix = cubey::math::Mat4{3.0F},
                },
        },
    });
    return plan;
}

cubey::ForwardPbrRenderer3DRenderRequest valid_render_request() {
    return {
        .target =
            {
                .device = reinterpret_cast<const cubey::vulkan::Device*>(0x10),
                .command_buffer = reinterpret_cast<VkCommandBuffer>(0x11),
                .color_target =
                    cubey::render::ColorTargetView{
                        .extent = {1280, 720},
                        .format = VK_FORMAT_R8G8B8A8_UNORM,
                        .image = reinterpret_cast<VkImage>(0x12),
                        .view = reinterpret_cast<VkImageView>(0x13),
                    },
                .frame_slot = cubey::render::FrameSlot{.index = 0, .count = 2},
                .color_initial_state =
                    {
                        .layout = VK_IMAGE_LAYOUT_UNDEFINED,
                        .access_mask = 0,
                        .stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    },
                .color_final_state =
                    {
                        .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                        .access_mask = 0,
                        .stage_mask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                    },
                .command_buffer_label = "vkEndCommandBuffer test renderer",
            },
        .view =
            {
                .scene = reinterpret_cast<const cubey::SceneReadView*>(0x20),
                .frame_plan = &valid_frame_plan(),
                .camera_entity = cubey::Entity{.index = 1, .generation = 1},
                .light_entity = cubey::Entity{.index = 2, .generation = 1},
                .fallback_light =
                    cubey::LightPacket3D{
                        .entity = cubey::Entity{.index = 2, .generation = 1},
                        .kind = cubey::LightKind3D::Directional,
                        .color = {1.0F, 1.0F, 1.0F},
                        .intensity = 1.0F,
                    },
            },
        .scene_resources =
            {
                .meshes =
                    reinterpret_cast<const cubey::render::MeshResourceTable<cubey::render::Mesh>*>(
                        0x30),
                .materials = reinterpret_cast<const cubey::render::PbrMaterialTable*>(0x31),
            },
    };
}

cubey::ForwardPbrRenderer3DFrameRequestInfo valid_frame_request_info() {
    return {
        .device = reinterpret_cast<const cubey::vulkan::Device*>(0x10),
        .command_buffer = reinterpret_cast<VkCommandBuffer>(0x11),
        .color_target =
            cubey::render::ColorTargetView{
                .extent = {1280, 720},
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .image = reinterpret_cast<VkImage>(0x12),
                .view = reinterpret_cast<VkImageView>(0x13),
            },
        .frame_slot = cubey::render::FrameSlot{.index = 0, .count = 2},
        .color_initial_state =
            {
                .layout = VK_IMAGE_LAYOUT_UNDEFINED,
                .access_mask = 0,
                .stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            },
        .color_final_state =
            {
                .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .access_mask = 0,
                .stage_mask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            },
        .command_buffer_label = "vkEndCommandBuffer test helper",
        .scene = reinterpret_cast<const cubey::SceneReadView*>(0x20),
        .frame_plan = &valid_frame_plan(),
        .camera_entity = cubey::Entity{.index = 1, .generation = 1},
        .light_entity = cubey::Entity{.index = 2, .generation = 1},
        .fallback_light =
            cubey::LightPacket3D{
                .entity = cubey::Entity{.index = 2, .generation = 1},
                .kind = cubey::LightKind3D::Directional,
                .color = {1.0F, 1.0F, 1.0F},
                .intensity = 1.0F,
            },
        .scene_resources =
            cubey::ForwardPbrRenderer3DSceneResources{
                .meshes =
                    reinterpret_cast<const cubey::render::MeshResourceTable<cubey::render::Mesh>*>(
                        0x30),
                .materials = reinterpret_cast<const cubey::render::PbrMaterialTable*>(0x31),
            },
        .settings =
            {
                .environment_rotation_degrees = 15.0F,
                .exposure = 1.25F,
            },
    };
}

cubey::scene::RenderDrawPacket3D
routed_packet(std::uint32_t material_index, cubey::render::MaterialAlphaMode alpha_mode,
              cubey::render::MaterialOpticalMode optical_mode,
              cubey::render::MaterialBlendMode blend, VkCullModeFlags cull_mode,
              cubey::render::MaterialPassMask pass_mask, bool cast_shadows = true) {
    return {
        .material = cubey::render::MaterialHandle{.index = material_index, .generation = 1},
        .material_info =
            {
                .alpha_mode = alpha_mode,
                .optical_mode = optical_mode,
                .blend = blend,
                .cull_mode = cull_mode,
                .pass_mask = pass_mask,
            },
        .cast_shadows = cast_shadows,
    };
}

void require_route_indices(const cubey::ForwardPbrDrawPlan& plan, cubey::ForwardPbrDrawRoute route,
                           std::initializer_list<std::uint32_t> expected, const char* message) {
    const std::span<const std::uint32_t> actual = plan.indices(route);
    require(actual.size() == expected.size(), message);
    require(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()), message);
}

} // namespace

void test_forward_pbr_renderer_3d_config_requires_shader_paths_and_shadow_extent() {
    require_throws([] { cubey::validate_forward_pbr_renderer_3d_config({}); },
                   "forward PBR renderer config should reject missing shader paths");

    cubey::ForwardPbrRenderer3DConfig config = valid_config();
    config.post_vertex_shader.clear();
    require_throws([&config] { cubey::validate_forward_pbr_renderer_3d_config(config); },
                   "forward PBR renderer config should reject missing post vertex shader");

    config = valid_config();
    config.post_fragment_shader.clear();
    require_throws([&config] { cubey::validate_forward_pbr_renderer_3d_config(config); },
                   "forward PBR renderer config should reject missing post fragment shader");

    config = valid_config();
    config.refraction_pyramid_fragment_shader.clear();
    require_throws(
        [&config] { cubey::validate_forward_pbr_renderer_3d_config(config); },
        "forward PBR renderer config should reject missing refraction pyramid fragment shader");

    config = valid_config();
    config.shadow_extent = 0;
    require_throws([&config] { cubey::validate_forward_pbr_renderer_3d_config(config); },
                   "forward PBR renderer config should reject zero shadow extent");

    config.shadow_extent = 1024;
    cubey::validate_forward_pbr_renderer_3d_config(config);
}

void test_forward_pbr_renderer_3d_config_defaults_to_hdr_scene_color() {
    const cubey::ForwardPbrRenderer3DConfig config;

    require(config.scene_color_format == VK_FORMAT_R16G16B16A16_SFLOAT,
            "forward PBR renderer should default to a float HDR scene color target");
}

void test_forward_pbr_renderer_3d_config_from_shader_directory_fills_package_paths() {
    cubey::ForwardPbrRenderer3DConfig base;
    base.shadow_extent = 1024;
    base.shadow_depth_format = VK_FORMAT_D32_SFLOAT;
    base.scene_color_format = VK_FORMAT_R32G32B32A32_SFLOAT;

    const cubey::ForwardPbrRenderer3DConfig config =
        cubey::forward_pbr_renderer_3d_config_from_shader_directory("build/shaders", base);

    require(config.pbr_vertex_shader ==
                std::filesystem::path{"build/shaders"} / "forward_pbr.vert.spv",
            "forward PBR shader directory helper should fill the material vertex shader path");
    require(config.pbr_fragment_shader ==
                std::filesystem::path{"build/shaders"} / "forward_pbr.frag.spv",
            "forward PBR shader directory helper should fill the material fragment shader path");
    require(config.skybox_vertex_shader ==
                std::filesystem::path{"build/shaders"} / "forward_pbr_skybox.vert.spv",
            "forward PBR shader directory helper should fill the skybox vertex shader path");
    require(config.skybox_fragment_shader ==
                std::filesystem::path{"build/shaders"} / "forward_pbr_skybox.frag.spv",
            "forward PBR shader directory helper should fill the skybox fragment shader path");
    require(config.atmosphere_vertex_shader ==
                std::filesystem::path{"build/shaders"} / "atmosphere.vert.spv",
            "forward PBR shader directory helper should fill the atmosphere vertex shader path");
    require(config.atmosphere_fragment_shader ==
                std::filesystem::path{"build/shaders"} / "atmosphere.frag.spv",
            "forward PBR shader directory helper should fill the atmosphere fragment shader path");
    require(config.post_vertex_shader ==
                std::filesystem::path{"build/shaders"} / "forward_pbr_post.vert.spv",
            "forward PBR shader directory helper should fill the post vertex shader path");
    require(config.post_fragment_shader ==
                std::filesystem::path{"build/shaders"} / "forward_pbr_post.frag.spv",
            "forward PBR shader directory helper should fill the post fragment shader path");
    require(config.refraction_pyramid_fragment_shader ==
                std::filesystem::path{"build/shaders"} / "hdr_color_pyramid.frag.spv",
            "forward PBR shader directory helper should fill the refraction pyramid shader path");
    require(config.shadow_depth_vertex_shader ==
                std::filesystem::path{"build/shaders"} / "forward_pbr_shadow_depth.vert.spv",
            "forward PBR shader directory helper should fill the shadow vertex shader path");
    require(config.shadow_depth_fragment_shader ==
                std::filesystem::path{"build/shaders"} / "forward_pbr_shadow_depth.frag.spv",
            "forward PBR shader directory helper should fill the shadow fragment shader path");
    require(config.shadow_extent == 1024,
            "forward PBR shader directory helper should preserve shadow extent");
    require(config.shadow_depth_format == VK_FORMAT_D32_SFLOAT,
            "forward PBR shader directory helper should preserve shadow depth format");
    require(config.scene_color_format == VK_FORMAT_R32G32B32A32_SFLOAT,
            "forward PBR shader directory helper should preserve scene color format");
}

void test_forward_pbr_renderer_3d_config_from_shader_directory_rejects_empty_directory() {
    require_throws(
        [] { static_cast<void>(cubey::forward_pbr_renderer_3d_config_from_shader_directory({})); },
        "forward PBR shader directory helper should reject an empty shader directory");
}

void test_forward_pbr_renderer_3d_refraction_pyramid_policy_is_hdr_and_bounded() {
    cubey::render::HdrColorPyramidConfig config{
        .extent = {1280U, 720U},
        .format = VK_FORMAT_R16G16B16A16_SFLOAT,
        .frame_slot_count = 2U,
        .minimum_mip_extent = 16U,
    };
    cubey::render::validate_hdr_color_pyramid_config(config);
    require(cubey::render::hdr_color_pyramid_mip_levels(config) == 8U,
            "HDR color pyramid should end at a roughly sixteen-pixel maximum dimension");
    const VkExtent2D final_extent = cubey::render::texture_2d_mip_extent(config.extent, 7U);
    require(final_extent.width == 10U && final_extent.height == 5U,
            "HDR color pyramid final mip should clamp each dimension independently");

    config.extent = {17U, 1U};
    require(cubey::render::hdr_color_pyramid_mip_levels(config) == 2U,
            "HDR color pyramid should retain a usable base and one downsample for narrow targets");
    const VkExtent2D narrow_final = cubey::render::texture_2d_mip_extent(config.extent, 1U);
    require(narrow_final.width == 8U && narrow_final.height == 1U,
            "HDR color pyramid mip extents should never collapse a narrow dimension to zero");

    const cubey::render::MaterialPassInfo pass =
        cubey::render::hdr_color_pyramid_filter_pass_info();
    require(pass.label == "hdr.color_pyramid.filter" && pass.descriptor_sets.size() == 1U,
            "HDR color pyramid should declare a reusable sampled-radiance filter pass");

    const auto* texture = reinterpret_cast<const cubey::render::Texture2D*>(0x1);
    const cubey::render::HdrColorPyramidSnapshot pending{
        .texture = texture,
        .max_lod = 7.0F,
        .valid = false,
    };
    require(pending.bindable() && !pending.valid,
            "same-command pyramid output should be bindable before its first recording completes");
}

void test_forward_pbr_renderer_3d_target_resources_use_material_table() {
    const cubey::render::PbrMaterialTable* materials =
        reinterpret_cast<const cubey::render::PbrMaterialTable*>(0x40);
    const cubey::ForwardPbrRenderer3DTargetResourcesInfo info{
        .extent = {640, 360},
        .color_format = VK_FORMAT_R8G8B8A8_UNORM,
        .materials = materials,
    };

    require(info.materials == materials,
            "forward PBR target resources should keep the PBR material table");

    const std::filesystem::path source_root{CUBEY_SOURCE_DIR};
    const std::string header =
        read_source_file(source_root / "include/cubey/engine/forward_pbr_renderer_3d.h");
    require_not_contains(header, "material_descriptor_set_layout",
                         "forward PBR callers should not pass a raw material descriptor layout");
}

void test_forward_pbr_renderer_3d_builds_render_request_from_frame_info() {
    cubey::ForwardPbrRenderer3DFrameRequestInfo info = valid_frame_request_info();
    info.command_buffer_mode = cubey::render::RenderGraphCommandBufferMode::AlreadyRecording;
    info.profiler = reinterpret_cast<cubey::vulkan::GpuTimestampProfiler*>(0x14);
    const cubey::ForwardPbrRenderer3DRenderRequest request =
        cubey::forward_pbr_renderer_3d_render_request(info);

    require(request.target.device == info.device,
            "forward PBR request helper should copy the target device");
    require(request.target.command_buffer == info.command_buffer,
            "forward PBR request helper should copy the command buffer");
    require(request.target.color_target.image == info.color_target.image,
            "forward PBR request helper should copy the color target image");
    require(request.target.color_target.view == info.color_target.view,
            "forward PBR request helper should copy the color target view");
    require(request.target.color_target.extent.width == info.color_target.extent.width,
            "forward PBR request helper should copy the color target extent");
    require(request.target.color_target.format == info.color_target.format,
            "forward PBR request helper should copy the color target format");
    require(request.target.frame_slot.index == info.frame_slot.index,
            "forward PBR request helper should copy the frame slot index");
    require(request.target.frame_slot.count == info.frame_slot.count,
            "forward PBR request helper should copy the frame slot count");
    require(request.target.color_initial_state == info.color_initial_state,
            "forward PBR request helper should copy the initial color state");
    require(request.target.color_final_state == info.color_final_state,
            "forward PBR request helper should copy the final color state");
    require(request.target.command_buffer_label == info.command_buffer_label,
            "forward PBR request helper should copy the command buffer label");
    require(request.target.command_buffer_mode == info.command_buffer_mode,
            "forward PBR request helper should copy the command buffer mode");
    require(request.target.profiler == info.profiler,
            "forward PBR request helper should copy the GPU profiler");
    require(request.view.scene == info.scene, "forward PBR request helper should copy scene view");
    require(request.view.frame_plan == info.frame_plan,
            "forward PBR request helper should copy frame plan");
    require(request.scene_resources.materials == info.scene_resources.materials,
            "forward PBR request helper should copy resources");
    require_near(request.settings.exposure, 1.25F,
                 "forward PBR request helper should copy display settings");
}

void test_forward_pbr_renderer_3d_frame_metrics_are_caller_owned_and_reused() {
    cubey::ForwardPbrRenderer3DFrameMetrics metrics;
    cubey::ForwardPbrRenderer3DFrameRequestInfo info = valid_frame_request_info();
    info.metrics = &metrics;
    const cubey::ForwardPbrRenderer3DRenderRequest request =
        cubey::forward_pbr_renderer_3d_render_request(info);

    require(request.metrics == &metrics,
            "forward PBR request conversion should preserve the caller-owned metrics output");
    metrics.draw_plan.scene_source_packet_count = 3U;
    metrics.material_table.material_count = 4U;
    metrics.render_graph.pass_count = 5U;
    metrics.render_graph_frame.slot_action = cubey::render::RenderGraphFrameSlotAction::Reused;
    require(metrics.draw_plan.scene_source_packet_count == 3U &&
                metrics.material_table.material_count == 4U &&
                metrics.render_graph.pass_count == 5U &&
                metrics.render_graph_frame.slot_action ==
                    cubey::render::RenderGraphFrameSlotAction::Reused,
            "forward PBR frame metrics should expose plain caller-owned snapshots");

    const std::filesystem::path source_root{CUBEY_SOURCE_DIR};
    const std::string source =
        read_source_file(source_root / "src/cubey/engine/forward_pbr_renderer_3d_graph.cpp");
    require_contains(source,
                     "const ForwardPbrDrawPlanMetrics& draw_plan_metrics = draw_plan.metrics()",
                     "forward PBR metrics should reuse the computed draw plan counts");
    require(source.find("build_forward_pbr_draw_plan(frame_plans)") ==
                source.rfind("build_forward_pbr_draw_plan(frame_plans)"),
            "forward PBR recording should build the draw plan only once per frame");
}

void test_forward_pbr_renderer_3d_record_accepts_frame_request_info() {
    const std::filesystem::path source_root{CUBEY_SOURCE_DIR};
    const std::string header =
        read_source_file(source_root / "include/cubey/engine/forward_pbr_renderer_3d.h");
    const std::string source =
        read_source_file(source_root / "src/cubey/engine/forward_pbr_renderer_3d_graph.cpp");

    require_contains(header, "void record(const ForwardPbrRenderer3DFrameRequestInfo& info);",
                     "forward PBR renderer should expose direct frame-info recording");
    require_contains(source, "record(forward_pbr_renderer_3d_render_request(info));",
                     "direct frame-info recording should delegate through the validated request");
}

void test_forward_pbr_renderer_3d_record_requires_created_resources() {
    cubey::ForwardPbrRenderer3D renderer(valid_config());

    require_throws([&renderer] { renderer.record(valid_render_request()); },
                   "forward PBR record should reject calls before renderer resources exist");
}

void test_forward_pbr_renderer_3d_lifecycle_guards_resource_ordering() {
    const std::filesystem::path source_root{CUBEY_SOURCE_DIR};
    const std::string internal_header =
        read_source_file(source_root / "src/cubey/engine/forward_pbr_renderer_3d_internal.h");
    const std::string lifecycle =
        read_source_file(source_root / "src/cubey/engine/forward_pbr_renderer_3d.cpp");
    const std::string resources =
        read_source_file(source_root / "src/cubey/engine/forward_pbr_renderer_3d_resources.cpp");
    const std::string graph =
        read_source_file(source_root / "src/cubey/engine/forward_pbr_renderer_3d_graph.cpp");

    require_contains(internal_header, "has_global_resources",
                     "forward PBR internals should expose a global-resource lifecycle query");
    require_contains(internal_header, "has_swapchain_resources",
                     "forward PBR internals should expose a swapchain-resource lifecycle query");
    require_contains(lifecycle, "require_no_global_resources",
                     "forward PBR lifecycle should reject repeated global creation");
    require_contains(lifecycle, "require_no_swapchain_resources",
                     "forward PBR lifecycle should reject repeated swapchain creation");
    require_contains(resources, "require_global_resources();",
                     "forward PBR swapchain creation should require global resources first");
    require_contains(resources, "require_no_swapchain_resources();",
                     "forward PBR swapchain creation should reject duplicate create calls");
    require_contains(resources, "depth_attachment.emplace(device, info.extent, true)",
                     "forward PBR scene depth should support atmosphere cloud sampling");
    require_contains(graph, "require_swapchain_resources();",
                     "forward PBR record should require swapchain resources before recording");
}

void test_forward_pbr_renderer_3d_render_request_validates_required_target_fields() {
    cubey::ForwardPbrRenderer3DRenderRequest request = valid_render_request();
    cubey::validate_forward_pbr_renderer_3d_render_request(request);

    request.target.device = nullptr;
    require_throws([&request] { cubey::validate_forward_pbr_renderer_3d_render_request(request); },
                   "forward PBR render request should reject missing device");

    request = valid_render_request();
    request.target.command_buffer = VK_NULL_HANDLE;
    require_throws([&request] { cubey::validate_forward_pbr_renderer_3d_render_request(request); },
                   "forward PBR render request should reject missing command buffer");
}

void test_forward_pbr_renderer_3d_render_request_validates_required_view_fields() {
    cubey::ForwardPbrRenderer3DRenderRequest request = valid_render_request();
    request.view.scene = nullptr;
    require_throws([&request] { cubey::validate_forward_pbr_renderer_3d_render_request(request); },
                   "forward PBR render request should reject missing scene read view");

    request = valid_render_request();
    request.view.frame_plan = nullptr;
    require_throws([&request] { cubey::validate_forward_pbr_renderer_3d_render_request(request); },
                   "forward PBR render request should reject missing frame plan");
}

void test_forward_pbr_renderer_3d_render_request_validates_required_resource_fields() {
    cubey::ForwardPbrRenderer3DRenderRequest request = valid_render_request();
    request.scene_resources.meshes = nullptr;
    require_throws([&request] { cubey::validate_forward_pbr_renderer_3d_render_request(request); },
                   "forward PBR render request should reject missing mesh table");

    request = valid_render_request();
    request.scene_resources.materials = nullptr;
    require_throws([&request] { cubey::validate_forward_pbr_renderer_3d_render_request(request); },
                   "forward PBR render request should reject missing material table");
}

void test_forward_pbr_renderer_3d_render_request_validates_atmosphere_background_uniforms() {
    cubey::ForwardPbrRenderer3DRenderRequest request = valid_render_request();
    request.settings.background_mode = cubey::ForwardPbrRenderer3DBackgroundMode::Atmosphere;
    require_throws([&request] { cubey::validate_forward_pbr_renderer_3d_render_request(request); },
                   "forward PBR atmosphere background should require frame uniforms");

    request.settings.atmosphere_background.emplace();
    cubey::validate_forward_pbr_renderer_3d_render_request(request);
}

void test_forward_pbr_renderer_3d_render_request_validates_atmosphere_clouds() {
    cubey::CloudEnvironmentRuntime clouds;
    cubey::ForwardPbrRenderer3DRenderRequest request = valid_render_request();
    request.settings.atmosphere_clouds = cubey::ForwardPbrRenderer3DAtmosphereClouds{
        .runtime = &clouds,
    };
    require_throws([&request] { cubey::validate_forward_pbr_renderer_3d_render_request(request); },
                   "forward PBR atmosphere clouds should require the atmosphere background");

    request.settings.background_mode = cubey::ForwardPbrRenderer3DBackgroundMode::Atmosphere;
    request.settings.atmosphere_background.emplace();
    cubey::validate_forward_pbr_renderer_3d_render_request(request);

    request.settings.atmosphere_clouds->runtime = nullptr;
    require_throws([&request] { cubey::validate_forward_pbr_renderer_3d_render_request(request); },
                   "forward PBR atmosphere clouds should require a runtime");
}

void test_forward_pbr_renderer_3d_render_request_validates_terrain_backdrop() {
    cubey::ForwardPbrRenderer3DRenderRequest request = valid_render_request();
    request.settings.terrain_backdrop.emplace();
    require_throws([&request] { cubey::validate_forward_pbr_renderer_3d_render_request(request); },
                   "forward PBR terrain should require the atmosphere background");

    request.settings.background_mode = cubey::ForwardPbrRenderer3DBackgroundMode::Atmosphere;
    request.settings.atmosphere_background.emplace();
    require_throws([&request] { cubey::validate_forward_pbr_renderer_3d_render_request(request); },
                   "forward PBR terrain should require a complete runtime");
}

void test_forward_pbr_renderer_3d_render_request_validates_ocean_surface() {
    cubey::ForwardPbrRenderer3DRenderRequest request = valid_render_request();
    request.settings.ocean_surface.emplace();
    require_throws([&request] { cubey::validate_forward_pbr_renderer_3d_render_request(request); },
                   "forward PBR ocean should require the atmosphere background");

    request.settings.background_mode = cubey::ForwardPbrRenderer3DBackgroundMode::Atmosphere;
    request.settings.atmosphere_background.emplace();
    require_throws([&request] { cubey::validate_forward_pbr_renderer_3d_render_request(request); },
                   "forward PBR ocean should require a complete runtime");

    request.settings.terrain_backdrop.emplace();
    require_throws([&request] { cubey::validate_forward_pbr_renderer_3d_render_request(request); },
                   "forward PBR v1 should reject simultaneous terrain and ocean");
}

void test_forward_pbr_renderer_3d_frame_plan_selects_required_passes() {
    const cubey::scene::FrameRenderPlan3D valid({
        cubey::scene::RenderPassPlan3D{
            .label = "shadow",
            .kind = cubey::scene::RenderPassKind3D::DepthOnly,
            .frame_plan =
                cubey::scene::RenderFramePlan3D{.view_projection_matrix = cubey::math::Mat4{2.0F}},
        },
        cubey::scene::RenderPassPlan3D{
            .label = "scene",
            .kind = cubey::scene::RenderPassKind3D::Color,
            .frame_plan =
                cubey::scene::RenderFramePlan3D{.view_projection_matrix = cubey::math::Mat4{3.0F}},
        },
    });
    const cubey::ForwardPbrRenderer3DFramePlans plans =
        cubey::forward_pbr_renderer_3d_frame_plans(valid);
    require(plans.shadow == &valid.passes()[0].frame_plan,
            "forward PBR renderer should select the depth-only pass as shadow");
    require(plans.scene == &valid.passes()[1].frame_plan,
            "forward PBR renderer should select the color pass as scene");

    const cubey::scene::FrameRenderPlan3D missing_shadow({
        cubey::scene::RenderPassPlan3D{.kind = cubey::scene::RenderPassKind3D::Color},
    });
    require_throws(
        [&] {
            const auto plans = cubey::forward_pbr_renderer_3d_frame_plans(missing_shadow);
            (void)plans;
        },
        "forward PBR renderer should reject a plan without a depth-only pass");

    const cubey::scene::FrameRenderPlan3D duplicate_scene({
        cubey::scene::RenderPassPlan3D{.kind = cubey::scene::RenderPassKind3D::DepthOnly},
        cubey::scene::RenderPassPlan3D{.kind = cubey::scene::RenderPassKind3D::Color},
        cubey::scene::RenderPassPlan3D{.kind = cubey::scene::RenderPassKind3D::Color},
    });
    require_throws(
        [&] {
            const auto plans = cubey::forward_pbr_renderer_3d_frame_plans(duplicate_scene);
            (void)plans;
        },
        "forward PBR renderer should reject duplicate color passes");
}

void test_forward_pbr_draw_plan_routes_packets_in_source_order() {
    const cubey::render::MaterialPassMask depth_and_color =
        cubey::render::default_material_pass_mask();
    const cubey::render::MaterialPassMask color_only =
        cubey::render::material_pass_mask(cubey::render::MaterialPassKind::ForwardColor);
    const cubey::render::MaterialPassMask depth_only =
        cubey::render::material_pass_mask(cubey::render::MaterialPassKind::DepthOnly);
    const auto opaque = cubey::render::MaterialAlphaMode::Opaque;
    const auto mask = cubey::render::MaterialAlphaMode::Mask;
    const auto blend = cubey::render::MaterialAlphaMode::Blend;
    const auto opaque_optics = cubey::render::MaterialOpticalMode::Opaque;
    const auto transmission = cubey::render::MaterialOpticalMode::Transmission;
    const auto opaque_blend = cubey::render::MaterialBlendMode::Opaque;
    const auto alpha_blend = cubey::render::MaterialBlendMode::AlphaBlend;

    cubey::scene::RenderFramePlan3D shadow_plan{
        .draw_packets =
            {
                routed_packet(20, opaque, opaque_optics, opaque_blend, VK_CULL_MODE_BACK_BIT,
                              depth_and_color),
                routed_packet(21, opaque, opaque_optics, opaque_blend, VK_CULL_MODE_NONE,
                              depth_and_color),
                routed_packet(22, mask, opaque_optics, opaque_blend, VK_CULL_MODE_BACK_BIT,
                              depth_and_color),
                routed_packet(23, mask, opaque_optics, opaque_blend, VK_CULL_MODE_NONE,
                              depth_and_color),
                routed_packet(24, opaque, opaque_optics, opaque_blend, VK_CULL_MODE_BACK_BIT,
                              depth_and_color, false),
                routed_packet(25, opaque, transmission, opaque_blend, VK_CULL_MODE_BACK_BIT,
                              depth_and_color),
                routed_packet(26, blend, opaque_optics, alpha_blend, VK_CULL_MODE_BACK_BIT,
                              depth_and_color),
                routed_packet(27, opaque, opaque_optics, opaque_blend, VK_CULL_MODE_FRONT_BIT,
                              depth_and_color),
                routed_packet(28, opaque, opaque_optics, opaque_blend, VK_CULL_MODE_BACK_BIT,
                              color_only),
                routed_packet(29, mask, opaque_optics, opaque_blend, VK_CULL_MODE_BACK_BIT,
                              depth_and_color),
                routed_packet(30, opaque, opaque_optics, opaque_blend, VK_CULL_MODE_NONE,
                              depth_and_color),
            },
    };
    cubey::scene::RenderFramePlan3D scene_plan{
        .draw_packets =
            {
                routed_packet(1, opaque, opaque_optics, opaque_blend, VK_CULL_MODE_BACK_BIT,
                              color_only),
                routed_packet(2, opaque, opaque_optics, opaque_blend, VK_CULL_MODE_NONE,
                              color_only),
                routed_packet(3, blend, opaque_optics, alpha_blend, VK_CULL_MODE_BACK_BIT,
                              color_only),
                routed_packet(4, blend, opaque_optics, alpha_blend, VK_CULL_MODE_NONE, color_only),
                routed_packet(5, opaque, transmission, opaque_blend, VK_CULL_MODE_BACK_BIT,
                              color_only),
                routed_packet(6, opaque, transmission, opaque_blend, VK_CULL_MODE_NONE, color_only),
                routed_packet(7, blend, transmission, alpha_blend, VK_CULL_MODE_BACK_BIT,
                              color_only),
                routed_packet(8, blend, transmission, alpha_blend, VK_CULL_MODE_NONE, color_only),
                routed_packet(1, opaque, opaque_optics, opaque_blend, VK_CULL_MODE_BACK_BIT,
                              color_only),
                routed_packet(9, opaque, opaque_optics, opaque_blend, VK_CULL_MODE_BACK_BIT,
                              depth_only),
                routed_packet(10, opaque, opaque_optics, opaque_blend, VK_CULL_MODE_FRONT_BIT,
                              color_only),
                routed_packet(11, mask, opaque_optics, opaque_blend, VK_CULL_MODE_BACK_BIT,
                              color_only),
                routed_packet(12, opaque, transmission, opaque_blend, VK_CULL_MODE_FRONT_BIT,
                              color_only),
                routed_packet(13, opaque, transmission, opaque_blend, VK_CULL_MODE_BACK_BIT,
                              depth_only),
            },
    };

    const cubey::ForwardPbrDrawPlan plan = cubey::build_forward_pbr_draw_plan({
        .shadow = &shadow_plan,
        .scene = &scene_plan,
    });

    require_route_indices(plan, cubey::ForwardPbrDrawRoute::ShadowOpaqueBack, {0},
                          "forward PBR plan should retain opaque shadow back-cull source order");
    require_route_indices(plan, cubey::ForwardPbrDrawRoute::ShadowOpaqueNoCull, {1, 10},
                          "forward PBR plan should retain no-cull opaque shadow source order");
    require_route_indices(plan, cubey::ForwardPbrDrawRoute::ShadowMaskedBack, {2, 9},
                          "forward PBR plan should retain masked shadow back-cull source order");
    require_route_indices(plan, cubey::ForwardPbrDrawRoute::ShadowMaskedNoCull, {3},
                          "forward PBR plan should retain masked shadow no-cull source order");
    require_route_indices(plan, cubey::ForwardPbrDrawRoute::SceneOpaqueBack, {0, 8, 11},
                          "forward PBR plan should preserve ordinary opaque source order");
    require_route_indices(plan, cubey::ForwardPbrDrawRoute::SceneOpaqueNoCull, {1},
                          "forward PBR plan should route ordinary no-cull opaque packets");
    require_route_indices(plan, cubey::ForwardPbrDrawRoute::SceneAlphaBack, {2},
                          "forward PBR plan should route ordinary back-cull alpha packets");
    require_route_indices(plan, cubey::ForwardPbrDrawRoute::SceneAlphaNoCull, {3},
                          "forward PBR plan should route ordinary no-cull alpha packets");
    require_route_indices(plan, cubey::ForwardPbrDrawRoute::TransmissionOpaqueBack, {4},
                          "forward PBR plan should route back-cull transmission opaque packets");
    require_route_indices(plan, cubey::ForwardPbrDrawRoute::TransmissionOpaqueNoCull, {5},
                          "forward PBR plan should route no-cull transmission opaque packets");
    require_route_indices(plan, cubey::ForwardPbrDrawRoute::TransmissionAlphaBack, {6},
                          "forward PBR plan should route back-cull transmission alpha packets");
    require_route_indices(plan, cubey::ForwardPbrDrawRoute::TransmissionAlphaNoCull, {7},
                          "forward PBR plan should route no-cull transmission alpha packets");

    const cubey::ForwardPbrDrawPlanMetrics& metrics = plan.metrics();
    require(plan.has_transmission() && metrics.has_transmission,
            "forward PBR plan should preserve the transmission graph trigger");
    require(metrics.scene_source_packet_count == 14U && metrics.scene_classification_count == 14U &&
                metrics.shadow_source_packet_count == 11U &&
                metrics.shadow_classification_count == 11U,
            "forward PBR plan should classify each scene and shadow source exactly once");
    require(metrics.route_packet_reference_count == 16U,
            "forward PBR plan should count routed packet references across all passes");
    require(metrics.visible_scene_unique_material_count == 9U,
            "forward PBR plan should count unique visible scene materials only");
}

void test_forward_pbr_draw_plan_retains_transmission_trigger_for_unsupported_routes() {
    const cubey::scene::RenderFramePlan3D shadow_plan{};
    const cubey::scene::RenderFramePlan3D scene_plan{
        .draw_packets =
            {
                routed_packet(1, cubey::render::MaterialAlphaMode::Opaque,
                              cubey::render::MaterialOpticalMode::Transmission,
                              cubey::render::MaterialBlendMode::Opaque, VK_CULL_MODE_FRONT_BIT,
                              cubey::render::material_pass_mask(
                                  cubey::render::MaterialPassKind::ForwardColor)),
            },
    };
    const cubey::ForwardPbrDrawPlan plan = cubey::build_forward_pbr_draw_plan({
        .shadow = &shadow_plan,
        .scene = &scene_plan,
    });

    require(plan.has_transmission() && plan.metrics().has_transmission,
            "unsupported transmission cull modes should retain the prior graph trigger");
    require(plan.metrics().route_packet_reference_count == 0U &&
                plan.metrics().visible_scene_unique_material_count == 0U,
            "unsupported cull routes should not enter renderer draw bins");

    const cubey::scene::RenderFramePlan3D opaque_scene_plan{
        .draw_packets =
            {
                routed_packet(2, cubey::render::MaterialAlphaMode::Opaque,
                              cubey::render::MaterialOpticalMode::Opaque,
                              cubey::render::MaterialBlendMode::Opaque, VK_CULL_MODE_BACK_BIT,
                              cubey::render::material_pass_mask(
                                  cubey::render::MaterialPassKind::ForwardColor)),
            },
    };
    const cubey::ForwardPbrDrawPlan opaque_plan = cubey::build_forward_pbr_draw_plan({
        .shadow = &shadow_plan,
        .scene = &opaque_scene_plan,
    });
    require(!opaque_plan.has_transmission() && !opaque_plan.metrics().has_transmission,
            "ordinary-only scene packets should preserve the direct graph path");
}

void test_forward_pbr_renderer_3d_settings_defaults_to_aces_display_transform() {
    const cubey::ForwardPbrRenderer3DSettings settings;

    require(settings.environment_rotation_degrees == 0.0F,
            "forward PBR renderer settings should default to no environment rotation");
    require(settings.exposure == 0.0F,
            "forward PBR renderer settings should default to neutral exposure");
    require(settings.tonemap == cubey::render::PbrTonemap::Aces,
            "forward PBR renderer settings should default to ACES tonemap");
    require(settings.debug_view == cubey::render::PbrDebugView::Final,
            "forward PBR renderer settings should default to final shaded output");
    require(settings.background_mode == cubey::ForwardPbrRenderer3DBackgroundMode::IblSkybox,
            "forward PBR renderer settings should default to the IBL skybox background");
    require(!settings.atmosphere_background.has_value(),
            "forward PBR renderer settings should not carry atmosphere uniforms by default");
    require(!settings.atmosphere_clouds.has_value(),
            "forward PBR renderer settings should not enable atmosphere clouds by default");
}

void test_forward_pbr_renderer_3d_selects_requested_light_or_fallback() {
    const cubey::Entity requested{.index = 2, .generation = 1};
    const cubey::Entity other{.index = 3, .generation = 1};
    const cubey::LightPacket3D fallback{
        .entity = cubey::Entity{.index = 9, .generation = 1},
        .color = {0.1F, 0.2F, 0.3F},
        .intensity = 4.0F,
        .direction = {0.0F, -1.0F, 0.0F},
    };
    const cubey::LightPacket3D packets[]{
        cubey::LightPacket3D{
            .entity = other,
            .color = {1.0F, 0.0F, 0.0F},
            .intensity = 1.0F,
        },
        cubey::LightPacket3D{
            .entity = requested,
            .color = {0.7F, 0.8F, 0.9F},
            .intensity = 2.5F,
            .direction = {1.0F, 0.0F, 0.0F},
        },
    };

    const cubey::LightPacket3D selected =
        cubey::forward_pbr_renderer_3d_selected_light(packets, requested, fallback);
    require(selected.entity == requested, "forward PBR renderer should select the requested light");
    require(selected.color == cubey::math::Vec3{0.7F, 0.8F, 0.9F},
            "forward PBR renderer should preserve selected light color");
    require(selected.intensity == 2.5F,
            "forward PBR renderer should preserve selected light intensity");

    const cubey::LightPacket3D missing = cubey::forward_pbr_renderer_3d_selected_light(
        std::span<const cubey::LightPacket3D>{}, requested, fallback);
    require(missing.entity == fallback.entity,
            "forward PBR renderer should fall back when the requested light is absent");
    require(missing.intensity == fallback.intensity,
            "forward PBR renderer should preserve fallback light intensity");
}

void test_forward_pbr_renderer_3d_shadow_vertex_layout_matches_pbr_vertices() {
    const cubey::render::VertexInputLayout layout =
        cubey::forward_pbr_renderer_3d_shadow_vertex_input_layout();
    require(layout.bindings().size() == 1,
            "forward PBR shadow vertex layout should expose one binding");
    require(layout.bindings()[0].stride == sizeof(cubey::render::PbrVertex),
            "forward PBR shadow vertex layout should use the PBR vertex stride");
    require(layout.attributes.size() == 4,
            "forward PBR shadow vertex layout should expose position, UVs, and vertex color");
    require(layout.attributes[0].location == 0,
            "forward PBR shadow vertex layout should bind position at location 0");
    require(layout.attributes[0].offset == offsetof(cubey::render::PbrVertex, position),
            "forward PBR shadow vertex layout should read PBR vertex position");
    require(layout.attributes[1].location == 3,
            "forward PBR shadow vertex layout should bind UV0 at the PBR UV location");
    require(layout.attributes[1].offset == offsetof(cubey::render::PbrVertex, uv0),
            "forward PBR shadow vertex layout should read PBR vertex UV0");
    require(layout.attributes[2].location == 4,
            "forward PBR shadow vertex layout should bind UV1 at the PBR UV1 location");
    require(layout.attributes[2].offset == offsetof(cubey::render::PbrVertex, uv1),
            "forward PBR shadow vertex layout should read PBR vertex UV1");
    require(layout.attributes[3].location == 5,
            "forward PBR shadow vertex layout should bind COLOR0 at the PBR color location");
    require(layout.attributes[3].offset == offsetof(cubey::render::PbrVertex, color0),
            "forward PBR shadow vertex layout should read PBR vertex COLOR0");
}

void test_forward_pbr_renderer_3d_binds_shadow_depth_with_depth_read_layout() {
    const std::filesystem::path root{CUBEY_SOURCE_DIR};
    const std::string source =
        read_source_file(root / "src/cubey/engine/forward_pbr_renderer_3d_resources.cpp");

    require_contains(source, "PbrSceneBinding::ShadowMap",
                     "forward PBR renderer should bind the shadow map in the scene set");
    require_contains(source, ".layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL",
                     "forward PBR shadow map descriptor should match sampled depth layout");
}

void test_forward_pbr_renderer_3d_records_masked_shadow_path_with_material_alpha() {
    const std::filesystem::path root{CUBEY_SOURCE_DIR};
    const std::string header =
        read_source_file(root / "include/cubey/engine/forward_pbr_renderer_3d.h");
    const std::string internal_header =
        read_source_file(root / "src/cubey/engine/forward_pbr_renderer_3d_internal.h");
    const std::string resources =
        read_source_file(root / "src/cubey/engine/forward_pbr_renderer_3d_resources.cpp");
    const std::string recording =
        read_source_file(root / "src/cubey/engine/forward_pbr_renderer_3d_recording.cpp");
    const std::string draw_plan =
        read_source_file(root / "src/cubey/engine/forward_pbr_renderer_3d_draw_plan.cpp");
    const std::string graph =
        read_source_file(root / "src/cubey/engine/forward_pbr_renderer_3d_graph.cpp");
    const std::string material = read_source_file(root / "include/cubey/render/material.h");
    const std::string pyramid_header =
        read_source_file(root / "include/cubey/render/hdr_color_pyramid.h");
    const std::string pyramid = read_source_file(root / "src/cubey/render/hdr_color_pyramid.cpp");
    const std::string pyramid_shader =
        read_source_file(root / "shaders/cubey/forward_pbr/hdr_color_pyramid.frag");
    const std::string importer =
        read_source_file(root / "src/cubey/engine/gltf_scene_importer_materials.cpp");

    require_contains(header, "shadow_depth_fragment_shader",
                     "forward PBR config should expose a mask-capable shadow fragment shader");
    require_contains(header, "std::unique_ptr<Impl> impl_",
                     "forward PBR public header should hide renderer runtime state behind Impl");
    require_not_contains(header, "enum class ForwardPbrPipelineVariant",
                         "forward PBR public header should not expose pipeline variants");
    require_not_contains(header, "ForwardPbrDrawPlan",
                         "forward PBR public header should not expose draw-plan routing");
    require_not_contains(header, "pipeline_variants_",
                         "forward PBR public header should not expose pipeline storage");
    require_not_contains(header, "<cubey/render/shadow_map.h>",
                         "forward PBR public header should not expose the shadow-map helper");
    require_contains(internal_header, "enum class ForwardPbrPipelineVariant",
                     "forward PBR internals should key pass/cull/blend pipeline variants");
    require_contains(internal_header, "SwapchainResources",
                     "forward PBR internals should group swapchain-lifetime renderer state");
    require_contains(internal_header, "pipeline_variants",
                     "forward PBR internals should store keyed pipeline variants together");
    require_contains(internal_header, "struct ForwardPbrDrawPlanMetrics",
                     "forward PBR internals should expose renderer-private plan metrics");
    require_contains(internal_header, "class ForwardPbrDrawPlan",
                     "forward PBR internals should retain renderer-private routing state");
    require_contains(resources, "pipeline_variant_slot(ForwardPbrPipelineVariant::MaskShadow)",
                     "forward PBR renderer should own a mask-capable shadow pipeline variant");
    require_not_contains(
        recording, "materials.upload",
        "forward PBR recording should not upload immutable material uniforms per draw");
    require_contains(resources,
                     "pipeline_variant_slot(ForwardPbrPipelineVariant::OpaqueDoubleSided)",
                     "forward PBR renderer should own a double-sided opaque pipeline variant");
    require_contains(resources,
                     "pipeline_variant_slot(ForwardPbrPipelineVariant::AlphaDoubleSided)",
                     "forward PBR renderer should own a double-sided alpha pipeline variant");
    require_contains(
        resources, "pipeline_variant_slot(ForwardPbrPipelineVariant::MaskShadowDoubleSided)",
        "forward PBR renderer should own a double-sided masked shadow pipeline variant");
    require_not_contains(internal_header, "opaque_double_sided_pipeline_",
                         "forward PBR renderer should not store named pipeline optionals");
    require_not_contains(internal_header, "alpha_double_sided_pipeline_",
                         "forward PBR renderer should not store named alpha optionals");
    require_not_contains(internal_header, "mask_shadow_double_sided_pipeline_",
                         "forward PBR renderer should not store named mask optionals");
    require_contains(resources, "fragment_shader_file(config_.shadow_depth_fragment_shader)",
                     "mask shadow pipeline should compile the configured fragment shader");
    require_contains(draw_plan, "VK_CULL_MODE_BACK_BIT",
                     "forward draw planning should filter single-sided materials by cull policy");
    require_contains(draw_plan, "VK_CULL_MODE_NONE",
                     "forward draw planning should route double-sided materials to no-cull bins");
    require_contains(draw_plan, "render::MaterialAlphaMode::Opaque",
                     "forward draw planning should keep a cheap opaque depth path");
    require_contains(draw_plan, "render::MaterialAlphaMode::Mask",
                     "forward draw planning should record a mask-aware depth path");
    require_contains(recording, "bind_pbr_material",
                     "masked shadow recording should bind the static material descriptor set");
    require_not_contains(recording, "materials.instance(",
                         "forward PBR should not use frame-slotted PBR material residency");
    require_contains(
        importer, "const render::MaterialAlphaMode alpha_mode = gltf_alpha_mode(source.alpha_mode)",
        "glTF importer should map source alpha modes into render material policy");
    require_contains(importer, "volume_boundary || !source.double_sided",
                     "glTF importer should preserve doubleSided for non-volume render policy");
    require_contains(importer, "const bool volume_boundary",
                     "glTF importer should select a separate closed-volume boundary cull policy");
    require_contains(importer, ".dispersion = source.dispersion",
                     "glTF importer should carry dispersion into the shared PBR factors");
    require_contains(material, "enum class MaterialOpticalMode",
                     "forward staging should classify optical transmission separately from alpha");
    require_contains(
        graph, "if (!has_transmission)",
        "forward graph should retain its original shape when no transmission is visible");
    require_contains(graph, "build_forward_pbr_draw_plan(frame_plans)",
                     "forward graph should build one renderer-private draw plan per frame");
    require_contains(graph, "draw_plan.has_transmission()",
                     "forward graph should select staged transmission from the draw plan");
    require_not_contains(graph, "has_transmission_packets",
                         "forward graph should not rescan scene packets for transmission");
    require_not_contains(
        resources, "has_transmission_packets",
        "forward renderer resources should not retain the retired transmission scan");
    require_contains(graph, "graph.add_pass(\"refraction source\"",
                     "forward graph should isolate the post-cloud HDR refraction source");
    require_contains(graph, "graph.add_pass(\"transmission\"",
                     "forward graph should expose a distinct transmission stage boundary");
    require_contains(graph, "graph.add_pass(\"alpha\"",
                     "forward graph should move ordinary alpha after the transmission boundary");
    require_contains(recording, "vulkan::load_store_attachment_ops()",
                     "forward staged recording should preserve color and depth across boundaries");
    require_contains(graph, ".read_write_color(post_scene_color)",
                     "staged continuation passes should synchronize attachment loads explicitly");
    require_contains(
        recording, "vulkan::clear_store_attachment_ops()",
        "forward opaque recording should preserve depth for cloud and transmission reads");
    require_contains(pyramid_header, "minimum_mip_extent = 16U",
                     "HDR refraction radiance should stop at a bounded roughness mip extent");
    require_contains(pyramid_shader, "filter_options.copy_source > 0.0",
                     "pyramid mip zero should faithfully copy linear HDR scene color");
    require_contains(pyramid_shader, "luminance_weight",
                     "HDR refraction radiance should resist bright procedural highlights");
    require_contains(
        pyramid, ".valid = slot.valid",
        "pyramid snapshots should distinguish a bindable image from produced contents");
    require_contains(resources, "existing.resources_created() && existing.pipeline_created()",
                     "refraction pyramid reuse should reject partial initialization");
    require_contains(resources, "PbrSceneBinding::RefractionRadiance",
                     "every forward scene descriptor should declare refraction radiance");
    require_contains(
        resources, "global_.environment.brdf_lut_view",
        "the no-transmission scene descriptor should bind a valid sampled 2D fallback");
    require_contains(
        graph, "refraction_pyramid().snapshot(target.frame_slot)",
        "staged transmission should bind the current frame-slot pyramid before recording");
    require_contains(graph, "radiance.bindable()",
                     "staged transmission should require a bindable current-command pyramid image");
    require_contains(graph, "transmission_scene_material().upload",
                     "staged transmission should upload matching scene uniforms to its descriptor");
    require_contains(
        graph, "MaterialDescriptorWriter(transmission_scene_material().set",
        "staged transmission should bind its pyramid only in the transmission descriptor");
    require_contains(internal_header, "transmission_scene_material",
                     "the pyramid descriptor should have an isolated swapchain-lifetime scene set");
    require_contains(recording, "record_transmission_stage",
                     "forward recording should own a dedicated transmission draw stage");
    require_contains(recording, "transmission_scene_material().material()",
                     "only transmission draws should consume the same-command pyramid descriptor");
    require_contains(draw_plan, "render::MaterialOpticalMode::Transmission",
                     "the draw plan should filter independent transmission packet classification");
    require_contains(draw_plan, "render::MaterialOpticalMode::Opaque",
                     "the draw plan should reject transmissive shadow and ordinary alpha packets");
    require_contains(recording, "draw_plan.indices(route)",
                     "forward recording should consume preclassified route index spans");
    require_contains(recording, "bound_material",
                     "forward recording should suppress redundant consecutive material binds");
    require_not_contains(
        recording, "record_pipeline_draw_packets_3d",
        "forward recording should not repeatedly full-scan packet spans per route");
    require_contains(pyramid_header, "void record_source_copy",
                     "HDR refraction capture should expose a mip-zero copy stage");
    require_contains(pyramid_header, "ColorTargetView source_target",
                     "HDR refraction capture should expose its isolated mip-zero target");
    require_contains(pyramid_header, "void record_remaining_mips",
                     "HDR refraction capture should finish filtering after source composition");
    require_contains(pyramid, "source_copy_pending",
                     "HDR refraction capture should validate per-slot split-recording state");
    require_contains(pyramid, "requires a source copy before filtering",
                     "HDR refraction capture should reject filtering before mip-zero is ready");
    require_contains(graph, "refraction_pyramid().record_source_copy",
                     "forward graph should copy opaque and cloud radiance before source alpha");
    require_contains(graph, "refraction_pyramid().source_target(frame_slot)",
                     "ordinary alpha source composition should target isolated pyramid mip zero");
    require_contains(graph, "refraction_pyramid().record_remaining_mips",
                     "forward graph should generate refraction mips only after source alpha");

    const std::size_t source_pass = graph.find("graph.add_pass(\"refraction source\"");
    const std::size_t source_copy = graph.find("refraction_pyramid().record_source_copy");
    const std::size_t source_alpha = graph.find("refraction_pyramid().source_target(frame_slot)");
    const std::size_t source_filter = graph.find("refraction_pyramid().record_remaining_mips");
    const std::size_t transmission = graph.find("graph.add_pass(\"transmission\"");
    const std::size_t final_alpha = graph.find("graph.add_pass(\"alpha\"");
    require(source_pass != std::string::npos && source_copy != std::string::npos &&
                source_alpha != std::string::npos && source_filter != std::string::npos &&
                transmission != std::string::npos && final_alpha != std::string::npos &&
                source_pass < source_copy && source_copy < source_alpha &&
                source_alpha < source_filter && source_filter < transmission &&
                transmission < final_alpha,
            "forward transmission should compose alpha into isolated radiance before filtering and "
            "final alpha");
}

void test_forward_pbr_renderer_3d_scene_uniforms_pack_view_light_environment_and_display() {
    const cubey::LightPacket3D light{
        .entity = cubey::Entity{.index = 2, .generation = 1},
        .color = {0.6F, 0.7F, 0.8F},
        .intensity = 3.5F,
        .direction = {0.0F, -1.0F, 0.0F},
    };
    std::array<cubey::math::Vec3, 9> diffuse_sh{};
    diffuse_sh[0] = {0.11F, 0.12F, 0.13F};
    diffuse_sh[4] = {0.41F, 0.42F, 0.43F};
    const cubey::render::PbrSceneUniforms uniforms = cubey::forward_pbr_renderer_3d_scene_uniforms({
        .view_projection = cubey::math::Mat4{2.0F},
        .light_view_projection = cubey::math::Mat4{3.0F},
        .camera_position = {1.0F, 2.0F, 3.0F},
        .light = light,
        .environment =
            cubey::scene::Environment3D{
                .ambient_color = {0.2F, 0.3F, 0.4F},
                .ambient_intensity = 2.0F,
                .diffuse_irradiance_sh = diffuse_sh,
                .diffuse_irradiance_sh_enabled = true,
            },
        .environment_intensity = 5.0F,
        .prefiltered_mip_levels = 6,
        .environment_blend = 0.35F,
        .environment_rotation_degrees = 90.0F,
        .debug_view = cubey::render::PbrDebugView::Shadow,
        .backdrop_reflection =
            {
                .radiance = {0.21F, 0.18F, 0.15F},
                .strength = 0.72F,
                .horizon_elevation_sine = 0.16F,
                .horizon_softness = 0.10F,
            },
    });

    require(uniforms.view_projection == cubey::math::Mat4{2.0F},
            "forward PBR scene uniforms should preserve scene view-projection");
    require(uniforms.light_view_projection == cubey::math::Mat4{3.0F},
            "forward PBR scene uniforms should preserve light view-projection");
    require(uniforms.camera_position == cubey::math::Vec4{1.0F, 2.0F, 3.0F, 1.0F},
            "forward PBR scene uniforms should pack camera position");
    require(uniforms.light_color_intensity == cubey::math::Vec4{0.6F, 0.7F, 0.8F, 3.5F},
            "forward PBR scene uniforms should pack light color and intensity");
    require(uniforms.ambient_color_intensity == cubey::math::Vec4{0.4F, 0.6F, 0.8F, 1.0F},
            "forward PBR scene uniforms should pack ambient color times intensity");
    require(uniforms.environment_intensity_mip_count.x == 5.0F,
            "forward PBR scene uniforms should pack environment intensity");
    require(uniforms.environment_intensity_mip_count.y == 6.0F,
            "forward PBR scene uniforms should pack prefiltered mip count");
    require_near(uniforms.environment_intensity_mip_count.z, 0.0F,
                 "forward PBR scene uniforms should pack rotation cosine");
    require_near(uniforms.environment_intensity_mip_count.w, 1.0F,
                 "forward PBR scene uniforms should pack rotation sine");
    require(uniforms.display_transform == cubey::math::Vec4{0.0F, 1.0F, 0.0F, 0.0F},
            "forward PBR scene uniforms should leave display transform neutral");
    require(uniforms.debug_options.x == static_cast<float>(cubey::render::PbrDebugView::Shadow),
            "forward PBR scene uniforms should pack the requested debug view");
    require(uniforms.debug_options.y == 0.0F && uniforms.debug_options.z == 0.0F &&
                uniforms.debug_options.w == 0.0F,
            "forward PBR scene uniforms should keep spare debug options neutral");
    require(uniforms.diffuse_irradiance_sh[0] == cubey::math::Vec4{0.11F, 0.12F, 0.13F, 0.0F},
            "forward PBR scene uniforms should pack diffuse SH coefficient zero");
    require(uniforms.diffuse_irradiance_sh[4] == cubey::math::Vec4{0.41F, 0.42F, 0.43F, 0.0F},
            "forward PBR scene uniforms should pack diffuse SH coefficient four");
    require(uniforms.environment_options.x == 1.0F,
            "forward PBR scene uniforms should enable diffuse SH only when requested");
    require(uniforms.environment_options.y == 0.35F,
            "forward PBR scene uniforms should pack the environment crossfade");
    require(uniforms.backdrop_reflection_radiance_strength ==
                cubey::math::Vec4{0.21F, 0.18F, 0.15F, 0.72F},
            "forward PBR scene uniforms should pack backdrop reflection radiance and strength");
    require(uniforms.backdrop_reflection_horizon == cubey::math::Vec4{0.16F, 0.10F, 0.0F, 0.0F},
            "forward PBR scene uniforms should pack backdrop reflection horizon controls");
}

void test_terrain_backdrop_reflection_uses_product_materials_lighting_and_horizon() {
    cubey::terrain::TerrainBackdropProduct product;
    product.request.visible_inner_radius_m = 3'200.0F;
    product.diagnostics.minimum_height_m = 0.0F;
    product.diagnostics.maximum_height_m = 1'000.0F;
    product.diagnostics.mean_rock = 0.35F;
    product.diagnostics.mean_snow = 0.25F;
    product.diagnostics.mean_vegetation = 0.10F;
    product.diagnostics.mean_moisture = 0.60F;

    cubey::TerrainBackdropRuntimeFrameInfo day;
    day.camera_position = {0.0F, 200.0F, 0.0F};
    day.atmosphere.sun_direction_radius = {0.0F, 1.0F, 0.0F, 0.004F};
    day.lighting.diffuse_irradiance_sh[0] = {0.8F, 0.9F, 1.0F};
    day.lighting.primary_light_direction = {0.0F, 1.0F, 0.0F};
    day.lighting.primary_light_color = {1.0F, 0.95F, 0.85F};
    day.lighting.primary_light_intensity = 2.0F;

    cubey::TerrainBackdropRuntimeFrameInfo night = day;
    night.atmosphere.sun_direction_radius.y = -1.0F;
    night.lighting.diffuse_irradiance_sh[0] = {0.05F, 0.07F, 0.12F};
    night.lighting.primary_light_direction = {0.0F, 0.5F, 0.5F};
    night.lighting.primary_light_color = {0.45F, 0.55F, 0.80F};
    night.lighting.primary_light_intensity = 0.08F;

    const cubey::TerrainBackdropReflection day_reflection =
        cubey::terrain_backdrop_reflection(product, day);
    const cubey::TerrainBackdropReflection night_reflection =
        cubey::terrain_backdrop_reflection(product, night);

    require(day_reflection.strength > 0.0F && day_reflection.strength <= 1.0F,
            "terrain reflection strength should remain normalized");
    require(day_reflection.horizon_elevation_sine > 0.0F &&
                day_reflection.horizon_elevation_sine <= 0.28F,
            "terrain reflection should derive a bounded raised horizon from product relief");
    require(day_reflection.radiance.x > night_reflection.radiance.x &&
                day_reflection.radiance.y > night_reflection.radiance.y &&
                day_reflection.radiance.z > night_reflection.radiance.z,
            "terrain reflection radiance should follow current environment lighting");

    day.reflections_enabled = false;
    const cubey::TerrainBackdropReflection disabled =
        cubey::terrain_backdrop_reflection(product, day);
    require(disabled.strength == 0.0F && disabled.radiance == cubey::math::Vec3{0.0F},
            "disabled terrain reflections should contribute no foreground radiance");
}

void test_ocean_surface_reflection_uses_water_material_lighting_and_horizon() {
    cubey::render::OceanSurfaceConfig config;
    cubey::OceanSurfaceRuntimeFrameInfo day;
    day.camera_position_m = {0.0F, 20.0F, 0.0F};
    day.planet_radius_m = 6'360'000.0F;
    day.water_datum_m = 0.0F;
    day.lighting.diffuse_irradiance_sh[0] = {0.8F, 0.9F, 1.0F};
    day.lighting.primary_light_direction = {0.0F, 1.0F, 0.0F};
    day.lighting.primary_light_color = {1.0F, 0.95F, 0.85F};
    day.lighting.primary_light_intensity = 2.0F;

    cubey::OceanSurfaceRuntimeFrameInfo night = day;
    night.lighting.diffuse_irradiance_sh[0] = {0.03F, 0.04F, 0.08F};
    night.lighting.primary_light_direction = {0.0F, -1.0F, 0.0F};
    night.lighting.primary_light_color = {0.35F, 0.45F, 0.75F};
    night.lighting.primary_light_intensity = 0.08F;

    const cubey::render::OceanSurfaceFrame day_surface =
        cubey::render::ocean_surface_frame_from_camera(config, day.camera_position_m,
                                                       day.planet_radius_m, day.water_datum_m);
    const cubey::render::OceanSurfaceFrame night_surface =
        cubey::render::ocean_surface_frame_from_camera(config, night.camera_position_m,
                                                       night.planet_radius_m, night.water_datum_m);
    const cubey::BackdropReflection day_reflection =
        cubey::ocean_surface_reflection(config, day, day_surface);
    const cubey::BackdropReflection night_reflection =
        cubey::ocean_surface_reflection(config, night, night_surface);

    require(day_reflection.strength > 0.0F && day_reflection.strength <= 1.0F,
            "ocean reflection strength should remain normalized");
    require(day_reflection.horizon_elevation_sine < 0.0F &&
                day_reflection.horizon_elevation_sine >= -0.08F,
            "ocean reflection should derive a bounded horizon depression from camera altitude");
    require(day_reflection.radiance.x > night_reflection.radiance.x &&
                day_reflection.radiance.y > night_reflection.radiance.y &&
                day_reflection.radiance.z > night_reflection.radiance.z,
            "ocean reflection radiance should follow current environment lighting");
}

void test_forward_pbr_renderer_3d_threads_debug_view_into_shader_and_scene_pass() {
    const std::filesystem::path root{CUBEY_SOURCE_DIR};
    const std::string header =
        read_source_file(root / "include/cubey/engine/forward_pbr_renderer_3d.h");
    const std::string graph =
        read_source_file(root / "src/cubey/engine/forward_pbr_renderer_3d_graph.cpp");
    const std::string internal_header =
        read_source_file(root / "src/cubey/engine/forward_pbr_renderer_3d_internal.h");
    const std::string recording =
        read_source_file(root / "src/cubey/engine/forward_pbr_renderer_3d_recording.cpp");
    const std::string vertex_shader =
        read_source_file(root / "shaders/cubey/forward_pbr/forward_pbr.vert");
    const std::string fragment_shader =
        read_source_file(root / "shaders/cubey/forward_pbr/forward_pbr.frag");

    require_contains(header, "render::PbrDebugView debug_view = render::PbrDebugView::Final",
                     "forward PBR settings should expose the PBR debug view");
    require_contains(graph, ".debug_view = settings.debug_view",
                     "forward PBR record path should pack settings debug view into uniforms");
    require_contains(graph, "settings.debug_view",
                     "forward PBR render graph should receive the requested debug view");
    require_contains(graph, "settings.debug_view == render::PbrDebugView::Final",
                     "forward PBR debug views should bypass creative display transforms");
    require_contains(graph, "render::PbrTonemap::Linear",
                     "forward PBR debug views should preserve diagnostic channel values");
    require_contains(internal_header, "render::PbrDebugView debug_view",
                     "forward PBR scene pass should accept the requested debug view");
    require_contains(recording, "debug_view == render::PbrDebugView::Final",
                     "forward PBR scene pass should suppress the skybox for debug views");
    require_contains(vertex_shader, "vec4 debug_options",
                     "forward PBR vertex shader uniform block should match scene uniforms");
    require_contains(fragment_shader, "vec4 debug_options",
                     "forward PBR fragment shader uniform block should match scene uniforms");
    require_contains(fragment_shader, "diffuse_irradiance_sh[9]",
                     "forward PBR fragment shader should receive diffuse SH coefficients");
    require_contains(fragment_shader, "environment_options.x > 0.5",
                     "forward PBR fragment shader should switch diffuse lighting to SH by flag");
    require_contains(fragment_shader, "backdrop_reflection_radiance_strength",
                     "forward PBR fragment shader should receive backdrop reflection radiance");
    require_contains(fragment_shader, "backdrop_coverage",
                     "forward PBR environment sampling should apply backdrop horizon coverage");
    require_contains(fragment_shader, "CUBEY_PBR_DEBUG_ROUGHNESS",
                     "forward PBR fragment shader should expose named debug view constants");
    require_contains(fragment_shader, "cubey_pbr_debug_output",
                     "forward PBR fragment shader should centralize debug output mapping");
}

void test_forward_pbr_renderer_3d_threads_atmosphere_background_path() {
    const std::filesystem::path root{CUBEY_SOURCE_DIR};
    const std::string header =
        read_source_file(root / "include/cubey/engine/forward_pbr_renderer_3d.h");
    const std::string internal_header =
        read_source_file(root / "src/cubey/engine/forward_pbr_renderer_3d_internal.h");
    const std::string resources =
        read_source_file(root / "src/cubey/engine/forward_pbr_renderer_3d_resources.cpp");
    const std::string graph =
        read_source_file(root / "src/cubey/engine/forward_pbr_renderer_3d_graph.cpp");
    const std::string recording =
        read_source_file(root / "src/cubey/engine/forward_pbr_renderer_3d_recording.cpp");
    const std::string cmake = read_source_file(root / "cmake/CubeyShaders.cmake");
    const std::string gltf_assets =
        read_source_file(root / "projects/gltf_viewer/gltf_viewer_assets.cpp");
    const std::string gltf_app =
        read_source_file(root / "projects/gltf_viewer/gltf_viewer_app.cpp");
    const std::string gltf_render =
        read_source_file(root / "projects/gltf_viewer/gltf_viewer_render.cpp");
    const std::string gltf_scene =
        read_source_file(root / "projects/gltf_viewer/gltf_viewer_scene.cpp");
    const std::string fragment_shader =
        read_source_file(root / "shaders/cubey/forward_pbr/forward_pbr.frag");
    const std::string skybox_shader =
        read_source_file(root / "shaders/cubey/forward_pbr/forward_pbr_skybox.frag");

    require_contains(header, "enum class ForwardPbrRenderer3DBackgroundMode",
                     "forward PBR settings should expose selectable background modes");
    require_contains(header, "ForwardPbrRenderer3DGlobalResourcesInfo",
                     "forward PBR global resources should accept optional atmosphere bindings");
    require_contains(header, "environment_textures",
                     "forward PBR globals should accept explicit PBR environment textures");
    require_not_contains(header, "const render::GeneratedPbrEnvironment* environment",
                         "forward PBR globals should not expose a pointer-only environment path");
    require_contains(header, "std::optional<render::AtmosphereEnvironmentFrameUniforms>",
                     "forward PBR settings should carry atmosphere frame uniforms");
    require_contains(internal_header, "render::AtmosphereBackgroundFrame atmosphere_background",
                     "forward PBR internals should own the atmosphere background frame");
    require_contains(internal_header, "render::PbrEnvironmentTextureBindings environment",
                     "forward PBR internals should store resolved environment bindings");
    require_not_contains(internal_header, "environment_source",
                         "forward PBR internals should not retain a generated environment pointer");
    require_contains(resources, "AtmosphereBackgroundFrameMaterialConfig",
                     "forward PBR resources should create atmosphere descriptors when provided");
    require_contains(resources, "AtmosphereBackgroundFramePipelineConfig",
                     "forward PBR resources should create an atmosphere background pipeline");
    require_contains(resources, "validate_pbr_environment_texture_bindings",
                     "forward PBR resources should validate explicit environment bindings");
    require_contains(resources, "global_.environment = info.environment_textures",
                     "forward PBR resources should consume explicit environment bindings directly");
    require_contains(header, "void update_environment",
                     "forward PBR renderer should expose a frame-slot environment handoff");
    require_contains(resources, "PreviousPrefilteredCube",
                     "forward PBR resources should bind the previous environment generation");
    require_contains(fragment_shader, "cubey_pbr_prefiltered_environment",
                     "forward PBR materials should crossfade prefiltered environment generations");
    require_contains(skybox_shader, "previous_environment_cube",
                     "forward PBR skybox should crossfade environment generations coherently");
    require_contains(graph, "global_.atmosphere_background.upload",
                     "forward PBR record path should upload per-frame atmosphere uniforms");
    require_contains(header, "ForwardPbrRenderer3DAtmosphereClouds",
                     "forward PBR settings should accept a shared atmosphere cloud frame");
    require_contains(graph, "declare_surface_product",
                     "forward PBR graph should declare the shared cloud march product");
    require_contains(graph, "declare_surface_composite",
                     "forward PBR graph should composite clouds over the atmosphere scene");
    require_contains(graph, "render_graph.scene_depth",
                     "forward PBR clouds should resolve descriptors against scene depth");
    require_contains(graph, ".read_texture(post_scene_color)",
                     "forward PBR post should consume the cloud-composited scene color");
    require_contains(recording, "ForwardPbrRenderer3DBackgroundMode::Atmosphere",
                     "forward PBR scene pass should branch to the atmosphere background");
    require_contains(cmake, "shaders/cubey/atmosphere/atmosphere.frag",
                     "forward PBR shader package should compile the shared atmosphere shader");
    require_contains(cmake, "cubey_atmosphere_shader_depends",
                     "forward PBR shader package should track shared atmosphere dependencies");
    require_contains(cmake, "forward_pbr_atmosphere_shader_depends",
                     "forward PBR shader package should include atmosphere shader dependencies");
    require_contains(cmake, "atmosphere_reflection_prefilter.frag",
                     "forward PBR shader package should compile the atmosphere probe prefilter");
    require_not_contains(
        cmake, "atmosphere_reflection_irradiance.frag",
        "forward PBR shader package should not compile removed atmosphere irradiance shader");
    require_contains(gltf_assets, "atmosphere_background_atlases_.create",
                     "glTF viewer should create the progressive atmosphere atlas runtime");
    require_contains(gltf_assets, "poll_atmosphere_background_atlases",
                     "glTF viewer should activate prepared atmosphere atlases");
    require_contains(gltf_assets, "create_atmosphere_environment_runtime",
                     "glTF viewer should create the shared atmosphere environment runtime");
    require_contains(gltf_assets, "pbr_environment_bindings",
                     "glTF viewer should feed explicit PBR environment bindings");
    require_contains(gltf_app, "atmosphere_runtime_.advance",
                     "glTF viewer should advance coherent atmosphere and cloud probes together");
    require_not_contains(gltf_app, "clouds().advance",
                         "glTF viewer should not own a separate cloud-probe cadence");
    require_contains(gltf_render, "ForwardPbrRenderer3DBackgroundMode::Atmosphere",
                     "glTF viewer should select the procedural atmosphere background");
    require_contains(gltf_render, "record_atmosphere_environment_if_needed",
                     "glTF viewer should update the atmosphere runtime before PBR recording");
    require_contains(gltf_render, ".atmosphere_clouds = atmosphere_clouds",
                     "glTF viewer should compose shared clouds into the visible atmosphere");
    require_contains(gltf_scene, "atmosphere_runtime_",
                     "glTF viewer should derive atmosphere background uniforms from the runtime");
    require_contains(gltf_scene, ".frame({",
                     "glTF viewer should use the shared atmosphere runtime frame payload");
}

void test_forward_pbr_renderer_3d_skybox_uniforms_pack_inverse_view_camera_environment_and_display() {
    const cubey::render::PbrSkyboxUniforms uniforms =
        cubey::forward_pbr_renderer_3d_skybox_uniforms({
            .view_projection = cubey::math::Mat4{1.0F},
            .camera_position = {4.0F, 5.0F, 6.0F},
            .environment_intensity = 2.25F,
            .environment_blend = 0.4F,
            .environment_rotation_degrees = 180.0F,
        });

    require(uniforms.inverse_view_projection == cubey::math::Mat4{1.0F},
            "forward PBR skybox uniforms should invert the view-projection matrix");
    require(uniforms.camera_position == cubey::math::Vec4{4.0F, 5.0F, 6.0F, 1.0F},
            "forward PBR skybox uniforms should pack camera position");
    require_near(uniforms.environment_rotation_intensity.x, -1.0F,
                 "forward PBR skybox uniforms should pack rotation cosine");
    require_near(uniforms.environment_rotation_intensity.y, 0.0F,
                 "forward PBR skybox uniforms should pack rotation sine");
    require(uniforms.environment_rotation_intensity.z == 2.25F,
            "forward PBR skybox uniforms should pack environment intensity");
    require(uniforms.environment_rotation_intensity.w == 0.4F,
            "forward PBR skybox uniforms should pack the environment crossfade");
    require(uniforms.display_transform == cubey::math::Vec4{0.0F, 1.0F, 0.0F, 0.0F},
            "forward PBR skybox uniforms should leave display transform neutral");
}

void test_forward_pbr_renderer_3d_post_uniforms_pack_display_transform() {
    const cubey::render::PbrPostUniforms unorm = cubey::forward_pbr_renderer_3d_post_uniforms({
        .color_format = VK_FORMAT_R8G8B8A8_UNORM,
        .exposure = 1.25F,
        .tonemap = cubey::render::PbrTonemap::Linear,
    });
    require(unorm.display_transform == cubey::math::Vec4{1.25F, 0.0F, 1.0F, 0.0F},
            "forward PBR post uniforms should request shader-side sRGB for UNORM targets");

    const cubey::render::PbrPostUniforms srgb = cubey::forward_pbr_renderer_3d_post_uniforms({
        .color_format = VK_FORMAT_B8G8R8A8_SRGB,
        .exposure = -0.5F,
        .tonemap = cubey::render::PbrTonemap::Aces,
    });
    require(srgb.display_transform == cubey::math::Vec4{-0.5F, 1.0F, 0.0F, 0.0F},
            "forward PBR post uniforms should leave encoding to sRGB targets");
}
