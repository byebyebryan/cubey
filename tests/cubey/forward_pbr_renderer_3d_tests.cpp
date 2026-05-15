#include "source_file_test_helpers.h"

#include <cubey/engine/forward_pbr_renderer_3d.h>

#include <vulkan/vulkan.h>

#include <cmath>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>

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
    require(config.post_vertex_shader ==
                std::filesystem::path{"build/shaders"} / "forward_pbr_post.vert.spv",
            "forward PBR shader directory helper should fill the post vertex shader path");
    require(config.post_fragment_shader ==
                std::filesystem::path{"build/shaders"} / "forward_pbr_post.frag.spv",
            "forward PBR shader directory helper should fill the post fragment shader path");
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
    const cubey::ForwardPbrRenderer3DFrameRequestInfo info = valid_frame_request_info();
    const cubey::ForwardPbrRenderer3DRenderRequest request =
        cubey::forward_pbr_renderer_3d_render_request(info);

    require(request.target.device == info.device,
            "forward PBR request helper should copy the target device");
    require(request.target.command_buffer_label == info.command_buffer_label,
            "forward PBR request helper should copy the command buffer label");
    require(request.view.scene == info.scene, "forward PBR request helper should copy scene view");
    require(request.view.frame_plan == info.frame_plan,
            "forward PBR request helper should copy frame plan");
    require(request.scene_resources.materials == info.scene_resources.materials,
            "forward PBR request helper should copy resources");
    require_near(request.settings.exposure, 1.25F,
                 "forward PBR request helper should copy display settings");
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
    const std::string importer =
        read_source_file(root / "src/cubey/engine/gltf_scene_importer_materials.cpp");

    require_contains(header, "shadow_depth_fragment_shader",
                     "forward PBR config should expose a mask-capable shadow fragment shader");
    require_contains(header, "std::unique_ptr<Impl> impl_",
                     "forward PBR public header should hide renderer runtime state behind Impl");
    require_not_contains(header, "enum class ForwardPbrPipelineVariant",
                         "forward PBR public header should not expose pipeline variants");
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
    require_contains(resources, "pipeline_variant_slot(ForwardPbrPipelineVariant::MaskShadow)",
                     "forward PBR renderer should own a mask-capable shadow pipeline variant");
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
    require_contains(recording, "VK_CULL_MODE_BACK_BIT",
                     "forward recording should filter single-sided materials by cull policy");
    require_contains(recording, "VK_CULL_MODE_NONE",
                     "forward recording should route double-sided materials to no-cull pipelines");
    require_contains(recording, "render::MaterialAlphaMode::Opaque",
                     "shadow recording should keep a cheap opaque depth path");
    require_contains(recording, "render::MaterialAlphaMode::Mask",
                     "shadow recording should record a mask-aware depth path");
    require_contains(recording, "bind_material_instance",
                     "masked shadow recording should bind material textures and uniforms");
    require_contains(importer, ".alpha_mode = gltf_alpha_mode(source.alpha_mode)",
                     "glTF importer should map source alpha modes into render material policy");
    require_contains(importer,
                     ".cull_mode = source.double_sided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT",
                     "glTF importer should map doubleSided into render culling policy");
}

void test_forward_pbr_renderer_3d_scene_uniforms_pack_view_light_environment_and_display() {
    const cubey::LightPacket3D light{
        .entity = cubey::Entity{.index = 2, .generation = 1},
        .color = {0.6F, 0.7F, 0.8F},
        .intensity = 3.5F,
        .direction = {0.0F, -1.0F, 0.0F},
    };
    const cubey::render::PbrSceneUniforms uniforms = cubey::forward_pbr_renderer_3d_scene_uniforms({
        .view_projection = cubey::math::Mat4{2.0F},
        .light_view_projection = cubey::math::Mat4{3.0F},
        .camera_position = {1.0F, 2.0F, 3.0F},
        .light = light,
        .environment =
            cubey::scene::Environment3D{
                .ambient_color = {0.2F, 0.3F, 0.4F},
                .ambient_intensity = 2.0F,
            },
        .environment_intensity = 5.0F,
        .prefiltered_mip_levels = 6,
        .environment_rotation_degrees = 90.0F,
        .debug_view = cubey::render::PbrDebugView::Shadow,
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
    require_contains(fragment_shader, "CUBEY_PBR_DEBUG_ROUGHNESS",
                     "forward PBR fragment shader should expose named debug view constants");
    require_contains(fragment_shader, "cubey_pbr_debug_output",
                     "forward PBR fragment shader should centralize debug output mapping");
}

void test_forward_pbr_renderer_3d_skybox_uniforms_pack_inverse_view_camera_environment_and_display() {
    const cubey::render::PbrSkyboxUniforms uniforms =
        cubey::forward_pbr_renderer_3d_skybox_uniforms({
            .view_projection = cubey::math::Mat4{1.0F},
            .camera_position = {4.0F, 5.0F, 6.0F},
            .environment_intensity = 2.25F,
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
