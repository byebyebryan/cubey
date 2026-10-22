#include "sdf_config.h"
#include "sdf_field.h"
#include "sdf_history.h"
#include "sdf_material.h"
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
          recipe_(!config_.authored_path.empty() ? Recipe{}
                  : config_.recipe_path.empty()
                      ? make_recipe(config_.scene, config_.seed, config_.count,
                                    config_.cutter_width, config_.cutter_mode)
                      : load_recipe(config_.recipe_path)),
          authored_(
              config_.authored_path.empty()
                  ? std::nullopt
                  : std::optional<AuthoredScene>{load_authored_scene(config_.authored_path)}) {
        if (authored_)
            validate_authored_motion(*authored_);
        if (!authored_ && !config_.export_authored_path.empty())
            throw std::invalid_argument("--export-authored requires --authored-scene");
        if (config_.validate_history && (!authored_ || authored_->history.empty()))
            throw std::invalid_argument("--validate-history requires a saved authored history");
        if (config_.debug > 8 || config_.shadows > 2 || !std::isfinite(config_.yaw) ||
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
        sample_time(playback_seconds_);
        if (!config_.export_recipe_path.empty())
            save_recipe(config_.export_recipe_path, recipe_);
        if (!config_.export_authored_path.empty())
            save_authored_scene(config_.export_authored_path, *authored_);
    }
    int run() {
        if (config_.common.headless) {
            host::HeadlessPngHostCallbacks callbacks;
            callbacks.create_resources = [&](host::HeadlessPngContext& ctx) {
                initialize(ctx.device(), ctx.gpu(), ctx.render_target(),
                           host::headless_capture_frame_slot_count(config_.common));
            };
            callbacks.record_frame =
                [&](host::HeadlessPngContext& ctx, const host::HeadlessCaptureFrame& frame,
                    VkCommandBuffer command, const host::HeadlessRenderTarget& target) {
                    auto view = config_;
                    view.debug = timeline_.debug;
                    renderer_.record(ctx.device(), command, target, frame.frame_slot, false, view,
                                     config_.yaw + orbit_.yaw() + timeline_.yaw, orbit_.pitch(),
                                     orbit_.distance(), ctx.profile_recorder(), frame.index,
                                     current_cutter(), timeline_.cutter_opacity, preview_cutters());
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
                    if (authored_) {
                        validate_field(ctx.device(), runtime_.gpu(), *field_, Recipe{},
                                       &*displayed_authored_);
                        validate_authored(ctx.device(), runtime_.gpu(), *displayed_authored_);
                    } else {
                        auto prefix = recipe_;
                        prefix.edits.resize(field_->displayed_edit_count());
                        validate_field(ctx.device(), runtime_.gpu(), *field_, prefix);
                    }
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
            if (authored_ && (reload_authored_ || materials_changed_)) {
                try {
                    auto next =
                        reload_authored_ ? load_authored_scene(config_.authored_path) : *authored_;
                    static_cast<void>(authored_appearance_hash(next));
                    validate_authored_motion(next);
                    const auto before = field_->stats().build_count;
                    auto previous = *authored_;
                    authored_ = std::move(next);
                    try {
                        prepare_time(ctx.device(), playback_seconds_, true);
                    } catch (...) {
                        authored_ = std::move(previous);
                        throw;
                    }
                    error_.clear();
                    std::cout << "sdf: authored update geometry=" << std::hex
                              << authored_geometry_hash(*authored_)
                              << " appearance=" << authored_appearance_hash(*authored_) << std::dec
                              << " field_rebuilt=" << (field_->stats().build_count != before)
                              << '\n';
                } catch (const std::exception& error) {
                    error_ = error.what();
                }
                reload_authored_ = false;
                materials_changed_ = false;
            }
            if (regenerate_requested_) {
                regenerate_requested_ = false;
                try {
                    auto next = make_recipe(config_.scene, config_.seed, config_.count,
                                            config_.cutter_width, config_.cutter_mode);
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
                if (authored_build()) {
                    playback_seconds_ =
                        authored_history_seek(config_.timeline, *authored_, active_prefix_);
                    sample_time(playback_seconds_);
                } else {
                    active_prefix_ = timeline_.prefix;
                }
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
                             current_cutter(), timeline_.cutter_opacity, preview_cutters());
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
    bool authored_build() const {
        return authored_ && (config_.timeline.motion == "construction" ||
                             config_.timeline.motion == "presentation");
    }
    void validate_authored_motion(const AuthoredScene& scene) const {
        if (!config_.recipe_path.empty() || !config_.export_recipe_path.empty() ||
            (config_.timeline.motion != "static" && config_.timeline.motion != "orbit" &&
             !((config_.timeline.motion == "construction" ||
                config_.timeline.motion == "presentation") &&
               !scene.history.empty())))
            throw std::invalid_argument(
                "Authored scenes support static/orbit, or "
                "construction/presentation with a saved history, and their own export format");
    }
    void sample_time(double time) {
        history_cutters_ = {};
        if (authored_build()) {
            auto sample =
                sample_authored_history(config_.timeline, *authored_, time, config_.debug);
            timeline_ = sample.timeline;
            history_cutters_ = sample.cutters;
        } else {
            timeline_ = sample_timeline(
                config_.timeline, authored_ ? authored_->program.size() : recipe_.edits.size(),
                time, config_.debug);
        }
    }
    std::span<const SceneInstruction> preview_cutters() const {
        return config_.cutter_cue ? history_cutters_ : std::span<const SceneInstruction>{};
    }
    const BoxEdit* current_cutter() const {
        if (authored_ || timeline_.cutter_index == 0 ||
            timeline_.cutter_index >= recipe_.edits.size() ||
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
            if (config_.validate_batches && authored_)
                validate_authored_lifecycle(device, runtime_.gpu());
            if (config_.validate_subtractions)
                validate_subtractions(device, runtime_.gpu(), *field_);
            if (config_.validate_history)
                validate_authored_history(device, runtime_.gpu(), *field_, *authored_);
            active_prefix_ = timeline_.prefix;
            if (authored_) {
                displayed_authored_ = authored_build()
                                          ? authored_history_scene(*authored_, timeline_.prefix)
                                          : *authored_;
                field_->build(*displayed_authored_);
                if (config_.validate) {
                    validate_field(device, runtime_.gpu(), *field_, Recipe{},
                                   &*displayed_authored_);
                    validate_authored(device, runtime_.gpu(), *displayed_authored_);
                    const auto before = field_->stats().build_count;
                    auto recolored = *displayed_authored_;
                    recolored.materials[0].base_roughness.x *= .5F;
                    field_->build(recolored);
                    if (field_->stats().build_count != before)
                        throw std::runtime_error("material-only edit rebuilt distance bricks");
                    std::cout << "sdf: material-only cache check passed builds=" << before << '\n';
                    if (std::any_of(authored_->patterns.begin(), authored_->patterns.end(),
                                    [](const auto& p) { return p.settings.x != 0; })) {
                        auto changed = *displayed_authored_;
                        changed.patterns[0].settings.y =
                            std::fmod(changed.patterns[0].settings.y + 1, 65536.0F);
                        field_->build(changed);
                        if (field_->stats().build_count != before)
                            throw std::runtime_error(
                                "procedural material edit rebuilt distance bricks");
                        std::cout << "sdf: procedural-only cache check passed builds=" << before
                                  << '\n';
                        for (std::size_t i = 0; i < authored_->patterns.size(); ++i) {
                            if (authored_->patterns[i].settings.x != 2)
                                continue;
                            auto tinted = *displayed_authored_;
                            tinted.patterns[i].offset.w =
                                tinted.patterns[i].offset.w == .1F ? .2F : .1F;
                            field_->build(tinted);
                            if (field_->stats().build_count != before)
                                throw std::runtime_error(
                                    "marble color edit rebuilt distance bricks");
                            std::cout << "sdf: marble color cache check passed builds=" << before
                                      << '\n';
                        }
                    }
                }
                std::cout << "sdf: authored geometry=" << std::hex
                          << authored_geometry_hash(*displayed_authored_)
                          << " appearance=" << authored_appearance_hash(*displayed_authored_)
                          << std::dec << '\n';
                if (authored_build())
                    std::cout << "sdf: history initial step=" << timeline_.prefix << "/"
                              << authored_->history.size() << '\n';
            } else
                field_->build(std::span<const BoxEdit>(recipe_.edits).first(active_prefix_));
            if (config_.validate && !authored_) {
                auto prefix = recipe_;
                prefix.edits.resize(active_prefix_);
                validate_field(device, runtime_.gpu(), *field_, prefix);
            }
            if (!authored_)
                std::cout << "sdf: recipe hash=" << std::hex << recipe_hash(recipe_) << std::dec
                          << '\n';
        }
        renderer_.create(device, target.extent, target.format, slots, *field_, config_,
                         displayed_authored_ ? &*displayed_authored_ : nullptr);
    }
    void prepare_time(vulkan::Device& device, double time, bool force = false) {
        sample_time(time);
        if (authored_) {
            if (!force && timeline_.prefix == active_prefix_)
                return;
            auto next = authored_build() ? authored_history_scene(*authored_, timeline_.prefix)
                                         : *authored_;
            runtime_.gpu().wait_queue_idle("sdf authored publication");
            field_->build(next);
            displayed_authored_ = std::move(next);
            renderer_.update_field(device, *field_);
            renderer_.update_authored(&*displayed_authored_);
            active_prefix_ = timeline_.prefix;
            error_.clear();
            if (authored_build())
                std::cout << "sdf: history step=" << active_prefix_ << "/"
                          << authored_->history.size() << " geometry=" << std::hex
                          << authored_geometry_hash(*displayed_authored_)
                          << " appearance=" << authored_appearance_hash(*displayed_authored_)
                          << std::dec << '\n';
            return;
        }
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
            if (authored_) {
                ImGui::Text("%s", authored_->name.c_str());
                ImGui::Text("%zu shapes / %zu instructions", authored_->leaf_count,
                            authored_->program.size());
            } else {
                ImGui::Text("%zu / %zu boxes", field_->displayed_edit_count(),
                            recipe_.edits.size());
                ImGui::Text("Recipe: %s", recipe_.generator.c_str());
            }
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
            if (ImGui::Combo("Motion", &motion, motions,
                             authored_ ? (authored_->history.empty() ? 2 : 4) : 5)) {
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
            if (authored_build()) {
                const auto total = authored_->history.size();
                const auto shown = std::min(active_prefix_, total);
                ImGui::Text("Edit %zu / %zu", shown, total);
                const auto label_index =
                    !history_cutters_.empty() ? shown : (shown ? shown - 1 : total);
                ImGui::TextWrapped("%s", label_index < total
                                             ? authored_->history[label_index].label.c_str()
                                             : "Empty workspace");
                const auto seek = [&](std::size_t count) {
                    playback_seconds_ = authored_history_seek(config_.timeline, *authored_, count);
                    if (!orbit_.paused())
                        orbit_.toggle_pause();
                };
                ImGui::BeginDisabled(shown == 0);
                if (ImGui::Button("Previous edit"))
                    seek(shown - 1);
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(shown == total);
                if (ImGui::Button("Next edit"))
                    seek(shown + 1);
                ImGui::EndDisabled();
                int step = static_cast<int>(shown);
                if (ImGui::SliderInt("Edit", &step, 0, static_cast<int>(total)))
                    seek(static_cast<std::size_t>(step));
            }
            int view = static_cast<int>(config_.debug);
            const char* views[]{"Shaded",         "Normals",     "Ray distance",
                                "Traversal work", "Brick cells", "Part identity",
                                "Base color",     "Roughness",   "Surface normals"};
            if (ImGui::Combo("View", &view, views, authored_ ? 9 : 5))
                config_.debug = static_cast<std::uint32_t>(view);
            int shadows = static_cast<int>(config_.shadows);
            const char* shadow_names[]{"Off", "Hard", "Soft (4 samples)"};
            if (ImGui::Combo("Shadows", &shadows, shadow_names, 3))
                config_.shadows = static_cast<std::uint32_t>(shadows);
            ImGui::Checkbox("Local ambient occlusion", &config_.ao);
            ImGui::Checkbox("Clay inspection", &config_.clay);
            ImGui::BeginDisabled(authored_ && !authored_build());
            ImGui::Checkbox(authored_ ? "Cut preview" : "Cutter bounds cue", &config_.cutter_cue);
            ImGui::EndDisabled();
            ImGui::SliderFloat("Sun elevation", &config_.sun_elevation, 5, 85, "%.0f deg");
            ImGui::SliderFloat("Sun azimuth", &config_.sun_azimuth, -180, 180, "%.0f deg");
            ImGui::SliderFloat("Exposure", &config_.exposure, -3, 3, "%.1f stops");
            ImGui::BeginDisabled(authored_ && !config_.clay);
            ImGui::SliderFloat(authored_ ? "Clay roughness" : "Roughness", &config_.roughness,
                               0.05F, 1);
            ImGui::EndDisabled();
            ImGui::SliderFloat("Focus height", &config_.target_y, -0.3F, 0.3F);
            ImGui::SliderFloat("Shadow softness", &config_.shadow_softness, 0, 0.15F);
            ImGui::Separator();
            if (authored_) {
                if (ImGui::Button("Reload authored scene"))
                    reload_authored_ = true;
                if (!config_.export_authored_path.empty() && ImGui::Button("Export authored scene"))
                    save_authored_scene(config_.export_authored_path, *authored_);
                for (std::size_t i = 0; i < authored_->materials.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));
                    auto& material = authored_->materials[i];
                    if (ImGui::TreeNode(authored_->material_names[i].c_str())) {
                        materials_changed_ |=
                            ImGui::ColorEdit3("Base color", &material.base_roughness.x);
                        materials_changed_ |=
                            ImGui::SliderFloat("Roughness", &material.base_roughness.w, .05F, 1);
                        materials_changed_ |=
                            ImGui::ColorEdit3("Emission", &material.emission.x,
                                              ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
                        for (int channel = 0; channel < 3; ++channel)
                            material.emission[channel] =
                                std::clamp(material.emission[channel], 0.0F, 32.0F);
                        auto& pattern = authored_->patterns[i];
                        if (pattern.settings.x > 0) {
                            ImGui::Text("Procedural: %s",
                                        pattern.settings.x == 1 ? "Limestone" : "Marble");
                            ImGui::Checkbox("Surface pattern", &config_.procedural);
                            ImGui::Checkbox("Bump detail", &config_.bump);
                            materials_changed_ |=
                                ImGui::ColorEdit3("Secondary color", &pattern.secondary.x);
                            materials_changed_ |=
                                ImGui::SliderFloat("Pattern scale", &pattern.settings.z, .25F, 32);
                            materials_changed_ |=
                                ImGui::SliderFloat("Color strength", &pattern.settings.w, 0, 1);
                            materials_changed_ |= ImGui::SliderFloat("Roughness variation",
                                                                     &pattern.secondary.w, 0, .5F);
                            materials_changed_ |=
                                ImGui::SliderFloat("Grain scale", &pattern.detail.y, 1, 512);
                            materials_changed_ |= ImGui::SliderFloat(
                                "Bump height", &pattern.detail.x, 0, .002F, "%.5f");
                            if (pattern.settings.x == 2) {
                                const int detail = marble_detail_index(pattern);
                                if (ImGui::BeginCombo(
                                        "Marble detail",
                                        detail < 0
                                            ? "Custom"
                                            : marble_details[static_cast<std::size_t>(detail)]
                                                  .name)) {
                                    for (std::size_t j = 0; j < marble_details.size(); ++j) {
                                        const bool selected = detail == static_cast<int>(j);
                                        if (ImGui::Selectable(marble_details[j].name, selected)) {
                                            apply_marble_detail(pattern, j);
                                            materials_changed_ = true;
                                            std::cout << "sdf: marble detail preset="
                                                      << marble_details[j].name << '\n';
                                        }
                                        if (selected)
                                            ImGui::SetItemDefaultFocus();
                                    }
                                    ImGui::EndCombo();
                                }
                                materials_changed_ |= ImGui::SliderFloat(
                                    "Vein frequency", &pattern.veins.w, .25F, 32);
                                materials_changed_ |= ImGui::SliderFloat(
                                    "Vein width", &pattern.detail.z, .02F, .5F, "%.3f");
                                materials_changed_ |=
                                    ImGui::SliderFloat("Vein warping", &pattern.detail.w, 0, 12);
                                materials_changed_ |= ImGui::SliderFloat(
                                    "Body color variation", &pattern.offset.w, 0, .25F, "%.3f");
                            }
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
            } else {
                const char* scene_labels[]{
                    "Sticks",      "Negative sticks", "Negative sphere",    "Box",
                    "Rotated box", "Cut fixture",     "Negative stick ball"};
                const std::string scene_names[]{
                    "sticks", "negative-sticks",    "negative-sphere", "box", "rotated",
                    "cut",    "negative-stick-ball"};
                int scene = 0;
                for (int i = 0; i < 7; ++i)
                    if (config_.scene == scene_names[i])
                        scene = i;
                if (ImGui::Combo("Generator", &scene, scene_labels, 7))
                    config_.scene = scene_names[scene];
                if (!config_.recipe_path.empty())
                    ImGui::TextWrapped("Loaded explicit recipe. Generate scene replaces it.");
                ImGui::InputScalar("Seed", ImGuiDataType_U32, &config_.seed);
                const bool negative = config_.scene == "negative-sticks" ||
                                      config_.scene == "negative-sphere" ||
                                      config_.scene == "negative-stick-ball";
                ImGui::InputScalar(negative ? "Cutter count" : "Stick count", ImGuiDataType_U32,
                                   &config_.count);
                if (config_.scene == "negative-stick-ball") {
                    ImGui::SliderFloat("Cutter width", &config_.cutter_width, 0.003F, 0.06F,
                                       "%.3f");
                    const char* mode_labels[]{"Original sticks", "Intro bores", "All through"};
                    const std::string mode_names[]{"original", "intro-bores", "through"};
                    int mode = 0;
                    for (int i = 0; i < 3; ++i)
                        if (config_.cutter_mode == mode_names[i])
                            mode = i;
                    if (ImGui::Combo("Cutter mode", &mode, mode_labels, 3))
                        config_.cutter_mode = mode_names[mode];
                }
                if (ImGui::Button("Generate scene")) {
                    config_.count = std::min(config_.count, negative ? 65535U : 65536U);
                    regenerate_requested_ = true;
                }
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
    std::optional<AuthoredScene> authored_;
    std::optional<AuthoredScene> displayed_authored_;
    std::span<const SceneInstruction> history_cutters_;
    ProjectRuntimeAdapter runtime_{1};
    OrbitController orbit_{{.distance = 1.35F, .min_distance = 0.6F, .max_distance = 5.0F}};
    std::unique_ptr<Field> field_;
    Renderer renderer_;
    TimelineSample timeline_;
    std::size_t active_prefix_ = std::numeric_limits<std::size_t>::max();
    double playback_seconds_ = 0;
    bool regenerate_requested_ = false;
    bool reload_authored_ = false, materials_changed_ = false;
    std::string error_;
};
} // namespace
int run_sdf(const SdfConfig& config) {
    return App(config).run();
}
} // namespace cubey::projects::sdf
