#include "sdf_config.h"
#include "sdf_field.h"
#include "sdf_renderer.h"
#include "sdf_validation.h"

#include <cubey/engine/project_runtime.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/pass.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/image_transitions.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <imgui.h>
#include <iostream>
#include <limits>

namespace cubey::projects::sdf {
namespace {
class App {
  public:
    explicit App(SdfConfig config)
        : config_(std::move(config)),
          recipe_(config_.recipe_path.empty()
                      ? make_recipe(config_.scene, config_.seed, config_.count)
                      : load_recipe(config_.recipe_path)) {
        if (config_.debug > 4 || config_.shadows > 2 || !std::isfinite(config_.yaw) ||
            !std::isfinite(config_.pitch) || !std::isfinite(config_.distance) ||
            !std::isfinite(config_.target_y) || config_.distance < 0.6F ||
            (config_.supersample != 1 && config_.supersample != 2) ||
            !std::isfinite(config_.exposure) || !std::isfinite(config_.sun_elevation) ||
            !std::isfinite(config_.sun_azimuth) || !std::isfinite(config_.roughness) ||
            config_.roughness < 0.05F || config_.roughness > 1.0F ||
            !std::isfinite(config_.shadow_softness) || config_.shadow_softness < 0 ||
            config_.shadow_softness > 0.3F)
            throw std::invalid_argument("invalid SDF view options");
        validate_timeline(config_.timeline);
        if (config_.timeline.motion == "carve")
            validate_carve_recipe(recipe_);
        if (!std::isfinite(config_.snapshot_time) || config_.snapshot_time < -1)
            throw std::invalid_argument("SDF time must be -1 or a nonnegative logical time");
        orbit_.set_pitch(config_.pitch);
        orbit_.set_home_distance(config_.distance);
        orbit_.set_distance(config_.distance);
        playback_seconds_ = std::max(config_.snapshot_time, 0.0);
        timeline_ = sample_timeline(config_.timeline, recipe_.edits.size(), playback_seconds_,
                                    config_.debug);
        if (!config_.export_recipe_path.empty())
            save_recipe(config_.export_recipe_path, recipe_);
    }
    int run() {
        if (config_.common.headless) {
            host::HeadlessPngHostCallbacks callbacks;
            callbacks.create_resources = [&](host::HeadlessPngContext& ctx) {
                initialize(ctx.device(), ctx.gpu(), ctx.render_target(),
                           host::headless_capture_frame_slot_count(config_.common));
            };
            callbacks.record_frame = [&](host::HeadlessPngContext& ctx,
                                         const host::HeadlessCaptureFrame& frame,
                                         VkCommandBuffer command,
                                         const host::HeadlessRenderTarget& target) {
                const double time =
                    config_.snapshot_time >= 0
                        ? config_.snapshot_time
                        : config_.timeline.duration *
                              static_cast<double>(host::headless_capture_progress(frame));
                const auto sample =
                    sample_timeline(config_.timeline, recipe_.edits.size(), time, config_.debug);
                auto view = config_;
                view.debug = sample.debug;
                renderer_.record(ctx.device(), command, target, frame.frame_slot, false, view,
                                 config_.yaw + orbit_.yaw() + sample.yaw, orbit_.pitch(),
                                 orbit_.distance(), ctx.profile_recorder(), frame.index,
                                 current_cutter(), sample.cutter_opacity);
            };
            callbacks.before_frame = [&](host::HeadlessPngContext& ctx,
                                         const host::HeadlessCaptureFrame& frame) {
                const double time =
                    config_.snapshot_time >= 0
                        ? config_.snapshot_time
                        : config_.timeline.duration *
                              static_cast<double>(host::headless_capture_progress(frame));
                auto span =
                    ctx.profile_recorder()
                        ? ctx.profile_recorder()->cpu_span(frame.index, "sdf.prepare_geometry")
                        : profiling::ScopedCpuProfileSpan{};
                prepare_time(ctx.device(), time);
                if (config_.validate && frame.index + 1 == frame.count &&
                    (config_.timeline.motion == "construction" ||
                     config_.timeline.motion == "presentation" ||
                     config_.timeline.motion == "carve")) {
                    auto prefix = recipe_;
                    prefix.edits.resize(field_->displayed_edit_count());
                    validate_field(ctx.device(), runtime_.gpu(), *field_, prefix);
                }
            };
            callbacks.shutdown = [&](host::HeadlessPngContext&) { shutdown(); };
            return host::HeadlessPngHost(
                       {.run_config = config_.common,
                        .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT},
                       std::move(callbacks))
                .run();
        }
        host::WindowedAppCallbacks callbacks;
        callbacks.create_swapchain_resources = [&](host::WindowedAppContext& ctx) {
            initialize(ctx.device(), ctx.gpu(),
                       render::ColorTargetView{.extent = ctx.swapchain().extent(),
                                               .format = ctx.swapchain().format()},
                       ctx.frame_slot_count());
        };
        callbacks.destroy_swapchain_resources = [&](host::WindowedAppContext&) {
            renderer_.destroy();
        };
        callbacks.update = [&](host::WindowedAppContext& ctx, const FrameTiming& timing) {
            orbit_.update_from_input(ctx.filtered_input(), timing.delta_seconds);
            if (ctx.filtered_input().key_pressed(input::Key::R)) {
                playback_seconds_ = 0;
                orbit_.set_pitch(config_.pitch);
                orbit_.set_distance(config_.distance);
            }
            if (regenerate_requested_) {
                regenerate_requested_ = false;
                try {
                    auto next = make_recipe(config_.scene, config_.seed, config_.count);
                    if (config_.timeline.motion == "carve")
                        validate_carve_recipe(next);
                    recipe_ = std::move(next);
                    config_.recipe_path.clear();
                    active_prefix_ = std::numeric_limits<std::size_t>::max();
                    playback_seconds_ = 0;
                    std::cout << "sdf: regenerated scene=" << config_.scene << " hash=" << std::hex
                              << recipe_hash(recipe_) << std::dec << '\n';
                } catch (const std::invalid_argument& error) {
                    error_ = error.what();
                    if (!orbit_.paused())
                        orbit_.toggle_pause();
                }
            }
            if (config_.timeline.motion != "static" && !orbit_.paused())
                playback_seconds_ =
                    std::min(playback_seconds_ + timing.delta_seconds, config_.timeline.duration);
            try {
                prepare_time(ctx.device(), playback_seconds_);
            } catch (const CapacityExceeded& error) {
                error_ = error.what();
                if (!orbit_.paused())
                    orbit_.toggle_pause();
                active_prefix_ = timeline_.prefix;
                renderer_.update_field(ctx.device(), *field_);
            }
        };
        callbacks.draw_ui = [&](host::WindowedAppContext&) { draw_ui(); };
        callbacks.record_frame = [&](host::WindowedAppContext& ctx,
                                     const host::WindowedRenderFrame& frame) {
            const vulkan::CommandRecorder recorder(frame.command_buffer);
            recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
            auto view = config_;
            view.debug = timeline_.debug;
            renderer_.record(ctx.device(), frame.command_buffer, frame.color_target,
                             frame.frame_slot, true, view,
                             config_.yaw + orbit_.yaw() + timeline_.yaw, orbit_.pitch(),
                             orbit_.distance(), ctx.profile_recorder(), frame.timing.frame_index,
                             current_cutter(), timeline_.cutter_opacity);
            recorder.end("sdf frame");
        };
        callbacks.shutdown = [&](host::WindowedAppContext&) { shutdown(); };
        return host::run_windowed_app(
            {.run_config = config_.common,
             .app_name = "sdf",
             .ready_status = "rendering sparse SDF bricks",
             .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
             .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
             .require_dynamic_rendering = true,
             .close_on_escape = true},
            std::move(callbacks));
    }

  private:
    const BoxEdit* current_cutter() const {
        if (timeline_.cutter_index == 0 || timeline_.cutter_index >= recipe_.edits.size() ||
            field_->displayed_edit_count() != timeline_.prefix)
            return nullptr;
        return &recipe_.edits[timeline_.cutter_index];
    }
    void initialize(vulkan::Device& device, vulkan::GpuRuntime& gpu,
                    const render::ColorTargetView& target, std::uint32_t slots) {
        runtime_.attach_gpu_if_needed(gpu);
        if (!field_) {
            field_ = std::make_unique<Field>(device, runtime_.gpu(), config_.atlas_axis);
            if (config_.validate_batches)
                validate_batches(device, runtime_.gpu(), *field_);
            if (config_.validate_subtractions)
                validate_subtractions(device, runtime_.gpu(), *field_);
            active_prefix_ = timeline_.prefix;
            field_->build(std::span<const BoxEdit>(recipe_.edits).first(active_prefix_));
            if (config_.validate) {
                auto prefix = recipe_;
                prefix.edits.resize(active_prefix_);
                validate_field(device, runtime_.gpu(), *field_, prefix);
            }
            std::cout << "sdf: recipe hash=" << std::hex << recipe_hash(recipe_) << std::dec
                      << '\n';
        }
        renderer_.create(device, target.extent, target.format, slots, *field_, config_);
    }
    void prepare_time(vulkan::Device& device, double time) {
        timeline_ = sample_timeline(config_.timeline, recipe_.edits.size(), time, config_.debug);
        if (timeline_.prefix == active_prefix_)
            return;
        runtime_.gpu().wait_queue_idle("sdf prefix publication");
        field_->build(std::span<const BoxEdit>(recipe_.edits).first(timeline_.prefix));
        renderer_.update_field(device, *field_);
        active_prefix_ = timeline_.prefix;
        error_.clear();
    }
    void draw_ui() {
        ImGui::SetNextWindowSize({340, 0}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("SDF study")) {
            ImGui::Text("%zu / %zu boxes", field_->displayed_edit_count(), recipe_.edits.size());
            ImGui::Text("Recipe: %s", recipe_.generator.c_str());
            if (config_.timeline.motion == "carve")
                ImGui::Text("Cuts: %zu / %zu",
                            field_->displayed_edit_count() > 0 ? field_->displayed_edit_count() - 1
                                                               : 0,
                            recipe_.edits.size() - 1);
            if (ImGui::Button(orbit_.paused() ? "Play" : "Pause"))
                orbit_.toggle_pause();
            ImGui::SameLine();
            if (ImGui::Button("Restart"))
                playback_seconds_ = 0;
            const char* motions[]{"Static", "Orbit", "Construction", "Presentation", "Carve"};
            const std::string motion_names[]{"static", "orbit", "construction", "presentation",
                                             "carve"};
            int motion = 0;
            for (int i = 0; i < 5; ++i)
                if (config_.timeline.motion == motion_names[i])
                    motion = i;
            if (ImGui::Combo("Motion", &motion, motions, 5)) {
                auto next = config_.timeline;
                next.motion = motion_names[motion];
                try {
                    validate_timeline(next);
                    if (next.motion == "carve")
                        validate_carve_recipe(recipe_);
                    config_.timeline = next;
                    playback_seconds_ = 0;
                } catch (const std::invalid_argument& error) {
                    error_ = error.what();
                }
            }
            float progress = static_cast<float>(playback_seconds_ / config_.timeline.duration);
            if (ImGui::SliderFloat("Timeline", &progress, 0, 1, "%.2f"))
                playback_seconds_ = static_cast<double>(progress) * config_.timeline.duration;
            int view = static_cast<int>(config_.debug);
            const char* views[]{"Shaded", "Normals", "Ray distance", "Traversal work",
                                "Brick cells"};
            if (ImGui::Combo("View", &view, views, 5))
                config_.debug = static_cast<std::uint32_t>(view);
            int shadows = static_cast<int>(config_.shadows);
            const char* shadow_names[]{"Off", "Hard", "Soft (4 samples)"};
            if (ImGui::Combo("Shadows", &shadows, shadow_names, 3))
                config_.shadows = static_cast<std::uint32_t>(shadows);
            ImGui::Checkbox("Local ambient occlusion", &config_.ao);
            ImGui::Checkbox("Cutter bounds cue", &config_.cutter_cue);
            ImGui::SliderFloat("Sun elevation", &config_.sun_elevation, 5, 85, "%.0f deg");
            ImGui::SliderFloat("Sun azimuth", &config_.sun_azimuth, -180, 180, "%.0f deg");
            ImGui::SliderFloat("Exposure", &config_.exposure, -3, 3, "%.1f stops");
            ImGui::SliderFloat("Roughness", &config_.roughness, 0.05F, 1);
            ImGui::SliderFloat("Focus height", &config_.target_y, -0.3F, 0.3F);
            ImGui::SliderFloat("Shadow softness", &config_.shadow_softness, 0, 0.15F);
            ImGui::Separator();
            const char* scene_labels[]{"Sticks", "Negative sticks", "Negative sphere",
                                       "Box",    "Rotated box",     "Cut fixture"};
            const std::string scene_names[]{"sticks", "negative-sticks", "negative-sphere",
                                            "box",    "rotated",         "cut"};
            int scene = 0;
            for (int i = 0; i < 6; ++i)
                if (config_.scene == scene_names[i])
                    scene = i;
            if (ImGui::Combo("Generator", &scene, scene_labels, 6))
                config_.scene = scene_names[scene];
            if (!config_.recipe_path.empty())
                ImGui::TextWrapped("Loaded explicit recipe. Generate scene replaces it.");
            ImGui::InputScalar("Seed", ImGuiDataType_U32, &config_.seed);
            const bool negative =
                config_.scene == "negative-sticks" || config_.scene == "negative-sphere";
            ImGui::InputScalar(negative ? "Cutter count" : "Stick count", ImGuiDataType_U32,
                               &config_.count);
            if (ImGui::Button("Generate scene")) {
                config_.count = std::min(config_.count, negative ? 65535U : 65536U);
                regenerate_requested_ = true;
            }
            if (!error_.empty())
                ImGui::TextWrapped("%s", error_.c_str());
            if (ImGui::CollapsingHeader("Field and tracing")) {
                const auto& stats = field_->stats();
                ImGui::Text("Bricks: %u / %u", stats.brick_count, field_->capacity);
                ImGui::Text("Field: %.1f MiB",
                            static_cast<double>(stats.allocation_bytes) / (1024 * 1024));
                ImGui::Text("Last build: %.2f ms", stats.build_ms);
                ImGui::TextWrapped("Four hierarchy stages skip empty cells. Each leaf caches 8x8x8 "
                                   "distance samples. Magenta reports an exhausted ray.");
            }
        }
        ImGui::End();
    }
    void shutdown() {
        renderer_.destroy();
        field_.reset();
        runtime_.detach_gpu_if_attached();
    }
    SdfConfig config_;
    Recipe recipe_;
    ProjectRuntimeAdapter runtime_{1};
    OrbitController orbit_{{.distance = 1.35F, .min_distance = 0.6F, .max_distance = 5.0F}};
    std::unique_ptr<Field> field_;
    Renderer renderer_;
    TimelineSample timeline_;
    std::size_t active_prefix_ = std::numeric_limits<std::size_t>::max();
    double playback_seconds_ = 0;
    bool regenerate_requested_ = false;
    std::string error_;
};
} // namespace
int run_sdf(const SdfConfig& config) {
    return App(config).run();
}
} // namespace cubey::projects::sdf
