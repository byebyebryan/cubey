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
    profile_ = profile;
    publish_materials(slot);
    if (temporal_material_revision_ != revision_) {
        ++temporal_content_generation_;
        temporal_material_revision_ = revision_;
    }
    if (whole_profiler_) {
        collect_profile_slot(slot.index);
    }
    if (present)
        vulkan::CommandRecorder(command).begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    if (config_.lighting_cycle != "frozen")
        prepare_environment(device, command, slot, target.extent);
    if (whole_profiler_) {
        whole_profiler_->begin_frame(command, slot.index);
        pass_profiler_->begin_frame(command, slot.index);
        pending_profile_frames_.at(slot.index) = index;
    }
    std::optional<vulkan::GpuTimestampScope> whole;
    if (whole_profiler_)
        whole.emplace(&*whole_profiler_, command, slot.index, "material workshop whole scene");
    auto settings = ForwardPbrRenderer3DSettings{.environment_rotation_degrees =
                                                     config_.pbr.environment_rotation_degrees,
                                                 .exposure = config_.pbr.exposure,
                                                 .debug_view = debug_view_};
    settings.transmission_source = config_.transmission_source == "environment"
                                       ? ForwardPbrTransmissionSource::Environment
                                       : ForwardPbrTransmissionSource::Screen;
    settings.anti_aliasing = host::native_taa_requested(config_.common)
                                 ? render::AntiAliasingMode::Taa
                                 : render::AntiAliasingMode::Off;
    settings.temporal_appearance_static = config_.lighting_cycle == "frozen";
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
                .temporal_content_generation = temporal_content_generation_,
            },
        .scene_resources =
            {
                .meshes = &meshes_,
                .materials = &materials_.at(slot.index),
            },
        .settings = settings,
        .scene_readback = hdr_readback_ ? std::optional{ForwardPbrRenderer3DSceneReadback{
                                              .buffer = hdr_readback_->handle(),
                                              .byte_size = hdr_readback_->size()}}
                                        : std::nullopt,
    });
    if (profile) {
        if (config_.taa_moving_fixture) {
            profile->record_metric(index, "taa.fixture", "seconds", fixture_seconds_);
            profile->record_metric(index, "taa.fixture", "world_x", fixture_position_.x);
            profile->record_metric(index, "taa.fixture", "world_z", fixture_position_.z);
        }
        const auto status = forward_pbr_renderer().temporal_status();
        profile->record_metric(index, "aa", "requested", static_cast<unsigned>(status.requested));
        profile->record_metric(index, "aa", "effective", static_cast<unsigned>(status.effective));
        profile->record_metric(index, "aa", "bypass_reason",
                               static_cast<unsigned>(status.bypass_reason));
        if (status.effective == render::AntiAliasingMode::Taa) {
            profile->record_metric(index, "taa", "history_age", status.history_age);
            profile->record_metric(index, "taa", "reset_reason",
                                   static_cast<unsigned>(status.reset_reason));
            profile->record_metric(index, "taa", "jitter_x", status.sample_offset_pixels.x);
            profile->record_metric(index, "taa", "jitter_y", status.sample_offset_pixels.y);
            profile->record_metric(
                index, "taa", "persistent_requested_bytes",
                static_cast<double>(status.resources.persistent_requested_bytes));
            profile->record_metric(
                index, "taa", "per_slot_transient_requested_bytes",
                static_cast<double>(status.resources.per_slot_transient_requested_bytes));
            profile->record_metric(
                index, "taa", "persistent_image_allocation_bytes",
                static_cast<double>(status.resources.persistent_image_allocation_bytes));
            profile->record_metric(
                index, "taa", "per_slot_transient_image_allocation_bytes",
                static_cast<double>(status.resources.per_slot_transient_image_allocation_bytes));
            profile->record_metric(index, "taa", "uniform_allocation_bytes",
                                   static_cast<double>(status.resources.uniform_allocation_bytes));
        }
    }
    whole.reset();
    if (present)
        vulkan::CommandRecorder(command).end("pbr_materials frame");
}

void PbrMaterialsApp::collect_profile_slot(std::uint32_t slot) {
    if (!whole_profiler_ || !accepted_profile_frames_.at(slot))
        return;
    for (auto* profiler : {&*whole_profiler_, &*pass_profiler_}) {
        profiler->collect(slot);
        if (profile_)
            for (const auto& timing : profiler->latest_timings())
                profile_->record_gpu_span(*accepted_profile_frames_[slot], timing.label,
                                          timing.milliseconds);
    }
    accepted_profile_frames_[slot].reset();
}
void PbrMaterialsApp::frame_submitted(render::FrameSlot slot, std::uint64_t index,
                                      vulkan::GpuSubmissionTicket ticket) {
    forward_pbr_renderer().frame_submitted(ticket);
    if (!aa_reported_) {
        const auto aa = forward_pbr_renderer().temporal_status();
        host::report_anti_aliasing(config_.common, aa.effective == render::AntiAliasingMode::Taa,
                                   forward_pbr_renderer_3d_taa_bypass_reason_name(aa.bypass_reason));
        aa_reported_ = true;
    }
    if (whole_profiler_ && pending_profile_frames_.at(slot.index)) {
        if (*pending_profile_frames_[slot.index] != index)
            throw std::logic_error("PBR profiler submission did not match its candidate");
        accepted_profile_frames_[slot.index] = pending_profile_frames_[slot.index];
        pending_profile_frames_[slot.index].reset();
    }
}
void PbrMaterialsApp::frame_discarded(render::FrameSlot slot) {
    if (forward_pbr_renderer_)
        forward_pbr_renderer().frame_discarded();
    if (slot.index < pending_profile_frames_.size())
        pending_profile_frames_[slot.index].reset();
}

} // namespace cubey::projects::pbr_materials::detail
