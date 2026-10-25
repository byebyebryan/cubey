#include "pbr_materials_app_internal.h"

#include <cubey/render/render_graph.h>
#include <cubey/vulkan/command_recorder.h>

namespace cubey::projects::pbr_materials::detail {

void PbrMaterialsApp::record_workshop_frame(cubey::host::WindowedAppContext& context,
                                            const cubey::host::WindowedRenderFrame& frame) {
    record_frame(context.device(), frame.command_buffer, frame.color_target, frame.frame_slot, true,
                 context.profile_recorder(), frame.timing.frame_index);
}

void PbrMaterialsApp::record_frame(const cubey::vulkan::Device& device, VkCommandBuffer command,
                                   cubey::render::ColorTargetView target,
                                   cubey::render::FrameSlot slot, bool present,
                                   cubey::profiling::ProfileRecorder* profile,
                                   std::uint64_t index) {
    publish_materials(slot);
    if (whole_profiler_) {
        whole_profiler_->collect(slot.index);
        pass_profiler_->collect(slot.index);
        if (profile && index >= slot.count) {
            for (const auto& timing : whole_profiler_->latest_timings())
                profile->record_gpu_span(index - slot.count, timing.label, timing.milliseconds);
            for (const auto& timing : pass_profiler_->latest_timings())
                profile->record_gpu_span(index - slot.count, timing.label, timing.milliseconds);
        }
    }
    if (present)
        vulkan::CommandRecorder(command).begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    if (config_.lighting_cycle != "frozen")
        prepare_environment(device, command, slot, target.extent);
    if (whole_profiler_) {
        whole_profiler_->begin_frame(command, slot.index);
        pass_profiler_->begin_frame(command, slot.index);
    }
    std::optional<vulkan::GpuTimestampScope> whole;
    if (whole_profiler_)
        whole.emplace(&*whole_profiler_, command, slot.index, "material workshop whole scene");
    auto settings = ForwardPbrRenderer3DSettings{.environment_rotation_degrees =
                                                     config_.pbr.environment_rotation_degrees,
                                                 .exposure = config_.pbr.exposure,
                                                 .debug_view = debug_view_};
    if (config_.environment != "studio") {
        forward_pbr_renderer().update_environment_frame(
            device, slot,
            render::pbr_environment_frame_bindings(
                atmosphere_.pbr_environment_bindings(ibl_environment())));
        settings.background_mode = ForwardPbrRenderer3DBackgroundMode::Atmosphere;
        settings.atmosphere_background =
            atmosphere_.frame({.view_rays = view_rays(target.extent)}).background;
        if (config_.environment == "clouds")
            settings.atmosphere_clouds = ForwardPbrRenderer3DAtmosphereClouds{
                .runtime = &atmosphere_.clouds(), .frame = cloud_frame(target.extent)};
    }
    cubey::SceneReadView scene_view = scene().read();
    const cubey::scene::FrameRenderPlan3D frame_plan =
        current_frame_plan(scene_view, target.extent);
    forward_pbr_renderer().record({
        .target =
            {
                .device = &device,
                .command_buffer = command,
                .color_target = target,
                .frame_slot = slot,
                .color_initial_state =
                    present ? cubey::render::render_graph_undefined_texture_state()
                            : cubey::render::render_graph_color_attachment_texture_state(),
                .color_final_state =
                    present ? cubey::render::render_graph_present_texture_state()
                            : cubey::render::render_graph_color_attachment_texture_state(),
                .command_buffer_label = "vkEndCommandBuffer pbr_materials",
                .command_buffer_mode =
                    cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
                .profiler = pass_profiler_ ? &*pass_profiler_ : nullptr,
            },
        .view =
            {
                .scene = &scene_view,
                .frame_plan = &frame_plan,
                .camera_entity = camera_entity_,
                .light_entity = light_entity_,
                .fallback_light = fallback_light_packet(),
            },
        .scene_resources =
            {
                .meshes = &meshes_,
                .materials = &materials_.at(slot.index),
            },
        .settings = settings,
    });
    whole.reset();
    if (present)
        vulkan::CommandRecorder(command).end("pbr_materials frame");
}

} // namespace cubey::projects::pbr_materials::detail
