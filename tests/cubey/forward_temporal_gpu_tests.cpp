#include <cubey/engine/engine.h>
#include <cubey/engine/forward_pbr_renderer_3d.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/material_specimen_mesh.h>
#include <cubey/render/primitive_resource.h>
#include <cubey/scene/scene_builder.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/immediate_commands.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace cubey;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int main() {
    try {
        vulkan::Instance instance({.application_name = "forward temporal retry and admission",
                                   .required_extensions = {},
                                   .validation = true,
                                   .require_validation = true});
        vulkan::Device device(instance,
                              {.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
                               .require_present = false,
                               .require_dynamic_rendering = true});
        vulkan::SubmissionCoordinator submission(device);
        vulkan::GpuRuntime gpu(device, submission);
        Engine engine({.worker_count = 1});
        render::MeshResourceTable<render::Mesh> meshes;
        const auto mesh = render::create_primitive_mesh_resource(
            engine.render_resources(), meshes, gpu, "temporal.sphere",
            render::make_material_sphere_mesh(.25F, 8, 16));
        auto defaults = render::create_pbr_default_texture_set(device, gpu);
        auto environment = render::create_generated_pbr_environment(device, gpu,
                                                                    {.irradiance_extent = 4,
                                                                     .prefiltered_extent = 8,
                                                                     .prefiltered_mip_levels = 3,
                                                                     .brdf_lut_extent = 8});
        const render::PbrMaterialDefinition definition{};
        const auto material =
            engine.render_resources().create_material(render::pbr_material_info(definition));
        render::PbrMaterialTable materials;
        materials.initialize(device, {.block_capacity = 1});
        static_cast<void>(materials.emplace(material, definition,
                                            render::pbr_default_sampled_image_bindings(defaults)));
        Scene& scene = engine.create_scene();
        Entity camera{}, object{};
        {
            auto setup = scene.begin_transaction();
            object = scene::create_renderable_entity_3d(
                setup, {.mesh = mesh,
                        .material = material,
                        .local_bounds = {.half_extent = {.25F, .25F, .25F}},
                        .cast_shadows = false,
                        .receive_shadows = false});
            camera = scene::create_camera_entity_3d(setup, orbit_camera_transform({.distance = 2}),
                                                    Camera3D{});
            auto light =
                directional_light_3d(glm::normalize(math::Vec3{.3F, .7F, 1.0F}), {1, 1, 1}, 2);
            light.casts_shadows = false;
            static_cast<void>(scene::create_directional_light_entity_3d(setup, light));
            setup.commit();
        }
        constexpr VkExtent2D extent{96, 64};
        const auto target_config =
            vulkan::color_render_target_image_config(extent, VK_FORMAT_R8G8B8A8_UNORM);
        vulkan::Image color(device, target_config);
        const auto target =
            render::color_target_view(extent, color.format(), color.handle(), color.view());
        const std::filesystem::path shaders = CUBEY_FORWARD_TEMPORAL_TEST_SHADER_DIR;
        const auto temp =
            std::filesystem::temp_directory_path() /
            ("cubey-temporal-retry-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(temp);
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{temp};
        auto config = forward_pbr_renderer_3d_config_from_shader_directory(shaders);
        config.shadow_extent = 32;
        config.temporal_resolve_shader = temp / "late-resolve.spv";
        ForwardPbrRenderer3D renderer(config);
        renderer.create_global_resources(device, environment, 1);
        renderer.create_swapchain_resources(
            device, {.extent = extent, .color_format = color.format(), .materials = &materials});
        bool target_initialized = false;
        const auto record = [&](bool submit, render::AntiAliasingMode mode,
                                bool missing_identity = false, bool masked = false,
                                bool diagnostic = false) {
            auto read = scene.read();
            const scene::View3D view{.camera_entity = camera,
                                     .width = extent.width,
                                     .height = extent.height,
                                     .culling_enabled = false};
            auto plan = scene::build_render_frame_plan_3d(view, read, engine.render_resources());
            if (missing_identity)
                plan.draw_packets.front().entity = {};
            if (masked)
                plan.draw_packets.front().material_info.alpha_mode =
                    render::MaterialAlphaMode::Mask;
            const scene::FrameRenderPlan3D frame(
                {{.label = "shadow",
                  .kind = scene::RenderPassKind3D::DepthOnly,
                  .frame_plan = plan},
                 {.label = "scene", .kind = scene::RenderPassKind3D::Color, .frame_plan = plan}});
            vulkan::ImmediateCommands commands(device, submission);
            try {
                renderer.record(
                    {.target = {.device = &device,
                                .command_buffer = commands.command_buffer(),
                                .color_target = target,
                                .frame_slot = {0, 1},
                                .color_initial_state =
                                    target_initialized
                                        ? render::render_graph_color_attachment_texture_state()
                                        : render::render_graph_undefined_texture_state(),
                                .color_final_state =
                                    render::render_graph_color_attachment_texture_state(),
                                .command_buffer_mode =
                                    render::RenderGraphCommandBufferMode::AlreadyRecording},
                     .view = {.scene = &read, .frame_plan = &frame, .camera_entity = camera},
                     .scene_resources = {.meshes = &meshes, .materials = &materials},
                     .settings = {.debug_view = diagnostic ? render::PbrDebugView::BaseColor
                                                           : render::PbrDebugView::Final,
                                  .anti_aliasing = mode}});
                if (submit) {
                    commands.submit_and_wait();
                    renderer.frame_submitted(submission.last_submitted());
                    target_initialized = true;
                } else
                    renderer.frame_discarded();
            } catch (...) {
                renderer.frame_discarded();
                throw;
            }
            return renderer.temporal_status();
        };
        bool failed = false;
        try {
            static_cast<void>(record(false, render::AntiAliasingMode::Taa));
        } catch (const std::exception&) {
            failed = true;
        }
        require(failed, "missing shader did not fail lazy creation");
        std::filesystem::copy_file(shaders / "resolve.comp.spv", config.temporal_resolve_shader);
        static_cast<void>(record(false, render::AntiAliasingMode::Taa));
        auto status = record(true, render::AntiAliasingMode::Taa);
        require(status.effective == render::AntiAliasingMode::Taa && status.history_age == 0,
                "failed creation or discarded record prevented fresh retry");
        {
            auto edit = scene.create_edit_queue();
            edit.transforms3d().set_local_transform(object, {.translation = {.05F, 0, 0}});
            scene.commit(edit);
        }
        status = record(true, render::AntiAliasingMode::Taa);
        require(status.history_age == 1, "rigid movement reset accepted history");
        static_cast<void>(record(false, render::AntiAliasingMode::Taa));
        status = record(true, render::AntiAliasingMode::Taa);
        require(status.history_age == 0 &&
                    status.reset_reason == render::TemporalResetReason::DiscardedFrame,
                "discard after accepted history did not reseed");
        status = record(true, render::AntiAliasingMode::Taa, true);
        require(status.effective == render::AntiAliasingMode::Off &&
                    status.bypass_reason == ForwardPbrRenderer3DTaaBypassReason::MissingIdentity,
                "missing stable identity was admitted to rigid history");
        status = record(true, render::AntiAliasingMode::Taa, false, true);
        require(status.effective == render::AntiAliasingMode::Off &&
                    status.bypass_reason == ForwardPbrRenderer3DTaaBypassReason::NonOpaqueMaterial,
                "masked surface was admitted");
        status = record(true, render::AntiAliasingMode::Taa, false, false, true);
        require(status.bypass_reason == ForwardPbrRenderer3DTaaBypassReason::DiagnosticView,
                "raw diagnostic was jittered");
        static_cast<void>(record(true, render::AntiAliasingMode::Off));
        status = record(true, render::AntiAliasingMode::Taa);
        require(status.history_age == 0, "return from Off reused stale history");
        renderer.destroy_all_resources();

        // Read the actual engine motion, rather than accepting a movie or an
        // advancing history age as proof that rigid vectors reached the resolve.
        // This only replaces the test's private resolve file; production shaders
        // and the normal resolve/reference tests remain unchanged.
        std::filesystem::copy_file(shaders / "temporal_motion_probe.comp.spv",
                                   config.temporal_resolve_shader,
                                   std::filesystem::copy_options::overwrite_existing);
        renderer.create_global_resources(device, environment, 1);
        renderer.create_swapchain_resources(
            device, {.extent = extent, .color_format = color.format(), .materials = &materials});
        const auto move_object = [&](float x) {
            auto edit = scene.create_edit_queue();
            edit.transforms3d().set_local_transform(object, {.translation = {x, 0, 0}});
            scene.commit(edit);
        };
        const auto center_pixel = [&] {
            vulkan::Buffer readback(device, vulkan::readback_buffer_config(4));
            vulkan::ImmediateCommands commands(device, submission);
            const auto command = commands.command_buffer();
            vulkan::transition_image_layout(
                command, vulkan::finish_color_attachment_for_readback_transition(color.handle()));
            const VkBufferImageCopy copy{
                .bufferOffset = 0,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                .imageOffset = {static_cast<std::int32_t>(extent.width / 2),
                                static_cast<std::int32_t>(extent.height / 2), 0},
                .imageExtent = {1, 1, 1}};
            vkCmdCopyImageToBuffer(command, color.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   readback.handle(), 1, &copy);
            vulkan::transition_image_layout(
                command, {.image = color.handle(),
                          .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                          .old_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          .new_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          .src_access_mask = VK_ACCESS_TRANSFER_READ_BIT,
                          .dst_access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                          .src_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
                          .dst_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT});
            commands.submit_and_wait();
            std::array<std::uint8_t, 4> pixel{};
            readback.download(pixel.data(), pixel.size());
            return pixel;
        };
        move_object(0);
        static_cast<void>(record(true, render::AntiAliasingMode::Taa));
        static_cast<void>(record(true, render::AntiAliasingMode::Taa));
        const auto stationary = center_pixel();
        move_object(.05F);
        status = record(true, render::AntiAliasingMode::Taa);
        const auto moving = center_pixel();
        require(stationary[0] <= 1 && moving[0] > stationary[0] + 8 && moving[1] <= 1 &&
                    status.history_age == 2,
                "actual rigid overlay did not supply signed object motion to the resolve");
        std::cout << "Rigid motion probe: stationary red=" << unsigned(stationary[0])
                  << ", right-moving red=" << unsigned(moving[0]) << '\n';
        renderer.destroy_all_resources();
        std::cout << "PASS: forward lazy-create retry, unsubmitted depth/history rollback, rigid "
                     "movement and signed GPU motion, "
                     "identity/mask/debug bypass and Off return\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
