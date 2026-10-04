#pragma once

#include "../sim/fluid_25d/fluid_25d_config.h"
#include "../sim/fluid_25d/fluid_25d_presentation.h"

#include <cubey/host/common_config.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {

struct Fluid25DTerrainOptions {
    std::optional<std::filesystem::path> heightfield_path{};
    std::optional<std::uint32_t> crop_x{};
    std::optional<std::uint32_t> crop_z{};
};

struct Fluid25DProjectConfig {
    host::CommonRunConfig common{};
    common::FluidGridOptions grid{};
    std::string view{};
    std::string catchment_view{};
    std::string debug_view{};
    float presentation_time_scale = kFluid25DDefaultWindowedPresentationTimeScale;
    bool gpu_oracle_validation = false;
    bool mass_audit = false;
    std::string mass_audit_control{};
    Fluid25DStartupOptions fluid{};
    Fluid25DTerrainOptions terrain{};
    std::optional<std::filesystem::path> natural_flow_recipe_path{};
    std::optional<float> natural_flow_home_pitch_radians{};
    std::optional<float> hillside_inspection_advance_seconds{};
    std::optional<float> hillside_advance_and_continue_seconds{};
    std::string hillside_camera{};
    bool motion_markers = false;
    std::string motion_marker_mode = "source";
    bool hillside_supply_response = false;
    bool hillside_supply_gpu_controls = false;
    bool rain_study_gpu_controls = false;
    bool motion_marker_gpu_controls = false;
    bool hillside_source_context = false;
    // Backend selection is independent of the built-in numerical method.
    // Omitted preserves the existing recording/stream flags and builtin default.
    std::optional<std::string> backend{};
    // An external recording is a presentation data source, never a solver selection.
    std::optional<std::filesystem::path> recording_path{};
    std::optional<std::filesystem::path> stream_path{};
    bool stream_follow_latest = false;
    float recording_speed = 60.0F;
    float recording_time_seconds = 0.0F;
    float recording_frame_interval_seconds = 60.0F;
    std::string recording_camera = "overview";
    bool recording_gpu_validation = false;
    Fluid25DCatchmentRenderOptions catchment_render{};
    Fluid25DConfig simulation{};
};

[[nodiscard]] inline std::string_view
fluid_25d_selected_backend(const Fluid25DProjectConfig& config) {
    const std::string_view inferred = config.recording_path ? "recording"
                                      : config.stream_path  ? "external"
                                                            : "builtin";
    if (config.backend && *config.backend != inferred) {
        throw std::runtime_error(
            "fluid 2.5D backend conflicts with the supplied data source: builtin needs no "
            "recording/stream, recording needs --fluid25d-recording, external needs "
            "--fluid25d-stream");
    }
    return inferred;
}

namespace fluid_25d_project_config_detail {

inline config::OptionSpec option(std::string path, std::string cli, std::string label,
                                 std::string help, config::ValueType type, config::Range range = {},
                                 std::vector<std::string> choices = {}) {
    return {.path = std::move(path),
            .cli_name = std::move(cli),
            .negative_cli_name = {},
            .label = std::move(label),
            .group_path = "Fluid 2.5D",
            .help = std::move(help),
            .type = type,
            .range = range,
            .enum_values = std::move(choices)};
}

} // namespace fluid_25d_project_config_detail

inline void validate_fluid_25d_project_config(const Fluid25DProjectConfig& project_config) {
    if (!project_config.mass_audit_control.empty() && project_config.mass_audit_control != "none" &&
        (!project_config.common.headless ||
         project_config.simulation.solver != Fluid25DSolver::FiniteVolume ||
         project_config.simulation.scenario != Fluid25DScenario::DryBed)) {
        throw std::runtime_error(
            "fluid 2.5D mass audit controls require headless finite-volume dry-bed scenario");
    }
    if (project_config.mass_audit &&
        (!project_config.common.headless || !project_config.common.profile_diagnostics ||
         project_config.simulation.solver != Fluid25DSolver::FiniteVolume)) {
        throw std::runtime_error(
            "fluid 2.5D mass audit requires headless finite-volume profile diagnostics");
    }
    validate_fluid_25d_windowed_presentation_time_scale(project_config.presentation_time_scale);
    if (project_config.fluid.headwaters_source_scale.has_value() &&
        project_config.simulation.scenario != Fluid25DScenario::SustainedHeadwatersDemo) {
        throw std::runtime_error("fluid 2.5D --fluid25d-headwaters-source-scale requires "
                                 "--fluid25d-scenario sustained-headwaters-demo");
    }
    const bool natural_flow_study =
        fluid_25d_is_natural_terrain_study(project_config.simulation.scenario);
    const bool rain_study =
        project_config.simulation.scenario == Fluid25DScenario::HillsideRainStudy;
    const bool macro_hillside_study =
        fluid_25d_is_macro_hillside_study(project_config.simulation.scenario);
    if (project_config.motion_markers &&
        (!macro_hillside_study || project_config.simulation.solver != Fluid25DSolver::FiniteVolume))
        throw std::runtime_error("motion markers require the finite-volume hillside study");
    if (project_config.motion_marker_gpu_controls && !project_config.common.headless)
        throw std::runtime_error("motion marker GPU controls are headless only");
    if ((project_config.motion_marker_mode != "source" &&
         project_config.motion_marker_mode != "local") ||
        (project_config.motion_marker_mode == "local" && !project_config.motion_markers))
        throw std::runtime_error("local marker mode requires motion markers");
    if (rain_study && project_config.motion_markers &&
        project_config.motion_marker_mode != "local") {
        throw std::runtime_error("hillside-rain-study motion markers require local marker mode");
    }
    if (project_config.hillside_supply_response &&
        (project_config.simulation.scenario != Fluid25DScenario::HillsideFlowStudy ||
         project_config.simulation.solver != Fluid25DSolver::FiniteVolume ||
         project_config.simulation.natural_flow_source_m3_per_s != 100.0F))
        throw std::runtime_error("hillside supply response requires finite-volume hillside Q100");
    if (project_config.hillside_supply_gpu_controls &&
        (!project_config.common.headless || !project_config.gpu_oracle_validation ||
         project_config.common.frames != 4U ||
         project_config.simulation.scenario != Fluid25DScenario::DryBed ||
         project_config.simulation.solver != Fluid25DSolver::FiniteVolume))
        throw std::runtime_error("supply GPU controls require four headless oracle dry-bed steps");
    if (project_config.rain_study_gpu_controls &&
        (!project_config.common.headless || !project_config.gpu_oracle_validation ||
         project_config.common.frames != 4U || !rain_study ||
         project_config.simulation.grid_width != 8U ||
         project_config.simulation.grid_height != 8U ||
         project_config.simulation.cell_size_m != kFluid25DHillsideRainStudyCellSizeM ||
         project_config.simulation.solver != Fluid25DSolver::FiniteVolume ||
         project_config.terrain.heightfield_path.has_value() ||
         project_config.terrain.crop_x.has_value() || project_config.terrain.crop_z.has_value())) {
        throw std::runtime_error(
            "rain-study GPU controls require four headless finite-volume oracle steps on an 8x8 "
            "native-spacing rain study");
    }
    if (project_config.catchment_render.hillside_depth_cues && !macro_hillside_study)
        throw std::runtime_error("hillside depth cues require hillside study");
    const bool valid_hillside_camera = rain_study
                                           ? (project_config.hillside_camera == "travel" ||
                                              project_config.hillside_camera == "collection" ||
                                              project_config.hillside_camera == "overview")
                                           : (project_config.hillside_camera == "source" ||
                                              project_config.hillside_camera == "branch" ||
                                              project_config.hillside_camera == "travel" ||
                                              project_config.hillside_camera == "collection" ||
                                              project_config.hillside_camera == "overview");
    if (!project_config.hillside_camera.empty() &&
        (!macro_hillside_study || !valid_hillside_camera))
        throw std::runtime_error(
            "hillside camera requires source, branch, travel, collection or overview");
    if ((project_config.hillside_camera == "travel" ||
         project_config.hillside_camera == "collection") &&
        (project_config.simulation.grid_width != 512U ||
         project_config.simulation.grid_height != 512U))
        throw std::runtime_error("downstream hillside cameras require the 512x512 study crop");
    if (project_config.hillside_source_context &&
        project_config.simulation.scenario != Fluid25DScenario::HillsideFlowStudy) {
        throw std::runtime_error("source-context camera requires hillside-flow-study");
    }
    if (project_config.hillside_inspection_advance_seconds.has_value()) {
        if (project_config.common.headless || !macro_hillside_study) {
            throw std::runtime_error("inspection advance is windowed hillside-flow-study only");
        }
        Fluid25DInspectionAdvance advance;
        advance.request(*project_config.hillside_inspection_advance_seconds,
                        project_config.simulation.fixed_delta_seconds);
    }
    if (project_config.hillside_advance_and_continue_seconds.has_value()) {
        if (project_config.common.headless || !macro_hillside_study ||
            project_config.hillside_inspection_advance_seconds.has_value())
            throw std::runtime_error(
                "advance and continue is windowed hillside only and excludes pause advance");
        Fluid25DInspectionAdvance advance;
        advance.request(*project_config.hillside_advance_and_continue_seconds,
                        project_config.simulation.fixed_delta_seconds);
    }
    if (project_config.natural_flow_recipe_path.has_value() != natural_flow_study ||
        (project_config.natural_flow_recipe_path.has_value() &&
         project_config.natural_flow_recipe_path->empty())) {
        throw std::runtime_error(
            "fluid 2.5D --fluid25d-natural-flow-recipe requires natural-flow-study and must be "
            "a non-empty path");
    }
    if (project_config.fluid.natural_flow_source_m3_per_s.has_value() != natural_flow_study) {
        throw std::runtime_error(
            "fluid 2.5D --fluid25d-natural-flow-source-m3-per-s requires natural-flow-study and "
            "must be supplied explicitly");
    }
    if (project_config.natural_flow_home_pitch_radians.has_value() &&
        ((!natural_flow_study && !rain_study) ||
         !std::isfinite(*project_config.natural_flow_home_pitch_radians) ||
         *project_config.natural_flow_home_pitch_radians < -1.55F ||
         *project_config.natural_flow_home_pitch_radians > -0.1F)) {
        throw std::runtime_error(
            "fluid 2.5D natural-flow home pitch requires natural-flow-study or "
            "hillside-rain-study and a finite value in [-1.55,-0.1]");
    }
    if (natural_flow_study && (!project_config.terrain.crop_x.has_value() ||
                               !project_config.terrain.crop_z.has_value())) {
        throw std::runtime_error("fluid 2.5D natural-flow-study requires explicit terrain crop x "
                                 "and z matching its recipe");
    }
    const Fluid25DCatchmentView catchment_view =
        fluid_25d_catchment_view_from_name(project_config.catchment_view);
    if (catchment_view == Fluid25DCatchmentView::TransportInspection &&
        !fluid_25d_transport_inspection_available(project_config.simulation)) {
        throw std::runtime_error(
            "fluid 2.5D transport-inspection requires an eligible finite-volume scenario and a "
            "positive dye pulse duration");
    }
    const bool terrain_case = project_config.simulation.scenario == Fluid25DScenario::TerrainCase;
    const Fluid25DCatchmentRenderOptions& render = project_config.catchment_render;
    const bool has_render_override =
        render.terrain_palette_low_m.has_value() || render.terrain_palette_high_m.has_value() ||
        render.terrain_height_scale.has_value() || render.home_camera_distance_m.has_value() ||
        render.terrain_thin_water_composite ||
        project_config.natural_flow_home_pitch_radians.has_value();
    if (has_render_override && !terrain_case && !natural_flow_study && !rain_study) {
        throw std::runtime_error(
            "fluid 2.5D terrain presentation overrides require terrain-case, a natural-flow study, "
            "or hillside-rain-study");
    }
    if (has_render_override && project_config.view == "diagnostics") {
        throw std::runtime_error(
            "fluid 2.5D terrain presentation overrides require the catchment view");
    }
    if (render.terrain_thin_water_composite && catchment_view != Fluid25DCatchmentView::Composite) {
        throw std::runtime_error(
            "fluid 2.5D thin-water composite requires the Composite catchment view");
    }
    if (render.terrain_palette_low_m.has_value() != render.terrain_palette_high_m.has_value()) {
        throw std::runtime_error(
            "fluid 2.5D terrain palette requires both low and high physical elevation bounds");
    }
    if (render.terrain_palette_low_m.has_value() &&
        (!std::isfinite(*render.terrain_palette_low_m) ||
         !std::isfinite(*render.terrain_palette_high_m) ||
         !(*render.terrain_palette_low_m < *render.terrain_palette_high_m))) {
        throw std::runtime_error(
            "fluid 2.5D terrain palette physical bounds must be finite and low < high");
    }
    if (render.terrain_height_scale.has_value() &&
        (!std::isfinite(*render.terrain_height_scale) ||
         *render.terrain_height_scale < kFluid25DMinTerrainCaseRenderHeightScale ||
         *render.terrain_height_scale > kFluid25DMaxTerrainCaseRenderHeightScale)) {
        throw std::runtime_error(
            "fluid 2.5D terrain render height scale must be finite and within 0.001..2.0");
    }
    if (render.home_camera_distance_m.has_value() &&
        (!std::isfinite(*render.home_camera_distance_m) ||
         *render.home_camera_distance_m <= 0.0F)) {
        throw std::runtime_error(
            "fluid 2.5D terrain home camera distance must be finite and positive");
    }
    const bool mountain_source_outlet =
        project_config.simulation.scenario == Fluid25DScenario::MountainSourceOutletDemo;
    const bool terrain_backed =
        terrain_case || mountain_source_outlet || natural_flow_study || rain_study;
    const bool has_heightfield = project_config.terrain.heightfield_path.has_value();
    const bool has_nonempty_heightfield =
        has_heightfield && !project_config.terrain.heightfield_path->empty();
    if (terrain_backed && !has_nonempty_heightfield && !project_config.rain_study_gpu_controls) {
        throw std::runtime_error(
            "fluid 2.5D terrain-backed scenarios require a non-empty --terrain-heightfield path");
    }
    if (!terrain_backed && has_heightfield) {
        throw std::runtime_error(
            "fluid 2.5D --terrain-heightfield requires a terrain-backed scenario");
    }
    if (!terrain_case && !natural_flow_study && !rain_study &&
        (project_config.terrain.crop_x.has_value() || project_config.terrain.crop_z.has_value())) {
        throw std::runtime_error(
            "fluid 2.5D terrain crop options require a terrain-backed crop scenario");
    }

    const bool has_explicit_terrain_water_option =
        project_config.fluid.terrain_water_protocol.has_value() ||
        project_config.fluid.rainfall_rate_mm_per_hour.has_value() ||
        project_config.fluid.sheet_depth_m.has_value();
    const bool has_explicit_dye_option =
        project_config.fluid.dye_pulse_start_seconds.has_value() ||
        project_config.fluid.dye_pulse_duration_seconds.has_value();
    if (has_explicit_dye_option &&
        ((project_config.simulation.scenario != Fluid25DScenario::SourceOutletDemo &&
          project_config.simulation.scenario != Fluid25DScenario::SustainedHeadwatersDemo &&
          !natural_flow_study) ||
         project_config.simulation.solver != Fluid25DSolver::FiniteVolume)) {
        throw std::runtime_error(
            "fluid 2.5D dye pulse timing requires an eligible finite-volume scenario");
    }
    if (!terrain_case && !rain_study && has_explicit_terrain_water_option) {
        throw std::runtime_error(
            "fluid 2.5D terrain-water protocol options require --fluid25d-scenario terrain-case");
    }
    if (rain_study && (project_config.fluid.terrain_water_protocol.has_value() ||
                       project_config.fluid.sheet_depth_m.has_value())) {
        throw std::runtime_error(
            "fluid 2.5D hillside-rain-study accepts the rainfall rate only; terrain protocol and "
            "sheet options are not part of this study");
    }
    if (mountain_source_outlet && project_config.fluid.source_active_duration_seconds.has_value()) {
        throw std::runtime_error(
            "fluid 2.5D mountain-source-outlet-demo rejects terrain-water forcing options");
    }
    if (natural_flow_study && (project_config.fluid.terrain_water_protocol.has_value() ||
                               project_config.fluid.rainfall_rate_mm_per_hour.has_value() ||
                               project_config.fluid.sheet_depth_m.has_value() ||
                               project_config.fluid.source_active_duration_seconds.has_value() ||
                               project_config.fluid.headwaters_source_scale.has_value())) {
        throw std::runtime_error(
            "fluid 2.5D natural-flow-study rejects terrain-water protocol, rain, sheet, "
            "source-duration, and headwaters-source-scale options, including explicit neutral "
            "values");
    }
    if (rain_study) {
        if (!project_config.rain_study_gpu_controls &&
            !project_config.terrain.heightfield_path.has_value()) {
            throw std::runtime_error(
                "fluid 2.5D hillside-rain-study requires --terrain-heightfield");
        }
        if (!project_config.fluid.rainfall_rate_mm_per_hour.has_value() ||
            !(project_config.simulation.rainfall_depth_rate_m_per_s > 0.0F)) {
            throw std::runtime_error(
                "fluid 2.5D hillside-rain-study requires a positive rainfall rate");
        }
        if (!project_config.rain_study_gpu_controls) {
            return;
        }
    }
    if (!terrain_case) {
        return;
    }

    const Fluid25DTerrainWaterProtocol protocol = project_config.simulation.terrain_water_protocol;
    const bool has_rainfall_rate = project_config.fluid.rainfall_rate_mm_per_hour.has_value();
    const bool has_sheet_depth = project_config.fluid.sheet_depth_m.has_value();
    const bool has_source_duration =
        project_config.fluid.source_active_duration_seconds.has_value();
    switch (protocol) {
    case Fluid25DTerrainWaterProtocol::None:
        if (has_rainfall_rate || has_sheet_depth || has_source_duration) {
            throw std::runtime_error(
                "fluid 2.5D terrain-case protocol none rejects forcing parameters");
        }
        break;
    case Fluid25DTerrainWaterProtocol::RainPulse:
        if (!has_rainfall_rate || !(project_config.simulation.rainfall_depth_rate_m_per_s > 0.0F) ||
            has_sheet_depth || !has_source_duration ||
            !(*project_config.simulation.source_active_duration_seconds > 0.0F)) {
            throw std::runtime_error(
                "fluid 2.5D rain-pulse requires positive rainfall and source-active duration, and "
                "rejects sheet depth");
        }
        break;
    case Fluid25DTerrainWaterProtocol::SheetRelease:
        if (!has_sheet_depth || !(project_config.simulation.sheet_initial_depth_m > 0.0F) ||
            has_rainfall_rate || has_source_duration) {
            throw std::runtime_error(
                "fluid 2.5D sheet-release requires positive sheet depth and rejects rain and "
                "source-active duration");
        }
        break;
    }
}

inline void resolve_fluid_25d_terrain_cell_size(Fluid25DProjectConfig& project_config,
                                                float source_spacing_m) {
    if (!std::isfinite(source_spacing_m) || source_spacing_m <= 0.0F) {
        throw std::runtime_error("fluid 2.5D terrain source has invalid sample spacing");
    }
    if (project_config.fluid.cell_size_m.has_value()) {
        const float requested_spacing_m = *project_config.fluid.cell_size_m;
        const float scale = std::max({1.0F, std::abs(requested_spacing_m), source_spacing_m});
        if (!std::isfinite(requested_spacing_m) || requested_spacing_m <= 0.0F ||
            std::abs(requested_spacing_m - source_spacing_m) > 1.0e-6F * scale) {
            throw std::runtime_error("fluid 2.5D terrain-backed scenario rejects "
                                     "--fluid25d-cell-size-m when it conflicts "
                                     "with the source sample spacing");
        }
    }
    // Always write the exact source value after the conflict check. This keeps
    // the imported scenario and every downstream GPU/CPU consumer canonical,
    // even when the explicit CLI value only differs by float roundoff.
    project_config.simulation.cell_size_m = source_spacing_m;
    validate_fluid_25d_config(project_config.simulation);
}

[[nodiscard]] inline config::Schema fluid_25d_project_config_schema(Fluid25DProjectConfig& config) {
    using config::ValueType;
    using fluid_25d_project_config_detail::option;

    auto builder = config::Schema::builder().compose(host::common_run_config_schema(config.common));
    builder.compose(common::fluid_grid_schema(config.grid, common::FluidGridSchemaMode::TwoD));
    builder
        .bind(option("fluid25d.backend", "--fluid25d-backend", "Water Backend",
                     "Select builtin, external, or recording; the builtin solver option remains "
                     "independent. Omitted preserves existing source-based selection.",
                     ValueType::Enum, {}, {"builtin", "external", "recording"}),
              config.backend)
        .bind(option("fluid25d.recording", "--fluid25d-recording", "Recording",
                     "Replay a portable native-state recording; no hydraulic solver runs.",
                     ValueType::Path),
              config.recording_path)
        .bind(option("fluid25d.stream", "--fluid25d-stream", "Live External Session",
                     "Watch atomic snapshots from a running external solver; no CUDA in Cubey.",
                     ValueType::Path),
              config.stream_path)
        .bind(option("fluid25d.stream_follow_latest", "--fluid25d-stream-follow-latest",
                     "Follow Latest",
                     "Follow the latest validated snapshot instead of paced viewing.",
                     ValueType::Bool),
              config.stream_follow_latest)
        .bind(option("fluid25d.recording_speed", "--fluid25d-recording-speed", "Playback Speed",
                     "Recorded physical seconds per wall second (windowed playback only).",
                     ValueType::Float,
                     {.has_min = true, .has_max = true, .min = 0.125, .max = 300.0}),
              config.recording_speed)
        .bind(option("fluid25d.recording_time_seconds", "--fluid25d-recording-time-seconds",
                     "Recording Time",
                     "Initial physical time; PNG captures hold this exact saved state.",
                     ValueType::Float, {.has_min = true, .min = 0.0}),
              config.recording_time_seconds)
        .bind(option("fluid25d.recording_frame_interval_seconds",
                     "--fluid25d-recording-frame-interval-seconds", "Recorded Video Interval",
                     "Physical seconds between headless video frames, beginning at recording time.",
                     ValueType::Float, {.has_min = true, .min = 0.0}),
              config.recording_frame_interval_seconds)
        .bind(option("fluid25d.recording_camera", "--fluid25d-recording-camera", "Recording Camera",
                     "Presentation-only overview or existing native runoff/collection observation.",
                     ValueType::Enum, {}, {"overview", "runoff", "collection"}),
              config.recording_camera)
        .bind(option("fluid25d.recording_gpu_validation", "--fluid25d-recording-gpu-validation",
                     "Recording Upload Validation",
                     "Headless bit-exact uploaded-field and presentation-immutability checks, not "
                     "a hydraulic oracle.",
                     ValueType::Bool),
              config.recording_gpu_validation)
        .bind(option("fluid25d.view", "--fluid25d-view", "View",
                     "Top-level River V0 surface: catchment or diagnostics.", ValueType::Enum, {},
                     {"catchment", "diagnostics"}),
              config.view)
        .bind(option("fluid25d.catchment_view", "--fluid25d-catchment-view", "Catchment View",
                     "Catchment presentation: composite, water-isolation, flow-inspection, or "
                     "transport-inspection (dye source/outlet or sustained-headwaters demo only).",
                     ValueType::Enum, {},
                     {"composite", "water-isolation", "flow-inspection", "transport-inspection"}),
              config.catchment_view)
        .bind(option("fluid25d.terrain_palette_low_m", "--fluid25d-terrain-palette-low-m",
                     "Terrain Palette Low",
                     "Terrain-case render-only physical elevation mapped to the lowland palette.",
                     ValueType::Float),
              config.catchment_render.terrain_palette_low_m)
        .bind(option("fluid25d.terrain_palette_high_m", "--fluid25d-terrain-palette-high-m",
                     "Terrain Palette High",
                     "Terrain-case render-only physical elevation mapped to the highland palette.",
                     ValueType::Float),
              config.catchment_render.terrain_palette_high_m)
        .bind(
            option(
                "fluid25d.terrain_height_scale", "--fluid25d-render-height-scale",
                "Terrain Render Height Scale",
                "Terrain-case render-only vertical scale; omitted preserves the scenario default.",
                ValueType::Float,
                {.has_min = true,
                 .has_max = true,
                 .min = static_cast<double>(kFluid25DMinTerrainCaseRenderHeightScale),
                 .max = static_cast<double>(kFluid25DMaxTerrainCaseRenderHeightScale)}),
            config.catchment_render.terrain_height_scale)
        .bind(option("fluid25d.home_camera_distance_m", "--fluid25d-home-camera-distance-m",
                     "Home Camera Distance",
                     "Terrain-case absolute render-only home camera distance in metres.",
                     ValueType::Float, {.has_min = true, .min = 1.0}),
              config.catchment_render.home_camera_distance_m)
        .bind(option("fluid25d.natural_flow_home_pitch_radians",
                     "--fluid25d-natural-flow-home-pitch-radians", "Natural Flow Home Pitch",
                     "Natural-flow or hillside-rain render-only home camera pitch in radians.",
                     ValueType::Float,
                     {.has_min = true, .has_max = true, .min = -1.55, .max = -0.1}),
              config.natural_flow_home_pitch_radians)
        .bind(option("fluid25d.hillside_inspection_advance_seconds",
                     "--fluid25d-hillside-inspection-advance-seconds", "Inspection Advance",
                     "Windowed hillside study: execute every fixed step then pause for inspection.",
                     ValueType::Float,
                     {.has_min = true, .has_max = true, .min = 0.001, .max = 7200.0}),
              config.hillside_inspection_advance_seconds)
        .bind(option("fluid25d.hillside_advance_and_continue_seconds",
                     "--fluid25d-hillside-advance-and-continue-seconds", "Advance and Continue",
                     "Compute normal fixed steps with progress, then resume continuous hillside "
                     "playback.",
                     ValueType::Float,
                     {.has_min = true, .has_max = true, .min = 0.001, .max = 7200.0}),
              config.hillside_advance_and_continue_seconds)
        .bind(option("fluid25d.hillside_camera", "--fluid25d-hillside-camera", "Hillside Camera",
                     "Render-only source, legacy branch, downhill travel, collection or overview.",
                     ValueType::Enum, {}, {"source", "branch", "travel", "collection", "overview"}),
              config.hillside_camera)
        .bind(option("fluid25d.motion_marker_mode", "--fluid25d-motion-marker-mode", "Marker Mode",
                     "Source-released travel or locally seeded movement indicators.",
                     ValueType::Enum, {}, {"source", "local"}),
              config.motion_marker_mode)
        .bind(option("fluid25d.hillside_supply_response", "--fluid25d-hillside-supply-response",
                     "Hillside Supply Response", "Continuous Q100/150/50/100 at 0/60/90/120 min.",
                     ValueType::Bool),
              config.hillside_supply_response)
        .bind(option("fluid25d.hillside_supply_gpu_controls",
                     "--fluid25d-hillside-supply-gpu-controls", "Supply GPU Controls",
                     "Synthetic four-step source sequence with CPU oracle.", ValueType::Bool),
              config.hillside_supply_gpu_controls)
        .bind(option("fluid25d.rain_study_gpu_controls", "--fluid25d-rain-study-gpu-controls",
                     "Rain Study GPU Controls",
                     "Synthetic four-step rain on/off sequence with CPU oracle.", ValueType::Bool),
              config.rain_study_gpu_controls)
        .bind(option("fluid25d.hillside_depth_cues", "--fluid25d-hillside-depth-cues",
                     "Hillside Depth Cues", "Fixed 0.01 to 10 m depth palette and wet-edge AA.",
                     ValueType::Bool),
              config.catchment_render.hillside_depth_cues)
        .bind(option("fluid25d.motion_markers", "--fluid25d-motion-markers", "Motion Markers",
                     "Opt-in source-released markers following hillside depth-averaged velocity.",
                     ValueType::Bool),
              config.motion_markers)
        .bind(option("fluid25d.motion_marker_gpu_controls", "--fluid25d-motion-marker-gpu-controls",
                     "Motion Marker GPU Controls",
                     "Headless synthetic validation of the marker shader.", ValueType::Bool),
              config.motion_marker_gpu_controls)
        .bind(option("fluid25d.hillside_source_context", "--fluid25d-hillside-source-context",
                     "Source Context Camera", "Render-only 3.2 km view around the hillside source.",
                     ValueType::Bool),
              config.hillside_source_context)
        .bind(option("fluid25d.terrain_thin_water_composite",
                     "--fluid25d-terrain-thin-water-composite", "Thin Water Composite",
                     "Terrain-case or hillside-rain Composite-only display attenuation for thin "
                     "water; solver state is unchanged.",
                     ValueType::Bool),
              config.catchment_render.terrain_thin_water_composite)
        .bind(option("fluid25d.presentation_time_scale", "--fluid25d-presentation-time-scale",
                     "Presentation Time Scale",
                     "Windowed-only simulation playback speed; 1, 4, and 8 are review-friendly.",
                     ValueType::Float,
                     {.has_min = true,
                      .has_max = true,
                      .min = static_cast<double>(kFluid25DMinWindowedPresentationTimeScale),
                      .max = static_cast<double>(kFluid25DMaxWindowedPresentationTimeScale)}),
              config.presentation_time_scale)
        .bind(option(
                  "fluid25d.debug_view", "--debug-view", "Debug View",
                  "Top-down diagnostic view: terrain, depth, surface, flow, direction, or wet-dry.",
                  ValueType::Enum, {},
                  {"terrain", "depth", "surface", "flow", "direction", "wet-dry"}),
              config.debug_view)
        .bind(
            option("fluid25d.gpu_oracle_validation", "--fluid25d-gpu-oracle-validation",
                   "GPU Oracle Validation",
                   "Headless-only opt-in CPU/GPU solver comparison with simulation-state readback.",
                   ValueType::Bool),
            config.gpu_oracle_validation)
        .bind(
            option(
                "fluid25d.mass_audit", "--fluid25d-mass-audit", "Mass Budget Audit",
                "Headless finite-volume committed-update mass audit; requires profile diagnostics.",
                ValueType::Bool),
            config.mass_audit)
        .bind(option("fluid25d.mass_audit_control", "--fluid25d-mass-audit-control",
                     "Mass Audit Control",
                     "Headless closed source basin or dam-break diagnostic fields.",
                     ValueType::Enum, {}, {"none", "source-basin", "dam-break"}),
              config.mass_audit_control)
        .bind(option("terrain.heightfield", "--terrain-heightfield", "Heightfield",
                     "Terrain heightfield manifest or directory used by a terrain-backed scenario.",
                     ValueType::Path),
              config.terrain.heightfield_path)
        .bind(option("fluid25d.natural_flow_recipe", "--fluid25d-natural-flow-recipe",
                     "Natural Flow Recipe",
                     "Frozen, hash-pinned native terrain recipe required by natural-flow-study.",
                     ValueType::Path),
              config.natural_flow_recipe_path)
        .bind(option("fluid25d.terrain_crop_x", "--fluid25d-terrain-crop-x", "Terrain Crop X",
                     "Native x sample index of the terrain-case crop origin.", ValueType::UInt32),
              config.terrain.crop_x)
        .bind(option("fluid25d.terrain_crop_z", "--fluid25d-terrain-crop-z", "Terrain Crop Z",
                     "Native z sample index of the terrain-case crop origin.", ValueType::UInt32),
              config.terrain.crop_z)
        .bind(option("fluid25d.terrain_water_protocol", "--fluid25d-terrain-water-protocol",
                     "Terrain-Water Protocol",
                     "Terrain-case-only neutral forcing: none, rain-pulse, or sheet-release.",
                     ValueType::Enum, {}, {"none", "rain-pulse", "sheet-release"}),
              config.fluid.terrain_water_protocol)
        .bind(option("fluid25d.rainfall_rate_mm_per_hour", "--fluid25d-rainfall-rate-mm-per-hour",
                     "Rainfall Rate",
                     "Terrain-case pulse or hillside-rain depth rate in "
                     "millimetres per hour.",
                     ValueType::Float, {.has_min = true, .min = 0.0}),
              config.fluid.rainfall_rate_mm_per_hour)
        .bind(option("fluid25d.sheet_depth_m", "--fluid25d-sheet-depth-m", "Sheet Depth",
                     "Terrain-case sheet-release initial water depth in metres.", ValueType::Float,
                     {.has_min = true, .min = 0.0}),
              config.fluid.sheet_depth_m)
        .bind(option("fluid25d.scenario", "--fluid25d-scenario", "Scenario",
                     "Deterministic River V0 fixture, opt-in finite-volume control/demo, or "
                     "terrain-backed scenario.",
                     ValueType::Enum, {},
                     {"dry-bed", "lake-at-rest", "river-catchment", "source-outlet-demo",
                      "mountain-source-outlet-demo", "sustained-headwaters-demo", "terrain-case",
                      "boundary-drain-fixture", "natural-flow-study", "hillside-flow-study",
                      "hillside-rain-study"}),
              config.fluid.scenario)
        .bind(
            option("fluid25d.solver", "--fluid25d-solver", "Solver",
                   "Opt-in numerical solver: virtual-pipes (default) or finite-volume comparison.",
                   ValueType::Enum, {}, {"virtual-pipes", "finite-volume"}),
            config.fluid.solver)
        .bind(option("fluid25d.cell_size_m", "--fluid25d-cell-size-m", "Cell Size",
                     "Simulation cell width in metres.", ValueType::Float,
                     {.has_min = true, .min = 0.000001}),
              config.fluid.cell_size_m)
        .bind(option("fluid25d.fixed_delta_seconds", "--fluid25d-fixed-delta-seconds",
                     "Fixed Delta", "Fixed simulation frame duration in seconds.", ValueType::Float,
                     {.has_min = true, .min = 0.000001}),
              config.fluid.fixed_delta_seconds)
        .bind(option("fluid25d.simulation_substeps", "--fluid25d-substeps", "Substeps",
                     "Fixed solver substeps per simulation frame.", ValueType::UInt32,
                     {.has_min = true,
                      .has_max = true,
                      .min = 1.0,
                      .max = static_cast<double>(kMaxFluid25DSubsteps)}),
              config.fluid.simulation_substeps)
        .bind(option("fluid25d.gravity_m_per_s2", "--fluid25d-gravity-m-per-s2", "Gravity",
                     "Gravitational acceleration in metres per second squared.", ValueType::Float,
                     {.has_min = true, .min = 0.000001}),
              config.fluid.gravity_m_per_s2)
        .bind(option("fluid25d.flow_damping_per_second", "--fluid25d-flow-damping-per-second",
                     "Flow Damping", "Flow damping rate in inverse seconds.", ValueType::Float,
                     {.has_min = true, .min = 0.0}),
              config.fluid.flow_damping_per_second)
        .bind(option("fluid25d.minimum_wet_depth_m", "--fluid25d-minimum-wet-depth-m",
                     "Minimum Wet Depth",
                     "Numerical wet/dry depth threshold used by flux evolution, in metres.",
                     ValueType::Float, {.has_min = true, .min = 0.0}),
              config.fluid.minimum_wet_depth_m)
        .bind(option("fluid25d.source_active_duration_seconds",
                     "--fluid25d-source-active-duration-seconds", "Source Active Duration",
                     "Optional fixed-simulation duration before source rates switch to zero.",
                     ValueType::Float, {.has_min = true, .min = 0.0}),
              config.fluid.source_active_duration_seconds)
        .bind(option("fluid25d.headwaters_source_scale", "--fluid25d-headwaters-source-scale",
                     "Headwaters Source Scale",
                     "Positive finite multiplier for both sustained-headwaters source fields.",
                     ValueType::Float),
              config.fluid.headwaters_source_scale)
        .bind(
            option(
                "fluid25d.natural_flow_source_m3_per_s", "--fluid25d-natural-flow-source-m3-per-s",
                "Natural Flow Total Source Rate",
                "Finite positive total water source rate distributed across the five recipe cells.",
                ValueType::Float, {.has_min = true, .min = 0.0}),
            config.fluid.natural_flow_source_m3_per_s)
        .bind(option("fluid25d.dye_pulse_start_seconds", "--fluid25d-dye-pulse-start-seconds",
                     "Dye Pulse Start",
                     "Opt-in finite-volume demo dye pulse start time in fixed-simulation seconds.",
                     ValueType::Float, {.has_min = true, .min = 0.0}),
              config.fluid.dye_pulse_start_seconds)
        .bind(option("fluid25d.dye_pulse_duration_seconds", "--fluid25d-dye-pulse-duration-seconds",
                     "Dye Pulse Duration",
                     "Opt-in finite-volume demo dye pulse duration in fixed-simulation seconds.",
                     ValueType::Float, {.has_min = true, .min = 0.0}),
              config.fluid.dye_pulse_duration_seconds);
    return std::move(builder).build();
}

[[nodiscard]] inline Fluid25DProjectConfig
parse_fluid_25d_project_config(int argc, char** argv, config::ParseResult* result = nullptr) {
    Fluid25DProjectConfig project_config;
    config::Schema schema = fluid_25d_project_config_schema(project_config);
    config::ParseResult parsed = schema.parse_cli(argc, argv);
    host::normalize_common_run_config(project_config.common,
                                      parsed.path_was_assigned("output") ||
                                          project_config.common.output_path != "cubey-output.png");
    const std::array recording_options{
        "fluid25d.recording_speed", "fluid25d.recording_time_seconds",
        "fluid25d.recording_frame_interval_seconds", "fluid25d.recording_camera",
        "fluid25d.recording_gpu_validation"};
    if (project_config.recording_path && project_config.stream_path)
        throw std::runtime_error("recording and live stream sources are mutually exclusive");
    static_cast<void>(fluid_25d_selected_backend(project_config));
    if (parsed.path_was_assigned("fluid25d.stream_follow_latest") && !project_config.stream_path)
        throw std::runtime_error("follow latest requires --fluid25d-stream");
    if (project_config.recording_path || project_config.stream_path) {
        if (project_config.stream_path && project_config.common.headless &&
            project_config.common.capture_mode == CaptureMode::Video)
            throw std::runtime_error("live streams support windowed viewing or a static PNG "
                                     "snapshot; convert the finished case for video");
        // Reject, rather than silently ignore, controls that imply running or changing physics.
        const std::array incompatible_options{"grid.width",
                                              "grid.height",
                                              "grid.depth",
                                              "terrain.heightfield",
                                              "fluid25d.solver",
                                              "fluid25d.scenario",
                                              "fluid25d.cell_size_m",
                                              "fluid25d.fixed_delta_seconds",
                                              "fluid25d.substeps",
                                              "fluid25d.gravity_m_per_s2",
                                              "fluid25d.flow_damping_per_second",
                                              "fluid25d.minimum_wet_depth_m",
                                              "fluid25d.terrain_crop_x",
                                              "fluid25d.terrain_crop_z",
                                              "fluid25d.terrain_water_protocol",
                                              "fluid25d.rainfall_rate_mm_per_hour",
                                              "fluid25d.sheet_depth_m",
                                              "fluid25d.source_active_duration_seconds",
                                              "fluid25d.headwaters_source_scale",
                                              "fluid25d.natural_flow_recipe",
                                              "fluid25d.natural_flow_source_m3_per_s",
                                              "fluid25d.dye_pulse_start_seconds",
                                              "fluid25d.dye_pulse_duration_seconds",
                                              "fluid25d.gpu_oracle_validation",
                                              "fluid25d.mass_audit",
                                              "fluid25d.mass_audit_control",
                                              "fluid25d.hillside_supply_response",
                                              "fluid25d.hillside_supply_gpu_controls",
                                              "fluid25d.rain_study_gpu_controls",
                                              "fluid25d.motion_marker_gpu_controls",
                                              "fluid25d.hillside_inspection_advance_seconds",
                                              "fluid25d.hillside_advance_and_continue_seconds",
                                              "fluid25d.presentation_time_scale",
                                              "fluid25d.hillside_source_context",
                                              "fluid25d.hillside_camera",
                                              "fluid25d.natural_flow_home_pitch_radians"};
        for (const char* path : incompatible_options) {
            if (parsed.path_was_assigned(path))
                throw std::runtime_error(
                    std::string("recorded playback rejects hydraulic/legacy control: ") + path);
        }
        if ((project_config.recording_path && project_config.recording_path->empty()) ||
            (project_config.stream_path && project_config.stream_path->empty()))
            throw std::runtime_error("recording manifest path must not be empty");
        if (project_config.recording_gpu_validation && !project_config.common.headless)
            throw std::runtime_error("recording GPU upload validation is headless-only");
        if (project_config.common.headless && parsed.path_was_assigned("fluid25d.recording_speed"))
            throw std::runtime_error(
                "headless recording uses frame interval, not windowed playback speed");
        if (parsed.path_was_assigned("fluid25d.recording_frame_interval_seconds") &&
            (!project_config.common.headless ||
             project_config.common.capture_mode != CaptureMode::Video))
            throw std::runtime_error("recording frame interval is headless-video-only");
        if (project_config.common.headless &&
            project_config.common.capture_mode == CaptureMode::Video &&
            project_config.recording_frame_interval_seconds <= 0.0F)
            throw std::runtime_error("recording video frame interval must be positive");
        if (parsed.path_was_assigned("fluid25d.hillside_depth_cues") ||
            parsed.path_was_assigned("fluid25d.terrain_thin_water_composite"))
            throw std::runtime_error("recording uses fixed depth cues and thin-film attenuation");
        if (project_config.catchment_view == "transport-inspection")
            throw std::runtime_error("recording contains water momentum, not conserved dye");
        if (project_config.motion_markers &&
            parsed.path_was_assigned("fluid25d.motion_marker_mode") &&
            project_config.motion_marker_mode != "local")
            throw std::runtime_error(
                "recorded rainfall supports local visual markers, not source release");
        project_config.motion_marker_mode = "local";
        static_cast<void>(fluid_25d_presentation_view_from_name(project_config.view));
        static_cast<void>(fluid_25d_catchment_view_from_name(project_config.catchment_view));
        static_cast<void>(fluid_25d_debug_view_from_name(project_config.debug_view));
        if (!project_config.catchment_view.empty() && project_config.view == "diagnostics")
            throw std::runtime_error("recorded catchment view cannot be combined with diagnostics");
        if (project_config.catchment_render.terrain_palette_low_m.has_value() !=
            project_config.catchment_render.terrain_palette_high_m.has_value())
            throw std::runtime_error("recording terrain palette bounds must be supplied together");
        if (project_config.catchment_render.terrain_palette_low_m &&
            *project_config.catchment_render.terrain_palette_low_m >=
                *project_config.catchment_render.terrain_palette_high_m)
            throw std::runtime_error("recording terrain palette bounds must be increasing");
        if (parsed.write_config_template_path)
            schema.write_template(*parsed.write_config_template_path);
        if (result != nullptr)
            *result = std::move(parsed);
        return project_config;
    }
    for (const char* path : recording_options) {
        if (parsed.path_was_assigned(path))
            throw std::runtime_error(
                std::string("recording option requires --fluid25d-recording: ") + path);
    }
    project_config.simulation =
        fluid_25d_config_from_options(project_config.grid, project_config.fluid);
    project_config.simulation.mass_audit = project_config.mass_audit;
    validate_fluid_25d_project_config(project_config);
    if (project_config.common.headless &&
        parsed.path_was_assigned("fluid25d.presentation_time_scale")) {
        throw std::runtime_error(
            "fluid 2.5D presentation time scale is windowed-only; omit it in headless mode");
    }
    static_cast<void>(fluid_25d_presentation_view_from_name(project_config.view));
    static_cast<void>(fluid_25d_catchment_view_from_name(project_config.catchment_view));
    if (!project_config.catchment_view.empty() && project_config.view == "diagnostics") {
        throw std::runtime_error("fluid 2.5D --fluid25d-catchment-view cannot be combined with "
                                 "--fluid25d-view diagnostics");
    }
    static_cast<void>(fluid_25d_debug_view_from_name(project_config.debug_view));
    if (project_config.gpu_oracle_validation && !project_config.common.headless) {
        throw std::runtime_error(
            "fluid 2.5D GPU oracle validation is headless-only; pass --headless");
    }
    if (project_config.common.profile_diagnostics && !project_config.common.headless) {
        throw std::runtime_error(
            "fluid 2.5D profile diagnostics are headless-only; pass --headless");
    }
    if (parsed.write_config_template_path.has_value()) {
        schema.write_template(parsed.write_config_template_path.value());
    }
    if (result != nullptr) {
        *result = std::move(parsed);
    }
    return project_config;
}

} // namespace cubey::projects::fluid::fluid_25d
