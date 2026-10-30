#include "sdf_comparison.h"
#include "sdf_config.h"
#include "sdf_field.h"
#include "sdf_history.h"
#include "sdf_material.h"
#include "sdf_render_modes.h"
#include "sdf_renderer.h"
#include "sdf_validation.h"
#include "sdf_workshop.h"

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
#include <fstream>
#include <imgui.h>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>

namespace cubey::projects::sdf {
namespace {
class App {
  public:
    explicit App(SdfConfig config)
        : config_(std::move(config)),
          recipe_((!config_.authored_path.empty() || !config_.workshop.empty()) ? Recipe{}
                  : config_.recipe_path.empty()
                      ? make_recipe(config_.scene, config_.seed, config_.count,
                                    config_.cutter_width, config_.cutter_mode)
                      : load_recipe(config_.recipe_path)),
          authored_(
              !config_.workshop.empty()
                  ? std::optional<AuthoredScene>{make_material_workshop(config_)}
              : config_.authored_path.empty()
                  ? std::nullopt
                  : std::optional<AuthoredScene>{load_authored_scene(config_.authored_path)}) {
        resolve_render_mode(config_, authored_.has_value());
        validate_source_optimization(config_);
        if (!config_.comparison_actions.empty()) {
            std::ifstream input(config_.comparison_actions);
            study_actions_ = nlohmann::json::parse(input);
            if (!config_.common.headless || !authored_ || config_.timeline.motion != "static" ||
                config_.comparison_output.empty() ||
                study_actions_.at("format") != "cubey.sdf.action-study" ||
                study_actions_.at("version") != 1 ||
                study_actions_.at("actions").size() != config_.common.frames ||
                config_.common.frames > 128)
                throw std::invalid_argument("action study requires authored static headless frames "
                                            "matching its bounded manifest and a receipt path");
            study_receipt_ = nlohmann::json::array();
            if (!config_.quality_output.empty() &&
                (!study_actions_.value("capture_frames", false) ||
                 std::any_of(study_actions_.at("actions").begin(),
                             study_actions_.at("actions").end(), [](const auto& action) {
                                 return !action.value("geometry_unchanged", false) ||
                                        action.contains("fault");
                             })))
                throw std::invalid_argument("quality pose studies require captured "
                                            "unchanged-geometry actions without faults");
            if ((!config_.common.headless_render_only &&
                 config_.common.capture_mode != CaptureMode::Video && config_.common.frames != 1) ||
                (study_actions_.value("capture_frames", false) &&
                 !config_.common.headless_render_only))
                throw std::invalid_argument("multi-frame action studies require render-only or "
                                            "video; per-frame PNG readback requires render-only");
        }
        if (!config_.workshop.empty())
            catalog_ = asset::load_material_catalog(
                std::filesystem::path(CUBEY_MATERIAL_RECIPE_DIR) / "catalog.json");
        if (authored_)
            validate_authored_motion(*authored_);
        if ((!config_.quality_output.empty() || config_.cached_quality != "ordinary") && !authored_)
            throw std::invalid_argument("quality output requires an authored scene");
        if ((config_.acceleration_variant == "unclamped" && config_.surface_trace != "hybrid") ||
            (config_.acceleration_variant != "off" && config_.acceleration_variant != "control" &&
             config_.acceleration_variant != "unclamped" && config_.surface_trace != "analytic"))
            throw std::invalid_argument("acceleration variant does not match its source mode");
        if (config_.acceleration_variant != "off" &&
            (!config_.common.headless || !authored_ || config_.timeline.motion != "static" ||
             config_.csg_evaluation != "pruned" || config_.surface_trace == "sampled" ||
             config_.region_axis != 64 ||
             (config_.comparison_stage != "off" && config_.frame_profile == "off") ||
             config_.comparison_tight_root || config_.comparison_residency != "host"))
            throw std::invalid_argument("acceleration study requires static headless authored "
                                        "pruned source at axis 64 and no diagnostic ablations");
        if (config_.frame_profile != "off") {
            const bool stages =
                config_.frame_profile == "runtime" || config_.frame_profile == "fixed";
            if (config_.acceleration_variant != "root-far" || config_.surface_trace != "analytic" ||
                !config_.common.headless || !authored_ || config_.timeline.motion != "static" ||
                config_.lighting != "atmosphere" || config_.debug || config_.ao || config_.ground ||
                config_.clouds || config_.occluder || !config_.comparison_actions.empty() ||
                stages == (config_.comparison_stage == "off"))
                throw std::invalid_argument(
                    "frame profiling requires static headless root-far "
                    "analytic atmosphere, debug 0, no AO/ground/clouds/actions; runtime/fixed "
                    "profiles require a comparison stage, other profiles require stage off");
            if ((config_.frame_profile == "single-material" ||
                 config_.frame_profile == "specialized" ||
                 config_.frame_profile.starts_with("opt-")) &&
                (authored_->materials.size() != 1 || !config_.material_fast_path))
                throw std::invalid_argument(
                    "single-material profile requires the eligible one-material shortcut");
        }
        if (config_.comparison_residency != "host" &&
            (config_.comparison_stage == "off" || config_.surface_trace == "sampled"))
            throw std::invalid_argument("residency ablation requires a source comparison stage");
        if (config_.comparison_tight_root &&
            (config_.comparison_stage == "off" || config_.surface_trace == "sampled"))
            throw std::invalid_argument("tight-root rendering requires a source comparison stage");
        if (config_.comparison_stage != "off" &&
            (!config_.common.headless || !authored_ || config_.timeline.motion != "static" ||
             config_.lighting != "atmosphere" || config_.debug != 0 || config_.ao ||
             config_.ground || config_.clouds || config_.occluder ||
             !config_.comparison_actions.empty() ||
             (config_.surface_trace != "sampled" && config_.csg_evaluation != "pruned")))
            throw std::invalid_argument(
                "comparison stages require static authored headless atmosphere rendering, "
                "debug 0, no AO/ground/clouds/occluder or scripted actions; source stages "
                "require pruned evaluation");
        if (config_.csg_evaluation == "pruned" &&
            (!authored_ || config_.surface_trace == "sampled" ||
             (config_.region_axis != 4 && config_.region_axis != 16 && config_.region_axis != 64)))
            throw std::invalid_argument(
                "pruned evaluation requires authored source tracing and region axis 4, 16 or 64");
        if (config_.surface_trace != "sampled") {
            if (!authored_)
                throw std::invalid_argument("source tracing requires an authored scene");
            if (config_.debug == 4)
                throw std::invalid_argument("brick debug is unavailable for source tracing");
            if (config_.validate || config_.validate_batches || config_.validate_subtractions ||
                config_.validate_history)
                throw std::invalid_argument("source tracing uses --comparison-input; legacy field "
                                            "oracles require sampled tracing");
        }
        if (!authored_ && config_.timeline.motion == "material-story")
            throw std::invalid_argument("material-story requires an authored body/cut history");
        if (config_.lighting != "atmosphere" && !authored_)
            throw std::invalid_argument("material-study lighting requires an authored scene");
        if (config_.clouds && config_.lighting != "material-atmosphere")
            throw std::invalid_argument("SDF clouds require material-atmosphere lighting");
        if (config_.lighting_cycle != "frozen" && config_.lighting != "material-atmosphere")
            throw std::invalid_argument("lighting cycle requires material-atmosphere lighting");
        if (!std::isfinite(config_.cloud_time) || config_.cloud_time < 0 ||
            (config_.cloud_view_samples != 1 && config_.cloud_view_samples != 2 &&
             config_.cloud_view_samples != 4) ||
            !std::isfinite(config_.cloud_coverage) || config_.cloud_coverage < 0 ||
            config_.cloud_coverage > 1)
            throw std::invalid_argument("invalid frozen SDF cloud options");
        if (authored_ && config_.lighting == "atmosphere" &&
            std::any_of(authored_->optics.begin(), authored_->optics.end(),
                        [](const auto& o) { return !default_optics(o); }))
            throw std::invalid_argument(
                "non-default material optics require material-study lighting");
        if (config_.lighting != "atmosphere" && (config_.ground || config_.occluder))
            throw std::invalid_argument("material study supports environment-only composition");
        if (config_.debug >= 9 && config_.lighting == "atmosphere")
            throw std::invalid_argument(
                "optical/component diagnostics require material-study lighting");
        if (config_.debug >= 10 && authored_ &&
            std::any_of(authored_->optics.begin(), authored_->optics.end(),
                        [](const auto& o) { return o.settings.y > 0; }))
            throw std::invalid_argument("component diagnostics require opaque materials");
        if (!authored_ && !config_.export_authored_path.empty())
            throw std::invalid_argument("--export-authored requires --authored-scene");
        if (config_.validate_history && (!authored_ || authored_->history.empty()))
            throw std::invalid_argument("--validate-history requires a saved authored history");
        if ((config_.environment_extent != 64 && config_.environment_extent != 128) ||
            (config_.glass_crossings != 4 && config_.glass_crossings != 8 &&
             config_.glass_crossings != 16) ||
            config_.debug > 12 || config_.shadows > 2 || !std::isfinite(config_.yaw) ||
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
            callbacks.record_frame = [&](host::HeadlessPngContext& ctx,
                                         const host::HeadlessCaptureFrame& frame,
                                         VkCommandBuffer command,
                                         const host::HeadlessRenderTarget& target) {
                auto view = config_;
                view.debug = timeline_.debug;
                renderer_->record(ctx.device(), command, target, frame.frame_slot, false, view,
                                  config_.yaw + orbit_.yaw() + timeline_.yaw, orbit_.pitch(),
                                  orbit_.distance(), ctx.profile_recorder(), frame.index,
                                  current_cutter(), timeline_.cutter_opacity, preview_cutters());
                if (!study_actions_.is_null() && study_actions_.value("capture_frames", false)) {
                    vulkan::transition_image_layout(
                        command,
                        vulkan::finish_color_attachment_for_readback_transition(target.image));
                    // This queues behind the current owner job's submission.
                    // Queue the copy while the host's slot image is still alive,
                    // including the final frame whose slot dies before shutdown.
                    study_readback_ = runtime_.gpu().enqueue_rgba8_image_readback(
                        target.image, target.extent, "study completed frame pixels");
                }
            };
            callbacks.before_frame = [&](host::HeadlessPngContext& ctx,
                                         const host::HeadlessCaptureFrame& frame) {
                if (!study_actions_.is_null()) {
                    finish_study_action();
                    apply_study_action(ctx.device(), frame.index);
                }
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
                     config_.timeline.motion == "material-story" ||
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
            renderer_->destroy();
        };
        callbacks.update = [&](host::WindowedAppContext& ctx, const FrameTiming& timing) {
            orbit_.update_from_input(ctx.filtered_input(), timing.delta_seconds);
            if (!requested_render_mode_.empty() || requested_supersample_) {
                try {
                    change_render_mode(ctx.device(), ctx.gpu(), requested_render_mode_, {},
                                       requested_supersample_);
                    error_.clear();
                } catch (const std::exception& error) {
                    error_ = error.what();
                    std::cout << "sdf: mode change rejected: " << error.what() << '\n';
                }
                requested_render_mode_.clear();
                requested_supersample_.reset();
            }
            if (ctx.filtered_input().key_pressed(input::Key::R)) {
                playback_seconds_ = 0;
                orbit_.set_pitch(config_.pitch);
                orbit_.set_distance(config_.distance);
            }
            if (authored_ && (reload_authored_ || materials_changed_ || workshop_changed_)) {
                try {
                    auto next = workshop_changed_
                                    ? make_material_workshop(config_, reload_workshop_material_
                                                                          ? nullptr
                                                                          : &*authored_)
                                : reload_authored_ ? load_authored_scene(config_.authored_path)
                                                   : *authored_;
                    static_cast<void>(authored_appearance_hash(next));
                    validate_authored_motion(next);
                    const auto before = field_ ? field_->stats().build_count : 0;
                    const auto regions_before = region_builds_;
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
                              << " field_rebuilt="
                              << (field_ && field_->stats().build_count != before)
                              << " region_rebuilt=" << (region_builds_ != regions_before) << '\n';
                } catch (const std::exception& error) {
                    error_ = error.what();
                    std::cout << "sdf: authored update rejected: " << error.what() << '\n';
                }
                reload_authored_ = false;
                materials_changed_ = false;
                workshop_changed_ = false;
                reload_workshop_material_ = false;
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
            if ((config_.timeline.motion != "static" || config_.lighting_cycle != "frozen") &&
                !orbit_.paused())
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
                if (field_)
                    renderer_->update_field(ctx.device(), *field_);
            }
        };
        callbacks.draw_ui = [&](host::WindowedAppContext&) { draw_ui(); };
        callbacks.record_frame = [&](host::WindowedAppContext& ctx,
                                     const host::WindowedRenderFrame& frame) {
            const vulkan::CommandRecorder recorder(frame.command_buffer);
            recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
            auto view = config_;
            view.debug = timeline_.debug;
            renderer_->record(ctx.device(), frame.command_buffer, frame.color_target,
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
                             config_.timeline.motion == "presentation" ||
                             config_.timeline.motion == "material-story");
    }
    void validate_authored_motion(const AuthoredScene& scene) const {
        if (config_.timeline.motion == "material-story")
            (void)sample_authored_history(config_.timeline, scene, 0, config_.debug);
        const bool saved_motion =
            !scene.history.empty() && (config_.timeline.motion == "construction" ||
                                       config_.timeline.motion == "presentation" ||
                                       config_.timeline.motion == "material-story");
        if (!config_.recipe_path.empty() || !config_.export_recipe_path.empty() ||
            (config_.timeline.motion != "static" && config_.timeline.motion != "orbit" &&
             !saved_motion))
            throw std::invalid_argument(
                "Authored scenes support static/orbit, or "
                "construction/presentation with a saved history, and their own export format");
    }
    void sample_time(double time) {
        if (config_.lighting_cycle != "frozen") {
            const auto sample =
                material_lighting_cycle(config_.lighting_cycle, time, config_.timeline.duration);
            config_.sun_elevation = sample.sun_elevation;
            config_.sun_azimuth = sample.sun_azimuth;
            config_.exposure = sample.exposure;
            config_.cloud_time = sample.cloud_seconds;
            renderer_->set_environment_elapsed_seconds(sample.elapsed_seconds);
        }
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
        gpu_ = &gpu;
        target_extent_ = target.extent;
        target_format_ = target.format;
        frame_slots_ = slots;
        runtime_.attach_gpu_if_needed(gpu);
        if (!geometry_initialized_) {
            if (config_.surface_trace != "analytic")
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
                if (config_.csg_evaluation == "pruned" || cached_source_normals(config_)) {
                    regions_ = std::make_unique<GpuRegionPrograms>(
                        device, *displayed_authored_, config_.region_axis, region_reference_limit,
                        config_.comparison_residency == "regions" ||
                            config_.comparison_residency == "all",
                        config_.acceleration_variant != "off" || cached_source_normals(config_)
                            ? &runtime_.gpu()
                            : nullptr,
                        cached_source_normals(config_) ? "root-far" : config_.acceleration_variant);
                    ++region_builds_;
                }
                if (field_)
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
                    auto optical_edit = *displayed_authored_;
                    optical_edit.optics[0].attenuation.x *= .9F;
                    field_->build(optical_edit);
                    if (field_->stats().build_count != before)
                        throw std::runtime_error("optics-only edit rebuilt distance bricks");
                    std::cout << "sdf: optics-only cache check passed builds=" << before << '\n';
                    if (std::any_of(authored_->libraries.begin(), authored_->libraries.end(),
                                    [](const auto& p) {
                                        return !procedural::default_surface_library(p);
                                    })) {
                        auto changed = *displayed_authored_;
                        auto& library = changed.libraries[0];
                        if (library.settings.x > 0)
                            library.settings.y = std::fmod(library.settings.y + 1, 65536.0F);
                        if (library.optical_options.x > .5F)
                            changed.materials[0].base_roughness.w =
                                std::min(1.0F, changed.materials[0].base_roughness.w + .05F);
                        field_->build(changed);
                        if (field_->stats().build_count != before)
                            throw std::runtime_error("library-only edit rebuilt distance bricks");
                        std::cout << "sdf: library-only cache check passed builds=" << before
                                  << '\n';
                    }
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
            geometry_initialized_ = true;
            field_generation_ += field_ ? field_->stats().build_count : 0;
            std::cout << "sdf: trace=" << config_.surface_trace
                      << " field_bytes=" << (field_ ? field_->stats().allocation_bytes : 0) << '\n';
        }
        renderer_->create(device, gpu, target.extent, target.format, slots, field_.get(), config_,
                          displayed_authored_ ? &*displayed_authored_ : nullptr, regions_.get());
        log_render_mode();
    }
    void log_render_mode() const {
        std::cout << "sdf: render-mode requested=" << config_.render_mode
                  << " effective=" << config_.effective_render_mode
                  << " trace=" << config_.surface_trace << " cached=" << config_.cached_quality
                  << " axis=" << config_.region_axis << " ss=" << config_.supersample
                  << " generation=" << mode_generation_ << " field_generation=" << field_generation_
                  << " field_bytes=" << (field_ ? field_->stats().allocation_bytes : 0)
                  << " region_bytes=" << (regions_ ? regions_->allocation_bytes : 0) << '\n';
    }
    void change_render_mode(vulkan::Device& device, vulkan::GpuRuntime& gpu, std::string_view mode,
                            std::string_view fault = {},
                            std::optional<std::uint32_t> supersample = {}) {
        if (!config_.quality_output.empty())
            throw std::invalid_argument("quality probes cannot change rendering mode");
        auto next_config =
            mode.empty() ? config_ : config_for_render_mode(config_, mode, authored_.has_value());
        if (supersample)
            next_config.supersample = *supersample;
        validate_source_optimization(next_config);
        if (next_config.surface_trace != "sampled" && next_config.debug == 4)
            throw std::invalid_argument("brick debug requires a cached rendering mode");
        if ((mode.empty() || config_.named_render_mode) &&
            next_config.effective_render_mode == config_.effective_render_mode &&
            next_config.supersample == config_.supersample) {
            config_ = std::move(next_config);
            log_render_mode();
            return;
        }
        const auto start = std::chrono::steady_clock::now();
        std::unique_ptr<Field> next_field;
        std::unique_ptr<GpuRegionPrograms> next_regions;
        std::unique_ptr<Renderer> next_renderer;
        const bool needs_field = next_config.surface_trace != "analytic";
        const bool needs_regions =
            next_config.csg_evaluation == "pruned" || cached_source_normals(next_config);
        const auto region_variant = [](const SdfConfig& c) {
            return cached_source_normals(c) ? std::string("root-far") : c.acceleration_variant;
        };
        try {
            if (needs_field && !field_) {
                next_field =
                    std::make_unique<Field>(device, runtime_.gpu(), next_config.atlas_axis);
                next_field->build(*displayed_authored_);
                if (fault == "mode-field")
                    throw CapacityExceeded("injected mode field preparation failure");
            }
            if (needs_regions && (!regions_ || regions_->axis != next_config.region_axis ||
                                  region_variant(config_) != region_variant(next_config))) {
                next_regions = std::make_unique<GpuRegionPrograms>(
                    device, *displayed_authored_, next_config.region_axis, region_reference_limit,
                    false, cached_source_normals(next_config) ? &runtime_.gpu() : nullptr,
                    region_variant(next_config));
                if (fault == "mode-region")
                    throw CapacityExceeded("injected mode region preparation failure");
            }
            next_renderer = std::make_unique<Renderer>();
            next_renderer->create(
                device, gpu, target_extent_, target_format_, frame_slots_,
                needs_field ? (next_field ? next_field.get() : field_.get()) : nullptr, next_config,
                displayed_authored_ ? &*displayed_authored_ : nullptr,
                needs_regions ? (next_regions ? next_regions.get() : regions_.get()) : nullptr,
                fault);
            runtime_.gpu().wait_queue_idle("sdf rendering mode activation");
            if (fault == "mode-activate")
                throw CapacityExceeded("injected mode activation failure");
        } catch (...) {
            if (next_renderer) {
                runtime_.gpu().wait_queue_idle("sdf discarded rendering mode");
                next_renderer->destroy();
            }
            throw;
        }
        // All preparation is complete; preceding use is retired before release.
        renderer_->destroy();
        renderer_ = std::move(next_renderer);
        if (next_field) {
            field_generation_ += next_field->stats().build_count;
            field_ = std::move(next_field);
        }
        if (!needs_field)
            field_.reset();
        if (next_regions) {
            regions_ = std::move(next_regions);
            ++region_builds_;
        }
        if (!needs_regions)
            regions_.reset();
        config_ = std::move(next_config);
        ++mode_generation_;
        mode_switch_ms_ =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        log_render_mode();
        std::cout << "sdf: mode switch_ms=" << mode_switch_ms_ << '\n';
    }
    void prepare_time(vulkan::Device& device, double time, bool force = false) {
        sample_time(time);
        if (authored_) {
            if (!force && timeline_.prefix == active_prefix_)
                return;
            auto next = authored_build() ? authored_history_scene(*authored_, timeline_.prefix)
                                         : *authored_;
            // Build all fallible candidate resources before publishing either the
            // field or the program. Queue-idle retires the preceding snapshot.
            std::unique_ptr<GpuRegionPrograms> next_regions;
            if ((config_.csg_evaluation == "pruned" || cached_source_normals(config_)) &&
                (!regions_ || regions_->geometry_hash != authored_geometry_hash(next)))
                next_regions = std::make_unique<GpuRegionPrograms>(
                    device, next, config_.region_axis, region_reference_limit,
                    config_.comparison_residency == "regions" ||
                        config_.comparison_residency == "all",
                    config_.acceleration_variant != "off" || cached_source_normals(config_)
                        ? &runtime_.gpu()
                        : nullptr,
                    cached_source_normals(config_) ? "root-far" : config_.acceleration_variant);
            auto next_program = renderer_->prepare_authored(
                device, &next, next_regions ? next_regions.get() : regions_.get(), study_fault_);
            if (study_fault_ == "region" || study_fault_ == "field")
                throw CapacityExceeded("injected study failure before " + study_fault_ +
                                       " publication");
            runtime_.gpu().wait_queue_idle("sdf authored publication");
            if (study_fault_ == "activate")
                throw CapacityExceeded("injected failure before authored activation");
            const auto previous_field_builds = field_ ? field_->stats().build_count : 0;
            if (field_)
                field_->build(next);
            field_generation_ += field_ ? field_->stats().build_count - previous_field_builds : 0;
            displayed_authored_ = std::move(next);
            if (next_regions) {
                regions_ = std::move(next_regions);
                ++region_builds_;
            }
            if (field_)
                renderer_->update_field(device, *field_);
            renderer_->update_authored(device, &*displayed_authored_, regions_.get(),
                                       std::move(next_program));
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
        const auto previous_field_builds = field_->stats().build_count;
        field_->build(std::span<const BoxEdit>(recipe_.edits).first(timeline_.prefix));
        field_generation_ += field_->stats().build_count - previous_field_builds;
        renderer_->update_field(device, *field_);
        active_prefix_ = timeline_.prefix;
        error_.clear();
    }
    void finish_study_action() {
        if (!pending_study_action_)
            return;
        runtime_.gpu().wait_queue_idle("study first completed GPU frame");
        auto& row = study_receipt_.back();
        if (!config_.quality_output.empty()) {
            auto path = config_.quality_output;
            path.replace_extension("frame-" + std::to_string(row.at("frame").get<unsigned>()) +
                                   ".bin");
            renderer_->save_quality_snapshot(path);
            row["quality_file"] = path.string();
        }
        row["request_to_waited_gpu_completion_ms"] =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                      *pending_study_action_)
                .count();
        pending_study_action_.reset();
        if (study_actions_.value("capture_frames", false)) {
            if (!study_readback_)
                throw std::runtime_error("action frame has no rendered target");
            auto pixels = runtime_.gpu().take_completed_readback(*study_readback_);
            auto path = config_.comparison_output;
            path.replace_extension("frame-" + std::to_string(row.at("frame").get<unsigned>()) +
                                   ".png");
            auto capture =
                runtime_.context().capture_queue().enqueue_png({.output_path = path,
                                                                .width = pixels.width,
                                                                .height = pixels.height,
                                                                .rgba8 = std::move(pixels.rgba8)});
            capture.finish();
            row["frame_png"] = path.string();
            study_readback_.reset();
        }
    }
    void apply_study_action(vulkan::Device& device, std::uint64_t frame) {
        const auto cones_before = renderer_->cone_builds();
        const auto& action = study_actions_.at("actions").at(frame);
        const auto oracle_start = std::chrono::steady_clock::now();
        auto expected = parse_authored_scene(action.at("visible_scene").dump());
        if (action.contains("visible_history_prefix"))
            expected = authored_history_scene(
                expected, action.at("visible_history_prefix").get<std::size_t>());
        const auto expected_geometry = authored_geometry_hash(expected),
                   expected_appearance = authored_appearance_hash(expected);
        const auto oracle_ms = std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() - oracle_start)
                                   .count();
        const auto start = std::chrono::steady_clock::now();
        const auto field_before = field_generation_;
        const auto mode_before = mode_generation_;
        const auto region_before = region_builds_;
        auto previous = *authored_;
        const auto previous_debug = config_.debug;
        const auto previous_fast_path = config_.material_fast_path;
        const std::array previous_camera{config_.yaw, config_.pitch, config_.distance};
        const auto restore_camera = [&] {
            config_.yaw = previous_camera[0];
            config_.pitch = previous_camera[1];
            config_.distance = previous_camera[2];
            orbit_.set_pitch(config_.pitch);
            orbit_.set_distance(config_.distance);
        };
        bool failed = false;
        try {
            if (action.contains("render")) {
                const auto& render = action.at("render");
                config_.debug = render.value("debug", config_.debug);
                config_.material_fast_path =
                    render.value("material_fast_path", config_.material_fast_path);
                config_.yaw = render.value("yaw", config_.yaw);
                config_.pitch = render.value("pitch", config_.pitch);
                config_.distance = render.value("distance", config_.distance);
                if (!std::isfinite(config_.yaw) || !std::isfinite(config_.pitch) ||
                    !std::isfinite(config_.distance) || config_.distance < .6F ||
                    config_.distance > 5.F || std::abs(config_.pitch) > 1.5F)
                    throw std::invalid_argument("invalid study camera pose");
                orbit_.set_pitch(config_.pitch);
                orbit_.set_distance(config_.distance);
                if (config_.debug > 12 ||
                    (config_.surface_trace != "sampled" && config_.debug == 4))
                    throw std::invalid_argument("unsupported study debug view");
            }
            auto next = parse_authored_scene(action.at("scene").dump());
            if (action.contains("history_prefix"))
                next = authored_history_scene(next, action.at("history_prefix").get<std::size_t>());
            validate_authored_motion(next);
            study_fault_ = action.value("fault", std::string{});
            if (action.contains("render_mode")) {
                if (authored_geometry_hash(next) != authored_geometry_hash(*displayed_authored_) ||
                    authored_appearance_hash(next) !=
                        authored_appearance_hash(*displayed_authored_))
                    throw std::invalid_argument(
                        "mode study actions must retain the published scene");
                change_render_mode(device, *gpu_, action.at("render_mode").get<std::string>(),
                                   study_fault_);
            } else {
                authored_ = std::move(next);
                prepare_time(device, 0, true);
            }
        } catch (const CapacityExceeded&) {
            authored_ = std::move(previous);
            config_.debug = previous_debug;
            config_.material_fast_path = previous_fast_path;
            restore_camera();
            failed = true;
        } catch (...) {
            authored_ = std::move(previous);
            config_.debug = previous_debug;
            config_.material_fast_path = previous_fast_path;
            restore_camera();
            study_fault_.clear();
            throw;
        }
        study_fault_.clear();
        if (failed != action.value("expect_failure", false))
            throw std::runtime_error("unexpected study publication result");
        if (authored_geometry_hash(*displayed_authored_) != expected_geometry ||
            authored_appearance_hash(*displayed_authored_) != expected_appearance)
            throw std::runtime_error("study published mixed geometry/appearance revision");
        const auto field_delta = field_generation_ - field_before;
        const auto region_delta = region_builds_ - region_before;
        if (action.value("geometry_unchanged", false) && !action.contains("render_mode") &&
            (field_delta || region_delta))
            throw std::runtime_error(
                "appearance or failed action rebuilt a published geometry cache");
        const auto cone_delta = mode_generation_ == mode_before
                                    ? renderer_->cone_builds() - cones_before
                                    : renderer_->cone_builds();
        if (renderer_->cone_hash() && renderer_->cone_hash() != expected_geometry)
            throw std::runtime_error("study published a stale prepared cone generation");
        if (action.value("geometry_unchanged", false) && cone_delta)
            throw std::runtime_error("appearance or failed action replaced the prepared cache");
        const auto shader = source_shader_route_name(renderer_->shader_route(config_));
        if (action.contains("expect_shader") && action.at("expect_shader") != shader)
            throw std::runtime_error("study selected unexpected published shader route");
        const auto optimization = renderer_->source_optimization(config_);
        if (action.contains("expect_optimization") &&
            action.at("expect_optimization") != optimization)
            throw std::runtime_error("study selected unexpected published optimization");
        study_receipt_.push_back(
            {{"label", action.at("label")},
             {"frame", frame},
             {"failed", failed},
             {"render_mode_requested", config_.render_mode},
             {"render_mode", config_.effective_render_mode},
             {"mode_generation", mode_generation_},
             {"mode_switch_ms", mode_generation_ == mode_before ? 0 : mode_switch_ms_},
             {"field_generation", field_generation_},
             {"shader_route", shader},
             {"source_optimization", optimization},
             {"cone_builds", renderer_->cone_builds()},
             {"cone_build_delta", cone_delta},
             {"cone_geometry_hash", renderer_->cone_hash()},
             {"shader_pipeline_count", renderer_->shader_pipeline_count()},
             {"published_material_count", displayed_authored_->materials.size()},
             {"debug", config_.debug},
             {"material_fast_path", config_.material_fast_path},
             {"camera",
              {{"yaw", config_.yaw},
               {"pitch", orbit_.pitch()},
               {"distance", orbit_.distance()},
               {"target_y", config_.target_y}}},
             {"expected_oracle_before_request_ms", oracle_ms},
             {"geometry_hash", authored_geometry_hash(*displayed_authored_)},
             {"appearance_hash", authored_appearance_hash(*displayed_authored_)},
             {"field_builds", field_delta},
             {"region_builds", region_delta},
             {"field_build_ms", field_ && field_delta ? field_->stats().build_ms : 0},
             {"region_build_upload_ms", regions_ && region_delta ? regions_->build_upload_ms : 0},
             {"field_bytes", field_ ? field_->stats().allocation_bytes : 0},
             {"region_bytes", regions_ ? regions_->allocation_bytes : 0},
             {"region_scratch_peak_bytes", regions_ ? regions_->scratch_peak_bytes : 0},
             {"region_references", regions_ ? regions_->reference_count : 0},
             {"compile_cache_publish_ms",
              std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                  .count()}});
        pending_study_action_ = start;
    }
    void draw_ui() {
        if (config_.timeline.motion == "material-story" || config_.lighting_cycle != "frozen")
            ImGui::SetNextWindowCollapsed(true, ImGuiCond_Once);
        ImGui::SetNextWindowSize(config_.workshop.empty() ? ImVec2{340, 0} : ImVec2{420, 640},
                                 ImGuiCond_FirstUseEver);
        if (ImGui::Begin("SDF study")) {
            const auto active_label = config_.named_render_mode
                                          ? render_mode_label(config_.effective_render_mode)
                                          : render_mode_label("custom");
            if (ImGui::BeginCombo("Rendering", std::string(active_label).c_str())) {
                for (const auto mode : render_modes) {
                    const auto reason =
                        render_mode_unavailable(config_, mode, authored_.has_value());
                    ImGui::BeginDisabled(!reason.empty());
                    if (ImGui::Selectable(std::string(render_mode_label(mode)).c_str(),
                                          config_.named_render_mode && config_.render_mode == mode))
                        requested_render_mode_ = mode;
                    ImGui::EndDisabled();
                    if (!reason.empty() &&
                        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("%s", reason.c_str());
                }
                ImGui::EndCombo();
            }
            const char* sample_choices[]{"1x", "2x"};
            int sample_index = static_cast<int>(config_.supersample) - 1;
            if (ImGui::Combo("Supersampling", &sample_index, sample_choices, 2))
                requested_supersample_ = static_cast<std::uint32_t>(sample_index + 1);
            if (!config_.workshop.empty()) {
                const char* specimens[]{"sphere", "block", "csg", "swatch", "cut-block"};
                int shape = config_.workshop == "sphere"      ? 0
                            : config_.workshop == "block"     ? 1
                            : config_.workshop == "swatch"    ? 3
                            : config_.workshop == "cut-block" ? 4
                                                              : 2;
                if (ImGui::Combo("Workshop specimen", &shape, specimens, 5)) {
                    config_.workshop = specimens[shape];
                    workshop_changed_ = true;
                }
                if (ImGui::BeginCombo("Family", family_filter_.c_str())) {
                    std::vector<std::string> families{"all"};
                    for (const auto& e : catalog_)
                        if (std::find(families.begin(), families.end(), e.family) == families.end())
                            families.push_back(e.family);
                    for (const auto& family : families)
                        if (ImGui::Selectable(family.c_str(), family_filter_ == family))
                            family_filter_ = family;
                    ImGui::EndCombo();
                }
                if (ImGui::BeginCombo("Shared recipe", config_.workshop_material.c_str())) {
                    for (const auto& e : catalog_) {
                        if (family_filter_ != "all" && e.family != family_filter_)
                            continue;
                        if (ImGui::Selectable(e.label.c_str(), config_.workshop_material == e.id)) {
                            config_.workshop_material = e.id;
                            config_.material_recipe_path.clear();
                            workshop_changed_ = true;
                            reload_workshop_material_ = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::Button("Reload library recipes")) {
                    try {
                        auto candidate = asset::load_material_catalog(
                            std::filesystem::path(CUBEY_MATERIAL_RECIPE_DIR) / "catalog.json");
                        if (std::none_of(candidate.begin(), candidate.end(), [&](const auto& e) {
                                return e.id == config_.workshop_material;
                            }))
                            throw std::runtime_error(
                                "Active material is missing from the candidate library.");
                        catalog_ = std::move(candidate);
                        config_.material_recipe_path.clear();
                        family_filter_ = "all";
                        workshop_changed_ = true;
                        reload_workshop_material_ = true;
                        error_.clear();
                    } catch (const std::exception& e) {
                        error_ = e.what();
                    }
                }
                workshop_changed_ |=
                    ImGui::Checkbox("Contrasting cut material", &config_.workshop_cut_material);
                workshop_changed_ |=
                    ImGui::Checkbox("Smooth material blending", &config_.workshop_blend_materials);
                if (ImGui::Button("Reset workshop material")) {
                    config_.material_recipe_path.clear();
                    workshop_changed_ = true;
                    reload_workshop_material_ = true;
                }
                ImGui::InputText("Recipe file", workshop_recipe_path_,
                                 sizeof(workshop_recipe_path_));
                if (ImGui::Button("Save shared recipe")) {
                    try {
                        asset::save_material_recipe(workshop_recipe_path_,
                                                    workshop_material_recipe(config_, *authored_));
                        error_.clear();
                    } catch (const std::exception& e) {
                        error_ = e.what();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Load shared recipe")) {
                    config_.material_recipe_path = workshop_recipe_path_;
                    workshop_changed_ = true;
                    reload_workshop_material_ = true;
                }
                ImGui::TextWrapped(
                    "Patterns use common material coordinates across fusion and cuts. Glass "
                    "measures interface segments; the mesh workshop estimates thickness.");
            }
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
            std::vector<const char*> motions{"Static", "Orbit"};
            std::vector<std::string> motion_names{"static", "orbit"};
            if (!authored_ || !authored_->history.empty()) {
                motions.insert(motions.end(), {"Construction", "Presentation"});
                motion_names.insert(motion_names.end(), {"construction", "presentation"});
            }
            if (!authored_) {
                motions.push_back("Carve");
                motion_names.push_back("carve");
            } else if (authored_->history.size() == 2 && authored_->history[0].cutters.empty() &&
                       !authored_->history[1].cutters.empty()) {
                motions.push_back("Material story");
                motion_names.push_back("material-story");
            }
            int motion = 0;
            for (int i = 0; i < static_cast<int>(motion_names.size()); ++i)
                if (config_.timeline.motion == motion_names[i])
                    motion = i;
            if (ImGui::Combo("Motion", &motion, motions.data(), static_cast<int>(motions.size()))) {
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
                const int minimum_step = config_.timeline.motion == "material-story" ? 1 : 0;
                ImGui::BeginDisabled(shown <= static_cast<std::size_t>(minimum_step));
                if (ImGui::Button("Previous edit"))
                    seek(shown - 1);
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(shown == total);
                if (ImGui::Button("Next edit"))
                    seek(shown + 1);
                ImGui::EndDisabled();
                int step = static_cast<int>(shown);
                if (ImGui::SliderInt("Edit", &step, minimum_step, static_cast<int>(total)))
                    seek(static_cast<std::size_t>(step));
            }
            int view = static_cast<int>(config_.debug);
            const char* views[]{"Shaded",          "Normals",        "Ray distance",
                                "Traversal work",  "Brick cells",    "Part identity",
                                "Base color",      "Roughness",      "Surface normals",
                                "Glass transport", "Opaque diffuse", "Opaque reflections",
                                "Metalness"};
            const bool glass =
                authored_ && std::any_of(authored_->optics.begin(), authored_->optics.end(),
                                         [](const auto& o) { return o.settings.y > 0; });
            const int view_count =
                config_.lighting != "atmosphere" ? (glass ? 10 : 13) : (authored_ ? 9 : 5);
            if (ImGui::BeginCombo("View", views[view])) {
                for (int i = 0; i < view_count; ++i) {
                    const bool unavailable = i == 4 && config_.surface_trace != "sampled";
                    ImGui::BeginDisabled(unavailable);
                    if (ImGui::Selectable(views[i], view == i))
                        config_.debug = static_cast<std::uint32_t>(i);
                    ImGui::EndDisabled();
                    if (unavailable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("Brick debug requires a cached rendering mode");
                }
                ImGui::EndCombo();
            }
            if (glass) {
                int budget =
                    config_.glass_crossings == 4 ? 0 : (config_.glass_crossings == 8 ? 1 : 2);
                const char* budgets[]{"4 interfaces", "8 interfaces", "16 interfaces"};
                if (ImGui::Combo("Glass budget", &budget, budgets, 3))
                    config_.glass_crossings = 4u << budget;
            }
            int shadows = static_cast<int>(config_.shadows);
            const char* shadow_names[]{"Off", "Hard", "Soft (4 samples)"};
            if (ImGui::Combo("Shadows", &shadows, shadow_names, 3))
                config_.shadows = static_cast<std::uint32_t>(shadows);
            ImGui::Checkbox("Local ambient occlusion", &config_.ao);
            ImGui::Checkbox("Neutral inspection", &config_.clay);
            ImGui::BeginDisabled(authored_ && !authored_build());
            ImGui::Checkbox(authored_ ? "Cut preview" : "Cutter bounds cue", &config_.cutter_cue);
            ImGui::EndDisabled();
            if (config_.lighting == "material-study" || config_.lighting == "material-furnace")
                ImGui::TextUnformatted("Fixed generated study environment");
            if (config_.lighting == "material-atmosphere")
                ImGui::TextUnformatted("Shared atmosphere / reflection probe");
            ImGui::BeginDisabled(config_.lighting == "material-study" ||
                                 config_.lighting == "material-furnace");
            ImGui::SliderFloat("Sun elevation", &config_.sun_elevation, -6, 85, "%.0f deg");
            ImGui::SliderFloat("Sun azimuth", &config_.sun_azimuth, -180, 180, "%.0f deg");
            ImGui::EndDisabled();
            ImGui::SliderFloat("Exposure", &config_.exposure, -3, 3, "%.1f stops");
            ImGui::BeginDisabled(authored_ && !config_.clay);
            ImGui::SliderFloat(authored_ ? "Inspection roughness" : "Roughness", &config_.roughness,
                               0.05F, 1);
            ImGui::EndDisabled();
            ImGui::SliderFloat("Focus height", &config_.target_y, -0.3F, 0.3F);
            ImGui::SliderFloat("Shadow softness", &config_.shadow_softness, 0, 0.15F);
            ImGui::Separator();
            if (authored_) {
                if (!config_.authored_path.empty() && ImGui::Button("Reload authored scene"))
                    reload_authored_ = true;
                if (!config_.export_authored_path.empty() && ImGui::Button("Export authored scene"))
                    save_authored_scene(config_.export_authored_path, *authored_);
                for (std::size_t i = 0; i < authored_->materials.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));
                    auto& material = authored_->materials[i];
                    if (ImGui::TreeNode(authored_->material_names[i].c_str())) {
                        if (authored_->optics[i].settings.y > 0)
                            ImGui::TextUnformatted(
                                "Bounded glass: environment-only, selected crossing budget");
                        ImGui::BeginDisabled(authored_->optics[i].settings.y > 0);
                        materials_changed_ |=
                            ImGui::ColorEdit3("Base color", &material.base_roughness.x);
                        ImGui::BeginDisabled(!authored_->libraries.empty() &&
                                             authored_->libraries[i].settings.x == 2);
                        materials_changed_ |=
                            ImGui::SliderFloat("Roughness", &material.base_roughness.w, .05F, 1);
                        ImGui::EndDisabled();
                        materials_changed_ |=
                            ImGui::ColorEdit3("Emission", &material.emission.x,
                                              ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
                        for (int channel = 0; channel < 3; ++channel)
                            material.emission[channel] =
                                std::clamp(material.emission[channel], 0.0F, 32.0F);
                        ImGui::EndDisabled();
                        auto& optics = authored_->optics[i];
                        ImGui::BeginDisabled(optics.settings.y > 0);
                        materials_changed_ |=
                            ImGui::SliderFloat("Diffuse roughness", &optics.attenuation.w, 0, 1);
                        ImGui::EndDisabled();
                        if (optics.settings.y > 0) {
                            materials_changed_ |=
                                ImGui::SliderFloat("IOR", &optics.settings.z, 1, 2.5F);
                            materials_changed_ |=
                                ImGui::ColorEdit3("Attenuation color", &optics.attenuation.x);
                            for (int k = 0; k < 3; ++k)
                                optics.attenuation[k] = std::max(.001F, optics.attenuation[k]);
                            materials_changed_ |= ImGui::SliderFloat("Attenuation distance",
                                                                     &optics.settings.w, .01F, 4);
                        }
                        if (!authored_->libraries.empty()) {
                            auto& library = authored_->libraries[i];
                            if (library.optical_options.x > .5F)
                                materials_changed_ |= ImGui::SliderFloat(
                                    "Frost roughness", &material.base_roughness.w, .05F, .8F);

                            if (library.settings.x > 0) {
                                ImGui::Checkbox("Surface pattern", &config_.procedural);
                                ImGui::Checkbox("Bump detail", &config_.bump);
                                int seed = static_cast<int>(library.settings.y);
                                if (ImGui::SliderInt("Library seed", &seed, 0, 65535)) {
                                    library.settings.y = static_cast<float>(seed);
                                    materials_changed_ = true;
                                }
                                materials_changed_ |= ImGui::SliderFloat(
                                    "Library scale", &library.settings.z, .1F, 16);
                                materials_changed_ |= ImGui::SliderFloat(
                                    "Relief height", &library.settings.w, 0, .005F, "%.5f");
                                materials_changed_ |= ImGui::ColorEdit3("Library secondary color",
                                                                        &library.secondary.x);
                                if (library.settings.x == 1) {
                                    materials_changed_ |= ImGui::SliderFloat(
                                        "Ring frequency", &library.wood.ring_frequency, 1, 100);
                                    materials_changed_ |= ImGui::SliderFloat(
                                        "Growth distortion", &library.wood.distortion, 0, 2);
                                    materials_changed_ |= ImGui::SliderFloat(
                                        "Fiber frequency", &library.wood.grain_frequency, 20, 600);
                                    materials_changed_ |= ImGui::SliderFloat(
                                        "Pore strength", &library.wood.pore_strength, 0, 1);
                                } else if (library.settings.x == 2) {
                                    materials_changed_ |= ImGui::SliderFloat(
                                        "Rust coverage", &library.rust.coverage, 0, 1);
                                    materials_changed_ |= ImGui::SliderFloat(
                                        "Patch frequency", &library.rust.patch_frequency, 1, 64);
                                    materials_changed_ |= ImGui::SliderFloat(
                                        "Edge breakup", &library.rust.edge_breakup, 0, 1);
                                    materials_changed_ |= ImGui::SliderFloat(
                                        "Pitting", &library.rust.pit_strength, 0, 1);
                                    materials_changed_ |= ImGui::SliderFloat(
                                        "Steel roughness", &library.rust_roughness.x, .05F, 1);
                                    materials_changed_ |= ImGui::SliderFloat(
                                        "Oxide roughness", &library.rust_roughness.y, .05F, 1);
                                } else if (library.settings.x == 3) {
                                    materials_changed_ |= ImGui::SliderFloat(
                                        "Yarn spacing", &library.weave.yarn_spacing, .003F, .06F,
                                        "%.4f");
                                    materials_changed_ |= ImGui::SliderFloat(
                                        "Yarn width", &library.weave.yarn_width, .3F, .95F);
                                    materials_changed_ |= ImGui::SliderFloat(
                                        "Yarn irregularity", &library.weave.irregularity, 0, .5F);
                                    materials_changed_ |= ImGui::SliderFloat(
                                        "Fine fibers", &library.weave.fiber_strength, 0, 1);
                                    materials_changed_ |=
                                        ImGui::ColorEdit3("Sheen color", &library.sheen.x);
                                    materials_changed_ |= ImGui::SliderFloat(
                                        "Sheen roughness", &library.sheen.w, .05F, 1);
                                    ImGui::TextWrapped("Weave follows the material X/Y axes. Use "
                                                       "the swatch to inspect its structure.");
                                } else {
                                    materials_changed_ |= ImGui::SliderFloat(
                                        "Fiber frequency", &library.paper.fiber_frequency, 40,
                                        1000);
                                    materials_changed_ |=
                                        ImGui::SliderFloat("Fiber directionality",
                                                           &library.paper.directionality, 0, 1);
                                    materials_changed_ |= ImGui::SliderFloat(
                                        "Pulp flecks", &library.paper.fleck_strength, 0, 1);
                                    materials_changed_ |= ImGui::SliderFloat(
                                        "Fiber relief", &library.paper.fiber_strength, 0, 1);
                                }
                            }
                        }
                        auto& pattern = authored_->patterns[i];
                        if (pattern.settings.x > 0) {
                            ImGui::Text("Procedural: %s",
                                        pattern.settings.x == 1
                                            ? "Limestone"
                                            : (pattern.settings.x == 2 ? "Marble" : "Granular"));
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
                const auto stats = field_ ? field_->stats() : FieldStats{};
                ImGui::Text("Bricks: %u / %u", stats.brick_count, field_ ? field_->capacity : 0);
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
        if (!study_actions_.is_null()) {
            finish_study_action();
            std::ofstream output(config_.comparison_output);
            if (!output)
                throw std::runtime_error("cannot write action study receipt");
            output
                << nlohmann::json{{"format", "cubey.sdf.action-results"},
                                  {"version", 1},
                                  {"trace", config_.surface_trace},
                                  {"csg", config_.csg_evaluation},
                                  {"region_axis", config_.region_axis},
                                  {"completion_scope",
                                   "request to queue-idle observation after first rendered GPU "
                                   "frame; serialized upper bound, not physical display latency"},
                                  {"actions", study_receipt_}}
                       .dump(2)
                << '\n';
        }
        renderer_->destroy();
        field_.reset();
        regions_.reset();
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
    std::unique_ptr<GpuRegionPrograms> regions_;
    std::uint64_t region_builds_ = 0;
    nlohmann::json study_actions_, study_receipt_;
    std::optional<ProjectGpuReadbackTicket> study_readback_;
    std::string study_fault_;
    std::optional<std::chrono::steady_clock::time_point> pending_study_action_;
    bool geometry_initialized_ = false;
    std::unique_ptr<Renderer> renderer_ = std::make_unique<Renderer>();
    vulkan::GpuRuntime* gpu_ = nullptr;
    VkExtent2D target_extent_{};
    VkFormat target_format_ = VK_FORMAT_UNDEFINED;
    std::uint32_t frame_slots_ = 0;
    std::uint64_t mode_generation_ = 0, field_generation_ = 0;
    double mode_switch_ms_ = 0;
    std::string requested_render_mode_;
    std::optional<std::uint32_t> requested_supersample_;
    TimelineSample timeline_;
    std::size_t active_prefix_ = std::numeric_limits<std::size_t>::max();
    double playback_seconds_ = 0;
    bool regenerate_requested_ = false;
    bool reload_authored_ = false, materials_changed_ = false, workshop_changed_ = false;
    bool reload_workshop_material_ = false;
    std::vector<asset::MaterialCatalogEntry> catalog_;
    std::string family_filter_ = "all";
    char workshop_recipe_path_[512] = "outputs/sdf-material-recipe.json";
    std::string error_;
};
} // namespace
int run_sdf(const SdfConfig& config) {
    if (!config.named_render_mode)
        validate_source_optimization(config);
    if ((!config.comparison_input.empty() && !config.comparison_actions.empty()) ||
        (config.comparison_donor && config.comparison_input.empty()) ||
        (!config.comparison_output.empty() && config.comparison_input.empty() &&
         config.comparison_actions.empty()))
        throw std::invalid_argument(
            "select exactly one tracing query or action study for its receipt");
    if (!config.comparison_input.empty() && config.named_render_mode)
        throw std::invalid_argument("tracing fixtures require explicit low-level tracing controls");
    if (!config.comparison_input.empty())
        return run_tracing_comparison(config);
    if (!config.workshop.empty() && (!config.authored_path.empty() || !config.recipe_path.empty()))
        throw std::invalid_argument("workshop selects its own specimen");
    return App(config).run();
}
} // namespace cubey::projects::sdf
