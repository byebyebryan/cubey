#include "fluid_25d_recording_app.h"

#include "fluid_25d_commands.h"
#include "fluid_25d_motion_markers.h"
#include "fluid_25d_project_config.h"
#include "fluid_25d_recording.h"

#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/memory_barriers.h>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <future>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace cubey::projects::fluid::fluid_25d {
namespace {

// This application intentionally has no hydraulic compute entry point. Its
// resources do not create solver pipelines; the frame graph is draw-only.
class RecordingApp {
  public:
    explicit RecordingApp(const Fluid25DProjectConfig& config)
        : config_(config), recording_(std::make_shared<Fluid25DRecording>(
                               config.stream_path ? *config.stream_path : *config.recording_path,
                               config.stream_path.has_value())),
          clock_(recording_->times_s()), view_(fluid_25d_presentation_view_from_name(config.view)),
          catchment_view_(fluid_25d_catchment_view_from_name(config.catchment_view)),
          debug_view_(fluid_25d_debug_view_from_name(config.debug_view)) {
        simulation_.grid_width = recording_->grid_width();
        simulation_.grid_height = recording_->grid_height();
        simulation_.cell_size_m = recording_->cell_size_m();
        simulation_.scenario = Fluid25DScenario::DryBed; // Allocation only, never a solver scene.
        simulation_.fixed_delta_seconds = 1.0F;          // Visual marker clock only.
        simulation_.minimum_wet_depth_m = 0.000001F;
        clock_.set_rate(config.recording_speed);
        live_playing_ = !config.common.headless && config.recording_time_seconds == 0.0F;
        follow_latest_ = config.stream_follow_latest;
        if (config.recording_time_seconds > recording_->times_s().back())
            throw std::runtime_error("recording start time exceeds the final saved state");
        clock_.seek(config.recording_time_seconds);
        if (config.common.headless &&
            recording_->times_s()[clock_.frame_index()] != config.recording_time_seconds)
            throw std::runtime_error(
                "headless recording start time must match a saved state exactly");
        clock_.set_paused(config.common.headless || config.recording_time_seconds != 0.0F);
        shown_index_ = clock_.frame_index();
        accept_frame(recording_->frame(shown_index_));
        scenario_.width = simulation_.grid_width;
        scenario_.height = simulation_.grid_height;
        scenario_.cell_size_m = simulation_.cell_size_m;
        scenario_.terrain_height_m.assign(recording_->bed().begin(), recording_->bed().end());
        scenario_.initial_water_depth_m = shown_->depth_m;
        scenario_.source_depth_rate_m_per_s.assign(shown_->depth_m.size(), 0.0F);
        scenario_.sink_depth_rate_m_per_s.assign(shown_->depth_m.size(), 0.0F);
        scenario_.boundary_outflow_face_mask.assign(shown_->depth_m.size(), 0U);
        render_ = config.catchment_render;
        render_.native_recording = true;
        render_.hillside_depth_cues = true;
        render_.terrain_thin_water_composite = true;
        render_.terrain_height_scale = render_.terrain_height_scale.value_or(1.0F);
        render_.quiver_speed_upper_m_per_s = 4.0F;
        if (!render_.terrain_palette_low_m) {
            const auto [lo, hi] =
                std::minmax_element(recording_->bed().begin(), recording_->bed().end());
            render_.terrain_palette_low_m = *lo;
            render_.terrain_palette_high_m = std::max(*lo + 1.0F, *hi);
        }
        configure_camera();
        std::printf("fluid_25d: %s SynxFlow states, %ux%u @ %.3f m, %zu frames, "
                    "%.0f..%.0f s; no hydraulic solver/reset/forcing dispatches\n",
                    config_.stream_path ? "LIVE EXTERNAL" : "RECORDED", simulation_.grid_width,
                    simulation_.grid_height, simulation_.cell_size_m, recording_->frame_count(),
                    recording_->times_s().front(), recording_->times_s().back());
        std::printf(
            "fluid_25d: recording=%s; initial scheduled rain=%.3f mm/h; native boundary=%s; "
            "h/q are held between saved times, velocity=q/h, qz=-native hUy\n",
            recording_->manifest_path().string().c_str(), recording_->rain_rate_mm_per_hour(),
            recording_->protocol().value("boundary", "unspecified").c_str());
    }

    int run() {
        const int status = config_.common.headless ? run_headless() : run_windowed();
        if (shutdown_failure_)
            std::rethrow_exception(shutdown_failure_);
        return stream_error_.empty() ? status : 1;
    }

  private:
    void accept_frame(std::shared_ptr<const Fluid25DRecordedFrame> frame) {
        shown_ = std::move(frame);
        packed_velocity_.resize(shown_->velocity.size());
        for (std::size_t i = 0; i < packed_velocity_.size(); ++i)
            packed_velocity_[i] = {{shown_->velocity[i].x_m_per_s, shown_->velocity[i].y_m_per_s,
                                    shown_->depth_m[i] > 0.0F ? 1.0F : 0.0F, 0.0F}};
        upload_pending_ = true;
    }

    void reset_visual_history() {
        cue_reset_ = quiver_reset_ = marker_reset_ = true;
        reset_on_load_ = true;
        visual_delta_ = marker_accumulator_ = 0.0;
    }

    void poll_frame_load() {
        const auto wanted = clock_.frame_index();
        if (pending_.valid() &&
            pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto loaded =
                pending_.get(); // Integrity failures stop playback, never silently substitute.
            if (pending_index_ == wanted) {
                accept_frame(std::move(loaded));
                shown_index_ = wanted;
                // Loading may finish after pause/end: initialize arrows from
                // the newly shown velocity instead of freezing the prior state.
                if (clock_.paused() || clock_.ended())
                    quiver_reset_ = true;
                if (reset_on_load_) {
                    cue_reset_ = quiver_reset_ = marker_reset_ = true;
                    reset_on_load_ = false;
                }
            }
        }
        if (shown_index_ != wanted && !pending_.valid()) {
            pending_index_ = wanted;
            pending_ = std::async(std::launch::async,
                                  [source = recording_, wanted] { return source->frame(wanted); });
        }
    }

    void poll_stream() {
        if (!config_.stream_path || !stream_error_.empty())
            return;
        if (stream_pending_.valid() &&
            stream_pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                auto next = stream_pending_.get();
                next->validate_successor_of(*recording_);
                if (next->revision() != recording_->revision()) {
                    clock_.extend(next->times_s());
                    recording_ = std::move(next);
                }
            } catch (const std::exception& error) {
                stream_error_ = error.what();
                live_playing_ = false;
                clock_.set_paused(true);
                std::fprintf(stderr, "fluid_25d_stream: VIEWER ERROR: %s\n", stream_error_.c_str());
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (!stream_pending_.valid() && recording_->producer_state() == "running" &&
            now >= next_stream_poll_) {
            next_stream_poll_ = now + std::chrono::milliseconds(100);
            stream_pending_ = std::async(std::launch::async, [path = *config_.stream_path] {
                return std::make_shared<Fluid25DRecording>(path, true);
            });
        }
    }

    void update_view(double wall_delta_s) {
        poll_stream();
        if (config_.stream_path) {
            clock_.set_paused(!live_playing_ || !stream_error_.empty());
            if (live_playing_ && follow_latest_ &&
                clock_.time_s() != recording_->times_s().back()) {
                clock_.seek(recording_->times_s().back());
                clock_.set_paused(false);
                reset_visual_history();
            }
        }
        try {
            poll_frame_load();
        } catch (const std::exception& error) {
            if (!config_.stream_path)
                throw;
            stream_error_ = error.what();
            live_playing_ = false;
            clock_.set_paused(true);
        }
        const double before = clock_.time_s();
        if (shown_index_ == clock_.frame_index())
            clock_.advance(std::min(wall_delta_s, 0.25));
        visual_delta_ = clock_.time_s() - before;
        if (config_.stream_path && live_playing_ && clock_.ended() &&
            recording_->producer_state() == "running")
            clock_.set_paused(false); // Waiting for a growing prefix is not completion.
        try {
            if (stream_error_.empty())
                poll_frame_load();
        } catch (const std::exception& error) {
            if (!config_.stream_path)
                throw;
            stream_error_ = error.what();
            live_playing_ = false;
            clock_.set_paused(true);
        }
        if (shown_index_ != clock_.frame_index())
            visual_delta_ = 0.0;
    }

    void create_resources(vulkan::Device& device, vulkan::GpuRuntime& gpu, std::uint32_t slots) {
        runtime_.attach_gpu_if_needed(gpu);
        resources_.create_global_resources_if_needed(device, runtime_.gpu(), simulation_, scenario_,
                                                     slots, true);
        if (config_.motion_markers)
            markers_.create(device, runtime_.gpu(), simulation_,
                            {&resources_.terrain(), &resources_.depth_a(), &resources_.depth_b(),
                             &resources_.velocity(), &resources_.source_rate(),
                             &resources_.presentation_cue_status()},
                            {-1.0F, -1.0F}, slots, false, Fluid25DMotionMarkerMode::Local);
        graph_.resize(slots);
    }

    void create_render_resources(vulkan::Device& device, render::ColorTargetView target) {
        resources_.create_render_pipelines(device, target.format, VK_FORMAT_D32_SFLOAT,
                                           target.extent);
        if (config_.motion_markers)
            markers_.create_render_pipeline(device, target.format, VK_FORMAT_D32_SFLOAT,
                                            target.extent);
    }

    void configure_camera() {
        const float width =
            static_cast<float>(simulation_.grid_width - 1U) * simulation_.cell_size_m;
        const float height =
            static_cast<float>(simulation_.grid_height - 1U) * simulation_.cell_size_m;
        const float domain = std::max(width, height);
        const auto [lo, hi] =
            std::minmax_element(recording_->bed().begin(), recording_->bed().end());
        const float scale = *render_.terrain_height_scale;
        target_ = {0.0F, 0.5F * (*lo + *hi) * scale, 0.0F};
        float framing = domain;
        if (config_.recording_camera != "overview") {
            // Observation-only normalized positions in the retained mountain.
            // Collection is the already surveyed B20 basin, not an authored drain.
            const float fx =
                config_.recording_camera == "collection" ? 474.0F / 511.0F : 392.0F / 511.0F;
            const float fz =
                config_.recording_camera == "collection" ? 105.0F / 511.0F : 311.0F / 511.0F;
            const auto col = static_cast<std::uint32_t>(
                std::round(fx * static_cast<float>(simulation_.grid_width - 1U)));
            const auto row = static_cast<std::uint32_t>(
                std::round(fz * static_cast<float>(simulation_.grid_height - 1U)));
            target_ = {
                (fx - 0.5F) * width,
                recording_->bed()[static_cast<std::size_t>(row) * simulation_.grid_width + col] *
                    scale,
                (fz - 0.5F) * height};
            framing =
                std::min(domain, config_.recording_camera == "collection" ? 2400.0F : 3200.0F);
        }
        const float maximum_distance = std::max(64.0F, domain * 4.0F);
        const float minimum_distance = std::max(16.0F, framing * 0.30F);
        const float home = render_.home_camera_distance_m.value_or(
            std::max(32.0F, framing * (config_.recording_camera == "overview" ? 1.65F : 1.35F)));
        if (home < minimum_distance || home > maximum_distance)
            throw std::runtime_error("recording camera home distance exceeds orbit limits");
        orbit_.set_distance_limits(minimum_distance, maximum_distance);
        orbit_.set_pitch_limits(-0.38F, 0.38F);
        orbit_.set_home_distance(home);
        orbit_.reset();
        const float far = fluid_25d_catchment_far_plane(
            maximum_distance, {-0.5F * width, *lo * scale, -0.5F * height},
            {0.5F * width, (*hi + 100.0F) * scale, 0.5F * height}, target_);
        camera_.set_projection(std::numbers::pi_v<float> / 3.0F, std::max(8.0F, framing * 0.10F),
                               far);
    }

    Fluid25DRenderCamera camera(VkExtent2D extent) const {
        const auto transform = orbit_camera_transform({.target = target_,
                                                       .distance = orbit_.distance(),
                                                       .yaw = -0.52F + orbit_.yaw(),
                                                       .pitch = -0.92F + orbit_.pitch()});
        return {camera_.view_projection_matrix(transform, static_cast<float>(extent.width) /
                                                              static_cast<float>(extent.height)),
                transform.translation};
    }

    void record(vulkan::Device& device, VkCommandBuffer commands, render::ColorTargetView target,
                render::FrameSlot slot, Fluid25DRenderTargetMode target_mode,
                profiling::ProfileRecorder* profile, std::uint64_t frame_index) {
        if (upload_pending_) {
            resources_.record_recording_upload(commands, slot.index, shown_->depth_m,
                                               packed_velocity_);
            upload_pending_ = false;
            if (config_.stream_path) {
                const double unix_s = std::chrono::duration<double>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count();
                std::printf("fluid_25d_stream_display: session=%s revision=%llu saved_s=%.0f "
                            "producer=%s native_running=%u native_s=%.0f published_unix_s=%.6f "
                            "displayed_unix_s=%.6f "
                            "ready_to_display_s=%.6f hydraulic_dispatches=0\n",
                            recording_->session_id().c_str(),
                            static_cast<unsigned long long>(recording_->revision()), shown_->time_s,
                            recording_->producer_state().c_str(),
                            recording_->native_running() ? 1U : 0U,
                            recording_->latest_native_time_s(), shown_->published_unix_s, unix_s,
                            unix_s - shown_->published_unix_s);
                std::fflush(stdout);
            }
        }
        const bool quiver = view_ == Fluid25DPresentationView::Catchment &&
                            catchment_view_ == Fluid25DCatchmentView::FlowInspection;
        const bool had_marker_reset = marker_reset_;
        if (config_.motion_markers && marker_reset_) {
            markers_.record_reset(commands);
            marker_reset_ = false;
        }
        // Bound visual advection to one physical second per presentation step.
        // It reads imported h/u and changes only cue/marker buffers.
        if (visual_delta_ <= 0.0) {
            record_fluid_25d_recorded_presentation(commands, resources_, simulation_, 0.0F,
                                                   cue_reset_, quiver_reset_, quiver);
        } else {
            const auto steps = static_cast<std::uint32_t>(std::ceil(visual_delta_));
            const float delta = static_cast<float>(visual_delta_ / static_cast<double>(steps));
            for (std::uint32_t i = 0U; i < steps; ++i)
                record_fluid_25d_recorded_presentation(commands, resources_, simulation_, delta,
                                                       cue_reset_, quiver_reset_, quiver);
        }
        if (config_.motion_markers) {
            marker_accumulator_ += visual_delta_;
            const auto steps = static_cast<std::uint32_t>(std::floor(marker_accumulator_));
            for (std::uint32_t i = 0U; i < steps; ++i)
                markers_.record_step(commands, true);
            marker_accumulator_ -= steps;
            if (!had_marker_reset && visual_delta_ > 0.0)
                marker_fraction_ = static_cast<float>(marker_accumulator_);
            else if (had_marker_reset)
                marker_fraction_ = 1.0F;
            vulkan::record_shader_write_barrier(
                commands,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        }
        const auto compiled = build_fluid_25d_frame_graph(
            target, resources_, simulation_, view_, catchment_view_, debug_view_,
            camera(target.extent), target_mode, false, true, hydraulic_reset_never_used_,
            cue_reset_, quiver_reset_, nullptr, slot.index, {}, render_,
            config_.motion_markers ? &markers_ : nullptr, marker_fraction_, config_.motion_markers);
        graph_.record(
            {.device = &device,
             .command_buffer = commands,
             .frame_slot = slot,
             .label = "recorded SynxFlow presentation (draw-only)",
             .command_buffer_mode = render::RenderGraphCommandBufferMode::AlreadyRecording},
            compiled);
        if (profile) {
            const auto metric = [&](const char* name, double value) {
                profile->record_metric(frame_index, "fluid_25d.recording", name, value);
            };
            metric("requested_time_s", clock_.time_s());
            metric("saved_state_time_s", shown_->time_s);
            metric("saved_frame_index", static_cast<double>(shown_index_));
            metric("water_volume_m3", shown_->stats.water_volume_m3);
            metric("max_depth_m", shown_->stats.max_depth_m);
            metric("max_speed_m_per_s", shown_->stats.max_speed_m_per_s);
            const Fluid25DRecordedRainStatus rain = recording_->rain_status_at(shown_->time_s);
            metric("recorded_rain_rate_mm_per_hour", rain.rate_mm_per_hour);
            metric("recorded_cumulative_scheduled_rain_depth_mm",
                   rain.cumulative_scheduled_rain_depth_mm);
            // Stable numeric phase codes: On=0, Tapering=1, Off=2.
            metric("recorded_rain_phase_code", static_cast<double>(rain.phase));
            metric("hydraulic_dispatch_count", 0.0);
            metric("visual_delta_s", visual_delta_);
        }
        visual_delta_ = 0.0;
    }

    template <typename Value>
    void check_buffer(const vulkan::Buffer& buffer, std::span<const Value> expected,
                      const char* name) {
        const auto bytes = runtime_.gpu().readback_buffer(buffer.handle(), buffer.size(), name);
        if (bytes.size() != expected.size_bytes() ||
            std::memcmp(bytes.data(), expected.data(), bytes.size()) != 0)
            throw std::runtime_error(std::string("recording GPU immutability mismatch: ") + name);
    }

    void shutdown() {
        // Preserve a late I/O/validation error, but always release resources
        // while the host device still exists. Report failure after host cleanup.
        try {
            if (config_.recording_gpu_validation) {
                check_buffer<float>(resources_.terrain(), recording_->bed(),
                                    "native numerical bed");
                check_buffer<float>(resources_.depth_a(), shown_->depth_m, "imported depth A");
                check_buffer<float>(resources_.depth_b(), shown_->depth_m, "imported depth B");
                check_buffer<Fluid25DVelocityGpu>(resources_.velocity(), packed_velocity_,
                                                  "derived velocity");
                std::printf(
                    "fluid_25d_recording_upload: PASS bit-exact bed/h/velocity at saved t=%.0f s; "
                    "hydraulic dispatches=0 (upload test, not native conservation oracle)\n",
                    shown_->time_s);
            }
        } catch (...) {
            shutdown_failure_ = std::current_exception();
        }
        try {
            if (pending_.valid())
                static_cast<void>(pending_.get());
        } catch (...) {
            if (!shutdown_failure_)
                shutdown_failure_ = std::current_exception();
        }
        try {
            if (stream_pending_.valid())
                static_cast<void>(stream_pending_.get());
        } catch (...) {
            if (!shutdown_failure_)
                shutdown_failure_ = std::current_exception();
        }
        graph_.clear();
        markers_.destroy();
        resources_.destroy_all_resources();
        runtime_.detach_gpu_if_attached();
    }

    void draw_ui() {
        ImGui::SetNextWindowSize(ImVec2(430.0F, 0.0F), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(config_.stream_path ? "Live external mountain runoff"
                                             : "Recorded mountain runoff")) {
            if (config_.stream_path) {
                ImGui::TextColored(ImVec4(0.25F, 0.85F, 1.0F, 1.0F),
                                   "LIVE EXTERNAL SYNXFLOW - Vulkan viewer only");
                ImGui::Text("Producer: %s | native %.0f s | available %.0f s",
                            recording_->producer_state().c_str(),
                            recording_->latest_native_time_s(), recording_->times_s().back());
                ImGui::Text("Native solver: %s", recording_->native_running()
                                                     ? "COMPUTING"
                                                     : "NOT COMPUTING (import/audit may continue)");
                ImGui::Text("Displayed %.0f s | import lag %.0f physical s", shown_->time_s,
                            recording_->latest_native_time_s() - recording_->times_s().back());
                ImGui::TextWrapped("%s", recording_->producer_message().c_str());
                if (!stream_error_.empty())
                    ImGui::TextWrapped("VIEWER ERROR: %s", stream_error_.c_str());
                if (recording_->producer_state() == "running" && clock_.ended())
                    ImGui::TextUnformatted("WAITING FOR NEXT SNAPSHOT");
                if (ImGui::Checkbox("Follow latest (may skip saved states)", &follow_latest_))
                    reset_visual_history();
                if (ImGui::Button(live_playing_ ? "Pause viewing [Space]"
                                                : "Resume viewing [Space]"))
                    live_playing_ = !live_playing_;
                ImGui::TextWrapped(
                    "Viewing controls never pause computation or change rainfall. Closing this "
                    "window detaches; the launcher owns the finite run.");
            } else {
                ImGui::TextColored(ImVec4(0.25F, 0.85F, 1.0F, 1.0F),
                                   "RECORDED SYNXFLOW - not a live Cubey solver");
            }
            ImGui::Text("Saved state: %.0f min %.0f s / %.0f min",
                        std::floor(shown_->time_s / 60.0), std::fmod(shown_->time_s, 60.0),
                        recording_->protocol().value("duration_s", recording_->times_s().back()) /
                            60.0);
            const Fluid25DRecordedRainStatus rain = recording_->rain_status_at(shown_->time_s);
            ImGui::Text("Scheduled rain source: %s | %.2f mm/h | %.3f mm scheduled cumulative",
                        fluid_25d_recorded_rain_phase_name(rain.phase), rain.rate_mm_per_hour,
                        rain.cumulative_scheduled_rain_depth_mm);
            if (shown_index_ != clock_.frame_index())
                ImGui::Text("Loading requested state %.0f s...", clock_.time_s());
            else
                ImGui::Text("%s | playhead %.1f s | %.1fx viewing speed",
                            clock_.ended()
                                ? (config_.stream_path ? (recording_->producer_state() == "running"
                                                              ? "WAITING FOR NEXT SNAPSHOT"
                                                              : "END OF FINITE SOURCE")
                                                       : "END OF RECORDING")
                            : clock_.paused() ? "PAUSED"
                                              : "PLAYING",
                            clock_.time_s(), clock_.rate());
            ImGui::BeginDisabled(clock_.ended() || config_.stream_path.has_value());
            if (ImGui::Button(clock_.paused() ? "Play [Space]" : "Pause [Space]"))
                clock_.set_paused(!clock_.paused());
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Restart [R]")) {
                clock_.restart();
                live_playing_ = false;
                follow_latest_ = false;
                reset_visual_history();
            }
            ImGui::SameLine();
            if (ImGui::Button("<")) {
                clock_.previous();
                live_playing_ = false;
                follow_latest_ = false;
                reset_visual_history();
            }
            ImGui::SameLine();
            if (ImGui::Button(">")) {
                clock_.next();
                live_playing_ = false;
                follow_latest_ = false;
                reset_visual_history();
            }
            float time = static_cast<float>(clock_.time_s() / 60.0);
            if (ImGui::SliderFloat("Seek (minutes)", &time,
                                   static_cast<float>(recording_->times_s().front() / 60.0),
                                   static_cast<float>(recording_->times_s().back() / 60.0),
                                   "%.1f")) {
                clock_.seek(static_cast<double>(time) * 60.0);
                live_playing_ = false;
                follow_latest_ = false;
                reset_visual_history();
            }
            double taper_start_s = -1.0;
            double rain_zero_s = -1.0;
            const auto rain_history = recording_->rainfall_history();
            for (std::size_t index = 0U; index + 1U < rain_history.size(); ++index) {
                const auto& start = rain_history[index];
                const auto& end = rain_history[index + 1U];
                if (taper_start_s < 0.0 && start.rate_m_per_s > end.rate_m_per_s &&
                    start.rate_m_per_s > 0.0) {
                    taper_start_s = start.time_s;
                }
                if (rain_zero_s < 0.0 && start.rate_m_per_s > 0.0 && end.rate_m_per_s == 0.0) {
                    rain_zero_s = end.time_s;
                }
            }
            const auto seek_to_recorded_time = [this](const char* label, double seek_time_s) {
                if (ImGui::Button(label)) {
                    clock_.seek(seek_time_s);
                    live_playing_ = false;
                    follow_latest_ = false;
                    reset_visual_history();
                }
            };
            if (taper_start_s >= 0.0) {
                seek_to_recorded_time("Rain taper start", taper_start_s);
                ImGui::SameLine();
            }
            if (rain_zero_s >= 0.0) {
                seek_to_recorded_time("Rain zero", rain_zero_s);
                ImGui::SameLine();
                seek_to_recorded_time("Zero +30m",
                                      std::min(rain_zero_s + 1800.0, recording_->times_s().back()));
                ImGui::SameLine();
                seek_to_recorded_time("Zero +1h",
                                      std::min(rain_zero_s + 3600.0, recording_->times_s().back()));
                ImGui::SameLine();
            }
            seek_to_recorded_time("End", recording_->times_s().back());
            float rate = static_cast<float>(clock_.rate());
            if (ImGui::SliderFloat("Playback x", &rate, 0.125F, 300.0F, "%.1fx",
                                   ImGuiSliderFlags_Logarithmic))
                clock_.set_rate(rate);
            ImGui::TextWrapped(
                "Fields are held between saved states (%.0f s here). Seeking pauses and "
                "clears visual history. End holds the final state; restart is explicit.",
                recording_->protocol().value("output_interval_s", 0.0));
            ImGui::Separator();
            for (const char* name : {"overview", "runoff", "collection"}) {
                if (ImGui::RadioButton(name, config_.recording_camera == name)) {
                    config_.recording_camera = name;
                    configure_camera();
                }
                ImGui::SameLine();
            }
            ImGui::NewLine();
            int mode = static_cast<int>(catchment_view_);
            if (ImGui::Combo("3D reading", &mode,
                             "Composite\0Water isolation\0Flow inspection\0")) {
                catchment_view_ = static_cast<Fluid25DCatchmentView>(mode);
                view_ = Fluid25DPresentationView::Catchment;
                quiver_reset_ = true;
            }
            bool diagnostics = view_ == Fluid25DPresentationView::Diagnostics;
            if (ImGui::Checkbox("2D diagnostics [A]", &diagnostics))
                view_ = diagnostics ? Fluid25DPresentationView::Diagnostics
                                    : Fluid25DPresentationView::Catchment;
            if (diagnostics) {
                int debug = static_cast<int>(debug_view_);
                if (ImGui::Combo("Map [D]", &debug,
                                 "Terrain\0Depth\0Surface\0Speed\0Direction\0Wet/dry\0"))
                    debug_view_ = static_cast<Fluid25DDebugView>(debug);
            }
            ImGui::Text("Depth color: log scale 0.01 / 0.1 / 1 / 10 m");
            if (diagnostics)
                ImGui::Text("2D speed: 0..15 m/s (saturates above 15)");
            ImGui::TextWrapped(
                "Composite attenuates thin rain film. Water isolation shows all wet cells. Arrows: "
                "fixed grid, smoothed neighborhood velocity; 0.025..4 m/s (saturates above 4).");
            ImGui::Text("Storage %.3f million m3 | deepest %.3f m",
                        shown_->stats.water_volume_m3 / 1.0e6, shown_->stats.max_depth_m);
            ImGui::Text("Maximum saved speed %.3f m/s | grid %.0f m",
                        shown_->stats.max_speed_m_per_s, simulation_.cell_size_m);
            ImGui::Text("Peak scheduled rain %.2f mm/h | vertical scale %.2fx",
                        recording_->rain_rate_mm_per_hour(), *render_.terrain_height_scale);
            ImGui::TextWrapped("Source is rainfall over the whole map. Native 'fall' perimeter "
                               "lets runoff leave; there is no point source/drain. Terrain is "
                               "unchanged. No native face ledger or Cubey CFL/oracle claim.");
            if (config_.motion_markers)
                ImGui::TextWrapped("Dots/trails are local visual cues advected in held saved "
                                   "velocities, not native simulated parcels or conserved dye.");
            ImGui::Text("Drag to orbit; scroll to zoom; Esc closes.");
        }
        ImGui::End();
    }

    int run_windowed() {
        host::WindowedAppCallbacks callbacks;
        callbacks.create_global_resources = [this](host::WindowedAppContext& context) {
            create_resources(context.device(), context.gpu(), context.frame_slot_count());
        };
        callbacks.create_swapchain_resources = [this](host::WindowedAppContext& context) {
            create_render_resources(
                context.device(), render::ColorTargetView{.extent = context.swapchain().extent(),
                                                          .format = context.swapchain().format()});
            graph_.clear();
            graph_.resize(context.frame_slot_count());
        };
        callbacks.destroy_swapchain_resources = [this](host::WindowedAppContext&) {
            graph_.clear();
            markers_.destroy_render_pipeline();
            resources_.destroy_swapchain_resources();
        };
        callbacks.draw_ui = [this](host::WindowedAppContext&) { draw_ui(); };
        callbacks.update = [this](host::WindowedAppContext& context, const FrameTiming& timing) {
            const auto input = context.filtered_input();
            orbit_.update_pointer_input(input, timing.delta_seconds);
            if (input.key_pressed(input::Key::Space)) {
                if (config_.stream_path)
                    live_playing_ = !live_playing_;
                else if (!clock_.ended())
                    clock_.set_paused(!clock_.paused());
            }
            if (input.key_pressed(input::Key::R)) {
                clock_.restart();
                live_playing_ = false;
                follow_latest_ = false;
                reset_visual_history();
            }
            if (input.key_pressed(input::Key::A)) {
                view_ = view_ == Fluid25DPresentationView::Diagnostics
                            ? Fluid25DPresentationView::Catchment
                            : Fluid25DPresentationView::Diagnostics;
                quiver_reset_ = true;
            }
            if (input.key_pressed(input::Key::D))
                debug_view_ =
                    static_cast<Fluid25DDebugView>((static_cast<int>(debug_view_) + 1) % 6);
            update_view(timing.delta_seconds);
        };
        callbacks.record_frame = [this](host::WindowedAppContext& context,
                                        const host::WindowedRenderFrame& frame) {
            const vulkan::CommandRecorder recorder(frame.command_buffer);
            recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
            record(context.device(), frame.command_buffer, frame.color_target, frame.frame_slot,
                   Fluid25DRenderTargetMode::Present, context.profile_recorder(),
                   frame.timing.frame_index);
            recorder.end("recorded SynxFlow window");
        };
        callbacks.shutdown = [this](host::WindowedAppContext&) { shutdown(); };
        return host::run_windowed_app(
            {.run_config = config_.common,
             .app_name = "fluid_25d",
             .ready_status =
                 config_.stream_path
                     ? "rendering live external SynxFlow snapshots (no hydraulic solve)"
                     : "rendering recorded SynxFlow mountain runoff (no hydraulic solve)",
             .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
             .close_on_escape = true},
            std::move(callbacks));
    }

    int run_headless() {
        host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [this](host::HeadlessPngContext& context) {
            create_resources(context.device(), context.gpu(),
                             host::headless_capture_frame_slot_count(config_.common));
            create_render_resources(context.device(), context.render_target());
        };
        callbacks.record_frame = [this](host::HeadlessPngContext& context,
                                        const host::HeadlessCaptureFrame& frame,
                                        VkCommandBuffer commands,
                                        const host::HeadlessRenderTarget& target) {
            // Load by immutable capture index on the GPU-owner callback. The
            // video producer may already be preparing later output frames.
            const bool video = config_.common.capture_mode == CaptureMode::Video;
            const double time = std::min(recording_->times_s().back(),
                                         static_cast<double>(config_.recording_time_seconds) +
                                             (video ? static_cast<double>(frame.index) *
                                                          config_.recording_frame_interval_seconds
                                                    : 0.0));
            const double previous = shown_->time_s;
            clock_.seek(time);
            const auto index = clock_.frame_index();
            if (index != shown_index_) {
                accept_frame(recording_->frame(index));
                shown_index_ = index;
            }
            // A sparse recording can jump far in physical time. Bound only
            // the optional approximate visual history, never the saved fields.
            visual_delta_ = video ? std::clamp(shown_->time_s - previous, 0.0, 60.0) : 0.0;
            record(context.device(), commands, target, frame.frame_slot,
                   Fluid25DRenderTargetMode::ColorAttachment, context.profile_recorder(),
                   frame.index);
            std::printf("fluid_25d_recording_capture: output_frame=%u requested_s=%.0f "
                        "saved_s=%.0f camera=%s hydraulic_dispatches=0\n",
                        frame.index, time, shown_->time_s, config_.recording_camera.c_str());
        };
        callbacks.shutdown = [this](host::HeadlessPngContext&) { shutdown(); };
        host::HeadlessPngHost host(
            {.run_config = config_.common,
             .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT},
            std::move(callbacks));
        return host.run();
    }

    Fluid25DProjectConfig config_;
    std::shared_ptr<Fluid25DRecording> recording_;
    Fluid25DRecordingPlayback clock_;
    std::shared_ptr<const Fluid25DRecordedFrame> shown_;
    std::future<std::shared_ptr<const Fluid25DRecordedFrame>> pending_;
    std::future<std::shared_ptr<Fluid25DRecording>> stream_pending_;
    std::chrono::steady_clock::time_point next_stream_poll_{};
    std::string stream_error_{};
    bool live_playing_ = false, follow_latest_ = false;
    std::exception_ptr shutdown_failure_{};
    std::size_t shown_index_ = 0U, pending_index_ = 0U;
    std::vector<Fluid25DVelocityGpu> packed_velocity_;
    Fluid25DConfig simulation_;
    Fluid25DScenarioData scenario_;
    Fluid25DCatchmentRenderOptions render_;
    ProjectRuntimeAdapter runtime_{1};
    Fluid25DGpuResources resources_;
    Fluid25DMotionMarkers markers_;
    render::RenderGraphFrameExecutor graph_;
    Camera3D camera_;
    OrbitController orbit_;
    math::Vec3 target_{};
    Fluid25DPresentationView view_;
    Fluid25DCatchmentView catchment_view_;
    Fluid25DDebugView debug_view_;
    bool cue_reset_ = true, quiver_reset_ = true, marker_reset_ = true, upload_pending_ = true;
    bool hydraulic_reset_never_used_ = false;
    bool reset_on_load_ = false;
    double visual_delta_ = 0.0, marker_accumulator_ = 0.0;
    float marker_fraction_ = 1.0F;
};
} // namespace

int run_fluid_25d_recording(const Fluid25DProjectConfig& config) {
    RecordingApp app(config);
    return app.run();
}
} // namespace cubey::projects::fluid::fluid_25d
