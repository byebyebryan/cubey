#include "fluid_25d_test_support.h"

namespace cubey::projects::fluid::fluid_25d::test_detail {

void test_backend_selection_preserves_existing_modes() {
    using namespace cubey::projects::fluid::fluid_25d;
    const auto defaults = parse_project({"fluid_25d"});
    require(fluid_25d_selected_backend(defaults) == "builtin" &&
                defaults.simulation.solver == Fluid25DSolver::VirtualPipes,
            "omitted backend must retain the builtin virtual-pipes default");
    const auto builtin = parse_project(
        {"fluid_25d", "--fluid25d-backend", "builtin", "--fluid25d-solver", "finite-volume"});
    require(fluid_25d_selected_backend(builtin) == "builtin" &&
                builtin.simulation.solver == Fluid25DSolver::FiniteVolume,
            "backend and builtin numerical method must remain separate");
    for (const bool explicit_backend : {false, true}) {
        std::vector<std::string> recording{"fluid_25d", "--fluid25d-recording", "recording.json"};
        std::vector<std::string> external{"fluid_25d", "--fluid25d-stream", "stream.json"};
        std::vector<std::string> service{"fluid_25d", "--fluid25d-external-session", "worker"};
        if (explicit_backend) {
            recording.insert(recording.end(), {"--fluid25d-backend", "recording"});
            external.insert(external.end(), {"--fluid25d-backend", "external"});
            service.insert(service.end(), {"--fluid25d-backend", "external"});
        }
        require(fluid_25d_selected_backend(parse_project(recording)) == "recording",
                "recording selection must support both old and explicit CLI forms");
        require(fluid_25d_selected_backend(parse_project(external)) == "external",
                "external selection must support both old and explicit CLI forms");
        const auto attached = parse_project(service);
        require(fluid_25d_selected_backend(attached) == "external" &&
                    attached.external_session_path == "worker",
                "external service selection must support omitted and explicit backend forms");
    }
    for (const auto& arguments : std::vector<std::vector<std::string>>{
             {"fluid_25d", "--fluid25d-backend", "external"},
             {"fluid_25d", "--fluid25d-backend", "recording"},
             {"fluid_25d", "--fluid25d-backend", "builtin", "--fluid25d-stream", "stream.json"},
             {"fluid_25d", "--fluid25d-backend", "builtin", "--fluid25d-recording",
              "recording.json"},
             {"fluid_25d", "--fluid25d-backend", "recording", "--fluid25d-stream", "stream.json"},
             {"fluid_25d", "--fluid25d-backend", "external", "--fluid25d-recording",
              "recording.json"},
             {"fluid_25d", "--fluid25d-backend", "external", "--fluid25d-recording",
              "recording.json", "--fluid25d-external-session", "worker"},
             {"fluid_25d", "--fluid25d-external-session", "worker", "--fluid25d-stream",
              "stream.json"},
             {"fluid_25d", "--fluid25d-external-session", "worker", "--fluid25d-backend",
              "builtin"},
             {"fluid_25d", "--fluid25d-backend", "not-a-backend"}}) {
        require_throws([&] { static_cast<void>(parse_project(arguments)); },
                       "backend conflicts must fail without silently selecting another mode");
    }
    for (const auto& arguments : std::vector<std::vector<std::string>>{
             {"fluid_25d", "--fluid25d-external-session", "worker", "--fluid25d-solver",
              "finite-volume"},
             {"fluid_25d", "--fluid25d-external-session", "worker",
              "--fluid25d-rainfall-rate-mm-per-hour", "3"},
             {"fluid_25d", "--fluid25d-external-session", "worker",
              "--fluid25d-stream-follow-latest"},
             {"fluid_25d", "--fluid25d-external-session", "worker",
              "--fluid25d-recording-time-seconds", "10"},
             {"fluid_25d", "--fluid25d-external-session", "worker", "--fluid25d-recording-speed",
              "2"},
             {"fluid_25d", "--fluid25d-external-session", "worker", "--headless",
              "--fluid25d-recording-frame-interval-seconds", "60"},
             {"fluid_25d", "--fluid25d-external-session", "worker", "--headless", "--capture",
              "video", "--output", "movie.mp4"}}) {
        require_throws([&] { static_cast<void>(parse_project(arguments)); },
                       "external service must reject physics and playback controls");
    }
    const auto static_service = parse_project({"fluid_25d", "--fluid25d-external-session", "worker",
                                               "--headless", "--fluid25d-recording-gpu-validation",
                                               "--fluid25d-recording-camera", "collection"});
    require(static_service.external_session_path == "worker" &&
                static_service.recording_gpu_validation &&
                static_service.recording_camera == "collection",
            "external service must allow static PNG upload validation and camera selection");
}

void test_external_session_freshness_and_typed_inputs() {
    using namespace cubey::projects::fluid::fluid_25d;
    using fluid_25d_project_config_detail::use_synchronous_whitewater_classification;
    cubey::host::CommonRunConfig whitewater_run;
    require(!use_synchronous_whitewater_classification(whitewater_run),
            "GUI whitewater should use bounded asynchronous classification");
    whitewater_run.headless = true;
    require(use_synchronous_whitewater_classification(whitewater_run),
            "PNG captures should retain deterministic synchronous classification");
    whitewater_run.capture_mode = cubey::CaptureMode::Video;
    require(use_synchronous_whitewater_classification(whitewater_run),
            "video captures should retain deterministic synchronous classification");
    whitewater_run.capture_mode = cubey::CaptureMode::Png;
    whitewater_run.headless_render_only = true;
    require(!use_synchronous_whitewater_classification(whitewater_run),
            "continuous offscreen should share the GUI asynchronous policy");
    using fluid_25d_project_config_detail::external_session_health;
    using fluid_25d_project_config_detail::ExternalSessionHealth;
    using fluid_25d_project_config_detail::should_refresh_external_control_input;

    require(external_session_health(Fluid25DLifecycle::Running, 0.1, 0.2) ==
                    ExternalSessionHealth::Healthy &&
                external_session_health(Fluid25DLifecycle::Paused, 0.5, 0.5) ==
                    ExternalSessionHealth::Healthy,
            "running and paused service heartbeats up to 0.5 s should remain healthy");
    require(external_session_health(Fluid25DLifecycle::Paused, 0.6, 0.6) ==
                    ExternalSessionHealth::Delayed &&
                external_session_health(Fluid25DLifecycle::Ready, 2.99, 0.2) ==
                    ExternalSessionHealth::Delayed,
            "late but sub-three-second service publications should be marked delayed");
    require(external_session_health(Fluid25DLifecycle::Running, 3.0, 0.1) ==
                    ExternalSessionHealth::Disconnected &&
                external_session_health(Fluid25DLifecycle::Paused, 0.1, 3.0) ==
                    ExternalSessionHealth::Disconnected,
            "three seconds without a changed or fresh publication should disconnect");
    require(external_session_health(Fluid25DLifecycle::Running, 4.0, 4.0, true, 4.0) ==
                    ExternalSessionHealth::Delayed &&
                external_session_health(Fluid25DLifecycle::Running, 31.0, 31.0, true, 30.0) ==
                    ExternalSessionHealth::Disconnected &&
                external_session_health(Fluid25DLifecycle::Failed, 4.0, 4.0, true, 4.0) ==
                    ExternalSessionHealth::Failed,
            "announced reset is delayed, bounded to thirty seconds, and never masks failure");
    require(external_session_health(Fluid25DLifecycle::Completed, 50.0, 50.0) ==
                    ExternalSessionHealth::Completed &&
                external_session_health(Fluid25DLifecycle::Stopped, 50.0, 50.0) ==
                    ExternalSessionHealth::Stopped &&
                external_session_health(Fluid25DLifecycle::Failed, 0.0, 0.0) ==
                    ExternalSessionHealth::Failed,
            "terminal completed/stopped states are normal while failed remains distinct");

    require(
        should_refresh_external_control_input(false, false, false) &&
            !should_refresh_external_control_input(true, false, false) &&
            should_refresh_external_control_input(true, false, true) &&
            should_refresh_external_control_input(true, true, false) &&
            should_refresh_external_control_input(true, false, false, true),
        "typed controls refresh only when clean, applied, reset-generation changed, or canceled");
}

void test_recording_cli_is_separate_from_physics() {
    using namespace cubey::projects::fluid::fluid_25d;
    const auto valid =
        parse_project({"fluid_25d", "--fluid25d-recording", "recording.json",
                       "--fluid25d-motion-markers", "--fluid25d-recording-speed", "60"});
    require(valid.recording_path == "recording.json" && valid.motion_marker_mode == "local",
            "recording path should resolve without hydraulic scenario validation");
    require(valid.simulation.solver == Fluid25DSolver::VirtualPipes,
            "recording must not promote finite-volume defaults");
    require(valid.native_presentation == "original",
            "reference native presentation must remain the default");
    require(!valid.native_display_coverage_path && valid.native_camera_pitch_radians == -0.92F &&
                !valid.catchment_render.native_display_coverage,
            "display coverage and observation camera changes must remain opt-in");
    const auto low_angle = parse_project({"fluid_25d", "--fluid25d-recording", "recording.json",
                                          "--fluid25d-native-camera-pitch", "-0.2617994"});
    require(low_angle.native_camera_pitch_radians > -0.263F &&
                low_angle.native_camera_pitch_radians < -0.261F &&
                low_angle.simulation.solver == Fluid25DSolver::VirtualPipes,
            "15-degree observation camera must not alter numerical defaults");
    for (const char* pitch : {"-0.19", "0"})
        require_throws(
            [pitch] {
                (void)parse_project({"fluid_25d", "--fluid25d-recording", "recording.json",
                                     "--fluid25d-native-camera-pitch", pitch});
            },
            "near-horizon and upward observation camera values remain bounded");
    require(
        valid.native_surface_highlights == "auto" &&
            fluid_25d_native_surface_highlights_enabled(
                fluid_25d_native_surface_highlights_policy(valid.native_surface_highlights), 0U),
        "native procedural highlights default to auto and remain on in Original");
    const auto highlight_policy = fluid_25d_native_surface_highlights_policy("auto");
    require(fluid_25d_native_surface_highlights_enabled(highlight_policy, 0U) &&
                fluid_25d_native_surface_highlights_enabled(highlight_policy, 1U) &&
                !fluid_25d_native_surface_highlights_enabled(highlight_policy, 2U) &&
                !fluid_25d_native_surface_highlights_enabled(highlight_policy, 3U),
            "auto highlights remain on in Original/Motion and off in Readable/Scenic");
    require(fluid_25d_native_surface_highlights_enabled(Fluid25DNativeSurfaceHighlightsPolicy::On,
                                                        2U) &&
                !fluid_25d_native_surface_highlights_enabled(
                    Fluid25DNativeSurfaceHighlightsPolicy::Off, 0U) &&
                !fluid_25d_native_surface_highlights_enabled(
                    Fluid25DNativeSurfaceHighlightsPolicy::Off, 1U) &&
                !fluid_25d_native_surface_highlights_enabled(
                    Fluid25DNativeSurfaceHighlightsPolicy::Off, 2U),
            "explicit highlight on overrides Readable auto; off suppresses all styles");
    require(!fluid_25d_native_surface_highlights_enabled(
                fluid_25d_native_surface_highlights_policy("invalid"), 0U) &&
                !fluid_25d_native_surface_highlights_enabled(
                    static_cast<Fluid25DNativeSurfaceHighlightsPolicy>(99U), 0U),
            "invalid highlight policy values fail closed");
    require(valid.native_water_sampling == "triangular" && valid.native_water_debug == "shaded",
            "reference geometry/material sampling and shaded water remain defaults");
    for (const char* sampling : {"triangular", "bilinear", "bspline"}) {
        const auto sampled = parse_project({"fluid_25d", "--fluid25d-recording", "recording.json",
                                            "--fluid25d-native-water-sampling", sampling});
        require(sampled.native_water_sampling == sampling &&
                    sampled.simulation.solver == Fluid25DSolver::VirtualPipes,
                "bank display interpolation cannot select or alter the solver");
    }
    for (const char* subdivision : {"1", "2", "4"}) {
        const auto sampled = parse_project({"fluid_25d", "--fluid25d-recording", "recording.json",
                                            "--fluid25d-native-water-sampling", "bspline",
                                            "--fluid25d-native-surface-subdivision", subdivision});
        require(sampled.native_surface_subdivision ==
                    static_cast<std::uint32_t>(std::stoi(subdivision)),
                "display subdivision is explicit and native only");
    }
    for (const char* subdivision : {"0", "3", "8"})
        require_throws(
            [subdivision] {
                static_cast<void>(
                    parse_project({"fluid_25d", "--fluid25d-recording", "recording.json",
                                   "--fluid25d-native-water-sampling", "bspline",
                                   "--fluid25d-native-surface-subdivision", subdivision}));
            },
            "invalid display subdivision fails closed");
    require_throws(
        [] {
            static_cast<void>(parse_project({"fluid_25d", "--fluid25d-recording", "recording.json",
                                             "--fluid25d-native-surface-subdivision", "2"}));
        },
        "display subdivision requires B-spline opt-in");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-native-surface-subdivision", "2"}));
        },
        "built-in sources reject display subdivision");
    for (const char* diagnostic : {"shaded", "solid", "unlit", "normals", "wireframe", "wet-mask",
                                   "no-occlusion", "contours"}) {
        const auto diagnosed = parse_project({"fluid_25d", "--fluid25d-recording", "recording.json",
                                              "--fluid25d-native-water-debug", diagnostic});
        require(diagnosed.native_water_debug == diagnostic,
                "water diagnosis must accept each explicit render-only view");
    }
    for (const char* option :
         {"--fluid25d-native-water-sampling", "--fluid25d-native-water-debug"}) {
        const char* accepted =
            std::string_view(option) == "--fluid25d-native-water-debug" ? "shaded" : "bilinear";
        require_throws(
            [option, accepted] {
                static_cast<void>(parse_project({"fluid_25d", option, accepted}));
            },
            "native water options must reject a built-in source");
        require_throws(
            [option] {
                static_cast<void>(parse_project(
                    {"fluid_25d", "--fluid25d-recording", "recording.json", option, "invalid"}));
            },
            "unknown bank display modes fail closed");
    }
    for (const char* style : {"original", "motion", "readable", "scenic"}) {
        const auto styled = parse_project({"fluid_25d", "--fluid25d-recording", "recording.json",
                                           "--fluid25d-native-presentation", style});
        require(styled.native_presentation == style &&
                    styled.simulation.solver == Fluid25DSolver::VirtualPipes,
                "native presentation is render-only and must not select a solver");
    }
    for (const std::string_view source : {"recording", "external"}) {
        for (const char* policy : {"auto", "on", "off"}) {
            const auto highlights =
                source == "recording"
                    ? parse_project({"fluid_25d", "--fluid25d-recording", "recording.json",
                                     "--fluid25d-native-surface-highlights", policy})
                    : parse_project({"fluid_25d", "--fluid25d-external-session", "worker",
                                     "--fluid25d-native-surface-highlights", policy});
            require(highlights.native_surface_highlights == policy &&
                        highlights.simulation.solver == Fluid25DSolver::VirtualPipes,
                    "recording and external sessions share render-only highlight policies");
        }
    }
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-native-surface-highlights", "off"}));
        },
        "native surface highlights must reject a built-in source");
    require_throws(
        [] {
            static_cast<void>(parse_project({"fluid_25d", "--fluid25d-recording", "recording.json",
                                             "--fluid25d-native-surface-highlights", "invalid"}));
        },
        "unknown native surface highlight policy fails closed");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-native-presentation", "readable"}));
        },
        "native presentation must not silently affect a built-in scene");
    require_throws(
        [] {
            static_cast<void>(parse_project({"fluid_25d", "--fluid25d-recording", "recording.json",
                                             "--fluid25d-native-presentation", "unknown"}));
        },
        "unknown native presentation must fail closed");
    const auto service_style = parse_project({"fluid_25d", "--fluid25d-external-session", "worker",
                                              "--fluid25d-native-presentation", "readable"});
    require(service_style.native_presentation == "readable",
            "native service and recording must share the presentation opt-in");
    require_close(fluid_25d_recorded_visual_delta(7200.0, 7205.0, 14400.0), 5.0, 0.0,
                  "cue time advances between held native frames");
    require_close(fluid_25d_recorded_visual_delta(7205.0, 7210.0, 14400.0), 5.0, 0.0,
                  "cue time must not wait for the next saved water frame");
    require_close(fluid_25d_recorded_visual_delta(7200.0, 7200.0, 14400.0), 0.0, 0.0,
                  "initial frame and pause hold the visual clock");
    require_close(fluid_25d_recorded_visual_delta(14395.0, 14405.0, 14400.0), 5.0, 0.0,
                  "visual catch-up cannot pass the final native frame");
    require_close(fluid_25d_recorded_visual_delta(14400.0, 14405.0, 14400.0), 0.0, 0.0,
                  "completed recording cannot keep flowing visually");
    require_close(fluid_25d_recorded_visual_delta(0.0, 120.0, 14400.0), 60.0, 0.0,
                  "sparse video has bounded approximate catch-up");
    require_throws([] { static_cast<void>(fluid_25d_recorded_visual_delta(10.0, 5.0, 20.0)); },
                   "backwards seek must reset rather than reverse approximate advection");
    require_throws(
        [] {
            static_cast<void>(fluid_25d_recorded_visual_delta(
                0.0, std::numeric_limits<double>::infinity(), 20.0));
        },
        "non-finite cue clock rejected");
    const float native_seed = fluid_25d_presentation_cue_seed(37U, 19U, true);
    require(native_seed >= 0.0F && native_seed <= 1.0F &&
                native_seed == fluid_25d_presentation_cue_seed(37U, 19U, true) &&
                native_seed != fluid_25d_presentation_cue_seed(37U, 19U),
            "native cue scale is deterministic, bounded and independent of reference seed");
    const auto reject = [](std::vector<std::string> extra) {
        std::vector<std::string> arguments{"fluid_25d", "--fluid25d-recording", "recording.json"};
        arguments.insert(arguments.end(), extra.begin(), extra.end());
        require_throws([&] { static_cast<void>(parse_project(arguments)); },
                       "recording accepted an incompatible or inactive control");
    };
    reject({"--fluid25d-solver", "virtual-pipes"});
    reject({"--fluid25d-scenario", "dry-bed"});
    reject({"--grid-width", "512"});
    reject({"--terrain-heightfield", "terrain.json"});
    reject({"--fluid25d-cell-size-m", "30"});
    reject({"--fluid25d-rainfall-rate-mm-per-hour", "12"});
    reject({"--fluid25d-gpu-oracle-validation"});
    reject({"--fluid25d-motion-markers", "--fluid25d-motion-marker-mode", "source"});
    reject({"--fluid25d-catchment-view", "transport-inspection"});
    reject({"--fluid25d-recording-gpu-validation"});
    reject({"--fluid25d-recording-frame-interval-seconds", "60"});
    reject({"--headless", "--fluid25d-recording-speed", "60"});
    reject({"--headless", "--capture", "video", "--output", "movie.mp4",
            "--fluid25d-recording-frame-interval-seconds", "0"});
    require_throws(
        [] { static_cast<void>(parse_project({"fluid_25d", "--fluid25d-recording-speed", "60"})); },
        "recording controls need a recording path");
    const auto capture = parse_project({"fluid_25d", "--headless", "--fluid25d-recording",
                                        "recording.json", "--fluid25d-recording-gpu-validation",
                                        "--fluid25d-recording-time-seconds", "900"});
    require(capture.recording_gpu_validation && capture.recording_time_seconds == 900.0F,
            "fixed-time recorded capture should be independently configured");
}

void test_config_defaults_and_parsing() {
    using namespace cubey::projects::fluid::fluid_25d;

    const Fluid25DConfig defaults;
    validate_fluid_25d_config(defaults);
    require(defaults.grid_width == kDefaultFluid25DGridWidth,
            "fluid 2.5D should expose the River V0 default grid width");
    require(defaults.grid_height == kDefaultFluid25DGridHeight,
            "fluid 2.5D should expose the River V0 default grid height");
    require(defaults.grid_width == 256U && defaults.grid_height == 128U,
            "fluid 2.5D should retain the bounded 256 by 128 product grid");
    require(defaults.cell_size_m == 1.0F, "fluid 2.5D should default to one metre cells");
    require(defaults.simulation_substeps == 2,
            "fluid 2.5D should default to two fixed solver substeps");
    require(defaults.scenario == Fluid25DScenario::RiverCatchment,
            "fluid 2.5D should default to the river catchment scenario");
    require(defaults.solver == Fluid25DSolver::VirtualPipes,
            "fluid 2.5D should retain virtual-pipes as the product default");
    require(std::string(fluid_25d_solver_name(Fluid25DSolver::FiniteVolume)) == "finite-volume" &&
                fluid_25d_solver_from_name("finite-volume") == Fluid25DSolver::FiniteVolume &&
                fluid_25d_solver_from_name("") == Fluid25DSolver::VirtualPipes,
            "fluid 2.5D finite-volume comparison solver names should be stable");
    require_throws([] { static_cast<void>(fluid_25d_solver_from_name("unknown")); },
                   "fluid 2.5D should reject unknown solver names");
    Fluid25DConfig invalid_solver = defaults;
    invalid_solver.solver = static_cast<Fluid25DSolver>(42U);
    require_throws([&] { validate_fluid_25d_config(invalid_solver); },
                   "fluid 2.5D should reject invalid solver enum values");
    require(std::string(fluid_25d_scenario_name(Fluid25DScenario::LakeAtRest)) == "lake-at-rest",
            "fluid 2.5D scenario names should be stable");
    require(fluid_25d_scenario_from_name("dry") == Fluid25DScenario::DryBed,
            "fluid 2.5D should retain the short dry scenario alias");
    require(fluid_25d_scenario_from_name("terrain-case") == Fluid25DScenario::TerrainCase,
            "fluid 2.5D should parse the explicit terrain case scenario");
    require(fluid_25d_scenario_from_name("boundary-drain-fixture") ==
                Fluid25DScenario::BoundaryDrainFixture,
            "fluid 2.5D should parse the numerical boundary drain fixture");
    require(fluid_25d_scenario_from_name("source-outlet-demo") ==
                    Fluid25DScenario::SourceOutletDemo &&
                std::string(fluid_25d_scenario_name(Fluid25DScenario::SourceOutletDemo)) ==
                    "source-outlet-demo",
            "fluid 2.5D should expose the opt-in source-outlet demo distinctly from River V0");
    require(fluid_25d_scenario_from_name("mountain-source-outlet-demo") ==
                    Fluid25DScenario::MountainSourceOutletDemo &&
                std::string(fluid_25d_scenario_name(Fluid25DScenario::MountainSourceOutletDemo)) ==
                    "mountain-source-outlet-demo",
            "fluid 2.5D should expose the pinned mountain source/outlet demo distinctly");
    require(fluid_25d_scenario_from_name("sustained-headwaters-demo") ==
                    Fluid25DScenario::SustainedHeadwatersDemo &&
                std::string(fluid_25d_scenario_name(Fluid25DScenario::SustainedHeadwatersDemo)) ==
                    "sustained-headwaters-demo",
            "fluid 2.5D should expose the opt-in sustained-headwaters control distinctly");
    require(fluid_25d_is_source_outlet_demo(Fluid25DScenario::SourceOutletDemo) &&
                fluid_25d_is_source_outlet_demo(Fluid25DScenario::MountainSourceOutletDemo) &&
                !fluid_25d_is_source_outlet_demo(Fluid25DScenario::RiverCatchment),
            "fluid 2.5D should share endpoint language only between the explicit demos");
    Fluid25DConfig virtual_pipes_demo = defaults;
    virtual_pipes_demo.scenario = Fluid25DScenario::SourceOutletDemo;
    require_throws([&] { validate_fluid_25d_config(virtual_pipes_demo); },
                   "source-outlet demo should require the explicit finite-volume solver");
    virtual_pipes_demo.solver = Fluid25DSolver::FiniteVolume;
    validate_fluid_25d_config(virtual_pipes_demo);
    Fluid25DConfig mountain_demo = defaults;
    mountain_demo.scenario = Fluid25DScenario::MountainSourceOutletDemo;
    mountain_demo.solver = Fluid25DSolver::FiniteVolume;
    mountain_demo.cell_size_m = kFluid25DMountainSourceOutletCellSizeM;
    validate_fluid_25d_config(mountain_demo);
    mountain_demo.grid_width -= 1U;
    require_throws([&] { validate_fluid_25d_config(mountain_demo); },
                   "mountain source/outlet demo should require its pinned width");
    mountain_demo = defaults;
    mountain_demo.scenario = Fluid25DScenario::MountainSourceOutletDemo;
    mountain_demo.solver = Fluid25DSolver::FiniteVolume;
    require_throws([&] { validate_fluid_25d_config(mountain_demo); },
                   "mountain source/outlet demo should require native 30 metre cells");
    Fluid25DConfig headwaters_demo = defaults;
    headwaters_demo.scenario = Fluid25DScenario::SustainedHeadwatersDemo;
    headwaters_demo.solver = Fluid25DSolver::FiniteVolume;
    headwaters_demo.grid_width = kFluid25DSustainedHeadwatersGridWidth;
    headwaters_demo.grid_height = kFluid25DSustainedHeadwatersGridHeight;
    headwaters_demo.cell_size_m = kFluid25DSustainedHeadwatersCellSizeM;
    validate_fluid_25d_config(headwaters_demo);
    struct HeadwatersResolution {
        std::uint32_t width;
        std::uint32_t height;
        float cell_size_m;
    };
    constexpr std::array<HeadwatersResolution, 3U> supported_headwaters_resolutions{{
        {kFluid25DSustainedHeadwatersGridWidth, kFluid25DSustainedHeadwatersGridHeight,
         kFluid25DSustainedHeadwatersCellSizeM},
        {kFluid25DSustainedHeadwatersGridWidth2m, kFluid25DSustainedHeadwatersGridHeight2m,
         kFluid25DSustainedHeadwatersCellSize2m},
        {kFluid25DSustainedHeadwatersGridWidth1m, kFluid25DSustainedHeadwatersGridHeight1m,
         kFluid25DSustainedHeadwatersCellSize1m},
    }};
    for (const HeadwatersResolution& resolution : supported_headwaters_resolutions) {
        Fluid25DConfig supported = headwaters_demo;
        supported.grid_width = resolution.width;
        supported.grid_height = resolution.height;
        supported.cell_size_m = resolution.cell_size_m;
        validate_fluid_25d_config(supported);
    }
    Fluid25DConfig headwaters_with_dye = headwaters_demo;
    headwaters_with_dye.dye_pulse_start_seconds = 0.25F;
    headwaters_with_dye.dye_pulse_duration_seconds = 0.5F;
    validate_fluid_25d_config(headwaters_with_dye);
    require(fluid_25d_transport_inspection_available(headwaters_with_dye),
            "a sustained-headwaters finite-volume dye pulse should enable transport inspection");
    require(!fluid_25d_transport_inspection_available(headwaters_demo),
            "sustained-headwaters transport inspection should remain opt-in without a dye pulse");
    Fluid25DConfig invalid_headwaters_demo = headwaters_demo;
    invalid_headwaters_demo.solver = Fluid25DSolver::VirtualPipes;
    require_throws([&] { validate_fluid_25d_config(invalid_headwaters_demo); },
                   "sustained-headwaters demo should require the finite-volume solver");
    invalid_headwaters_demo = headwaters_demo;
    invalid_headwaters_demo.grid_width -= 1U;
    require_throws([&] { validate_fluid_25d_config(invalid_headwaters_demo); },
                   "sustained-headwaters demo should require its control grid width");
    invalid_headwaters_demo = headwaters_demo;
    invalid_headwaters_demo.grid_width = kFluid25DSustainedHeadwatersGridWidth2m;
    invalid_headwaters_demo.grid_height = kFluid25DSustainedHeadwatersGridHeight2m;
    invalid_headwaters_demo.cell_size_m = 1.0F;
    require_throws([&] { validate_fluid_25d_config(invalid_headwaters_demo); },
                   "sustained-headwaters demo should reject a mismatched refined tuple");
    invalid_headwaters_demo = headwaters_demo;
    invalid_headwaters_demo.source_active_duration_seconds = 10.0F;
    require_throws([&] { validate_fluid_25d_config(invalid_headwaters_demo); },
                   "sustained-headwaters demo should reject a source shutoff duration");
    invalid_headwaters_demo = headwaters_with_dye;
    invalid_headwaters_demo.source_active_duration_seconds = 10.0F;
    require_throws([&] { validate_fluid_25d_config(invalid_headwaters_demo); },
                   "dye timing should not permit sustained-headwaters hydraulic shutoff");
    invalid_headwaters_demo = headwaters_demo;
    invalid_headwaters_demo.terrain_water_protocol = Fluid25DTerrainWaterProtocol::RainPulse;
    invalid_headwaters_demo.rainfall_depth_rate_m_per_s = 0.001F;
    require_throws([&] { validate_fluid_25d_config(invalid_headwaters_demo); },
                   "sustained-headwaters demo should reject rain forcing");
    invalid_headwaters_demo = headwaters_demo;
    invalid_headwaters_demo.terrain_water_protocol = Fluid25DTerrainWaterProtocol::SheetRelease;
    invalid_headwaters_demo.sheet_initial_depth_m = 0.1F;
    require_throws([&] { validate_fluid_25d_config(invalid_headwaters_demo); },
                   "sustained-headwaters demo should reject sheet forcing");
    for (const Fluid25DScenario scenario :
         {Fluid25DScenario::MountainSourceOutletDemo, Fluid25DScenario::TerrainCase}) {
        Fluid25DConfig invalid_dye_scenario = defaults;
        invalid_dye_scenario.scenario = scenario;
        invalid_dye_scenario.solver = Fluid25DSolver::FiniteVolume;
        invalid_dye_scenario.dye_pulse_start_seconds = 0.0F;
        invalid_dye_scenario.dye_pulse_duration_seconds = 1.0F;
        require_throws([&] { validate_fluid_25d_config(invalid_dye_scenario); },
                       "terrain-backed and mountain scenarios should reject dye pulse timing");
    }

    const Fluid25DProjectConfig parsed_headwaters =
        parse_project({"fluid_25d", "--fluid25d-scenario", "sustained-headwaters-demo",
                       "--fluid25d-solver", "finite-volume", "--grid-width", "65", "--grid-height",
                       "33", "--fluid25d-cell-size-m", "4"});
    require(parsed_headwaters.simulation.scenario == Fluid25DScenario::SustainedHeadwatersDemo &&
                parsed_headwaters.simulation.solver == Fluid25DSolver::FiniteVolume &&
                parsed_headwaters.simulation.grid_width == kFluid25DSustainedHeadwatersGridWidth &&
                parsed_headwaters.simulation.grid_height ==
                    kFluid25DSustainedHeadwatersGridHeight &&
                parsed_headwaters.simulation.cell_size_m == kFluid25DSustainedHeadwatersCellSizeM &&
                parsed_headwaters.simulation.headwaters_source_scale ==
                    kFluid25DDefaultHeadwatersSourceScale,
            "sustained-headwaters demo should be selectable through its explicit CLI contract");
    const Fluid25DProjectConfig scaled_headwaters = parse_project(
        {"fluid_25d", "--fluid25d-scenario", "sustained-headwaters-demo", "--fluid25d-solver",
         "finite-volume", "--grid-width", "65", "--grid-height", "33", "--fluid25d-cell-size-m",
         "4", "--fluid25d-headwaters-source-scale", "2"});
    require(scaled_headwaters.simulation.headwaters_source_scale == 2.0F,
            "sustained-headwaters CLI should accept a positive source multiplier");
    for (const std::string invalid_scale : {"0", "-1", "nan", "inf"}) {
        require_throws(
            [invalid_scale] {
                static_cast<void>(
                    parse_project({"fluid_25d", "--fluid25d-scenario", "sustained-headwaters-demo",
                                   "--fluid25d-solver", "finite-volume", "--grid-width", "65",
                                   "--grid-height", "33", "--fluid25d-cell-size-m", "4",
                                   "--fluid25d-headwaters-source-scale", invalid_scale}));
            },
            "sustained-headwaters CLI should reject non-positive or non-finite source multipliers");
    }
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-headwaters-source-scale", "1"}));
        },
        "headwaters source multiplier should be rejected when its scenario is not selected");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-scenario", "sustained-headwaters-demo",
                               "--fluid25d-solver", "finite-volume", "--grid-width", "65",
                               "--grid-height", "33", "--fluid25d-cell-size-m", "4",
                               "--fluid25d-source-active-duration-seconds", "10"}));
        },
        "sustained-headwaters CLI should reject source shutoff scheduling");
    require(fluid_25d_terrain_water_protocol_from_name("rain-pulse") ==
                Fluid25DTerrainWaterProtocol::RainPulse,
            "fluid 2.5D should parse the rain-pulse terrain-water protocol");
    require(fluid_25d_terrain_water_protocol_from_name("sheet-release") ==
                Fluid25DTerrainWaterProtocol::SheetRelease,
            "fluid 2.5D should parse the sheet-release terrain-water protocol");
    require_throws([] { static_cast<void>(fluid_25d_terrain_water_protocol_from_name("inflow")); },
                   "fluid 2.5D should reject unsupported terrain-water protocols");
    require_close(fluid_25d_rainfall_depth_rate_m_per_s_from_mm_per_hour(3600.0F), 0.001,
                  kDepthToleranceM, "fluid 2.5D should convert rainfall mm/hour to depth rate m/s");
    require_throws(
        [] { static_cast<void>(fluid_25d_rainfall_depth_rate_m_per_s_from_mm_per_hour(-1.0F)); },
        "fluid 2.5D should reject negative rainfall conversion inputs");
    require_throws([] { static_cast<void>(fluid_25d_scenario_from_name("unknown")); },
                   "fluid 2.5D should reject unknown scenario names");
    require(fluid_25d_debug_view_from_name("flow") == Fluid25DDebugView::FlowMagnitude,
            "fluid 2.5D should parse the flow diagnostic view");
    require_throws([] { static_cast<void>(fluid_25d_debug_view_from_name("unknown")); },
                   "fluid 2.5D should reject unknown diagnostic views");
    require(fluid_25d_presentation_view_from_name("catchment") ==
                Fluid25DPresentationView::Catchment,
            "fluid 2.5D should select the product catchment view explicitly");
    require(fluid_25d_catchment_view_from_name("composite") == Fluid25DCatchmentView::Composite,
            "fluid 2.5D should retain Composite as the default catchment presentation");
    require_throws([] { static_cast<void>(fluid_25d_presentation_view_from_name("composite")); },
                   "fluid 2.5D surface selector should keep catchment modes in their own enum");
    require(fluid_25d_catchment_view_from_name("water-isolation") ==
                Fluid25DCatchmentView::WaterIsolation,
            "fluid 2.5D should parse the water-isolation presentation mode");
    require(fluid_25d_catchment_view_from_name("flow-inspection") ==
                Fluid25DCatchmentView::FlowInspection,
            "fluid 2.5D should parse the flow-inspection presentation mode");
    require(static_cast<std::uint32_t>(Fluid25DCatchmentView::Composite) == 0U &&
                static_cast<std::uint32_t>(Fluid25DCatchmentView::WaterIsolation) == 1U &&
                static_cast<std::uint32_t>(Fluid25DCatchmentView::FlowInspection) == 2U &&
                static_cast<std::uint32_t>(Fluid25DCatchmentView::TransportInspection) == 3U &&
                fluid_25d_catchment_view_from_name("transport-inspection") ==
                    Fluid25DCatchmentView::TransportInspection &&
                std::string(fluid_25d_catchment_view_name(
                    Fluid25DCatchmentView::TransportInspection)) == "Transport Inspection",
            "fluid 2.5D should append the transport view without renumbering existing modes");
    require(fluid_25d_presentation_view_from_name("diagnostics") ==
                Fluid25DPresentationView::Diagnostics,
            "fluid 2.5D should retain an explicit diagnostic presentation mode");
    require(std::string(fluid_25d_presentation_view_name(Fluid25DPresentationView::Catchment)) ==
                    "Catchment" &&
                std::string(fluid_25d_catchment_view_name(Fluid25DCatchmentView::Composite)) ==
                    "Composite",
            "fluid 2.5D surface and catchment presentation names should stay distinct");
    require_throws([] { static_cast<void>(fluid_25d_catchment_view_from_name("diagnostics")); },
                   "fluid 2.5D catchment presentation should reject diagnostics");
    require_throws([] { static_cast<void>(fluid_25d_presentation_view_from_name("unknown")); },
                   "fluid 2.5D should reject unknown presentation views");
    require(fluid_25d_mesh_vertex_count(Fluid25DConfig{
                .grid_width = 2U,
                .grid_height = 2U,
            }) == 6U,
            "fluid 2.5D should generate six procedural vertices per grid quad");
    require_throws(
        [] {
            static_cast<void>(fluid_25d_mesh_vertex_count(Fluid25DConfig{
                .grid_width = 1U,
                .grid_height = 2U,
            }));
        },
        "fluid 2.5D should reject product meshes with fewer than two cells per axis");

    const Fluid25DProjectConfig parsed = parse_project(
        {"fluid_25d", "--grid-width", "10", "--grid-height", "6", "--fluid25d-scenario",
         "lake-at-rest", "--fluid25d-cell-size-m", "2.5", "--fluid25d-fixed-delta-seconds", "0.02",
         "--fluid25d-substeps", "3", "--fluid25d-gravity-m-per-s2", "9.5",
         "--fluid25d-flow-damping-per-second", "0.8", "--fluid25d-minimum-wet-depth-m", "0.002"});
    require(parsed.simulation.grid_width == 10 && parsed.simulation.grid_height == 6,
            "fluid 2.5D parser should bind shared grid dimensions");
    require(parsed.simulation.scenario == Fluid25DScenario::LakeAtRest,
            "fluid 2.5D parser should bind the scenario");
    require_close(parsed.simulation.cell_size_m, 2.5, kDepthToleranceM,
                  "fluid 2.5D parser should bind cell size in metres");
    require_close(parsed.simulation.fixed_delta_seconds, 0.02, kDepthToleranceM,
                  "fluid 2.5D parser should bind fixed delta");
    require(parsed.simulation.simulation_substeps == 3,
            "fluid 2.5D parser should bind fixed substeps");
    require_close(parsed.simulation.gravity_m_per_s2, 9.5, kDepthToleranceM,
                  "fluid 2.5D parser should bind gravity");
    require_close(parsed.simulation.flow_damping_per_second, 0.8, kDepthToleranceM,
                  "fluid 2.5D parser should bind flow damping");
    require_close(parsed.simulation.minimum_wet_depth_m, 0.002, kDepthToleranceM,
                  "fluid 2.5D parser should bind wet depth threshold");
    require(parsed.presentation_time_scale == kFluid25DDefaultWindowedPresentationTimeScale,
            "fluid 2.5D should default windowed presentation playback to 1x");
    require(parsed.view.empty(),
            "fluid 2.5D should default the CLI view name to the catchment enum default");
    require(parsed.catchment_view.empty(),
            "fluid 2.5D should default the catchment presentation to Composite");
    require(!parsed.gpu_oracle_validation,
            "fluid 2.5D should keep solver readback disabled unless explicitly requested");
    const Fluid25DProjectConfig finite_volume =
        parse_project({"fluid_25d", "--fluid25d-solver", "finite-volume"});
    require(finite_volume.simulation.solver == Fluid25DSolver::FiniteVolume,
            "fluid 2.5D parser should opt into the finite-volume comparison solver");
    require_throws(
        [] {
            static_cast<void>(parse_project({"fluid_25d", "--fluid25d-solver", "not-a-solver"}));
        },
        "fluid 2.5D parser should reject an unknown solver");

    const Fluid25DProjectConfig scheduled =
        parse_project({"fluid_25d", "--fluid25d-source-active-duration-seconds", "0.05"});
    require(scheduled.simulation.source_active_duration_seconds == 0.05F,
            "fluid 2.5D parser should retain an optional source active duration");

    const Fluid25DProjectConfig playback =
        parse_project({"fluid_25d", "--fluid25d-presentation-time-scale", "4"});
    require(playback.presentation_time_scale == 4.0F,
            "fluid 2.5D parser should bind the windowed presentation time scale");
    require_throws(
        [] {
            static_cast<void>(parse_project(
                {"fluid_25d", "--headless", "--fluid25d-presentation-time-scale", "4"}));
        },
        "fluid 2.5D presentation time scale should be rejected in headless mode");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-presentation-time-scale", "0"}));
        },
        "fluid 2.5D presentation time scale should reject zero");
    require_throws(
        [] { validate_fluid_25d_windowed_presentation_time_scale(0.1F); },
        "fluid 2.5D presentation time scale should reject values below the named minimum");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-presentation-time-scale", "9"}));
        },
        "fluid 2.5D presentation time scale should reject values above the review bound");

    const Fluid25DProjectConfig water_isolation =
        parse_project({"fluid_25d", "--fluid25d-catchment-view", "water-isolation"});
    require(water_isolation.catchment_view == "water-isolation",
            "fluid 2.5D parser should propagate the water-isolation presentation mode");
    const Fluid25DProjectConfig flow_inspection =
        parse_project({"fluid_25d", "--fluid25d-catchment-view", "flow-inspection"});
    require(flow_inspection.catchment_view == "flow-inspection",
            "fluid 2.5D parser should propagate the flow-inspection presentation mode");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-catchment-view", "not-a-catchment-view"}));
        },
        "fluid 2.5D parser should reject an unknown catchment presentation mode");
    require_throws(
        [] {
            static_cast<void>(parse_project({"fluid_25d", "--fluid25d-view", "diagnostics",
                                             "--fluid25d-catchment-view", "composite"}));
        },
        "fluid 2.5D parser should reject diagnostics combined with a catchment mode");

    const Fluid25DProjectConfig transport_inspection =
        parse_project({"fluid_25d", "--fluid25d-catchment-view", "transport-inspection",
                       "--fluid25d-scenario", "source-outlet-demo", "--fluid25d-solver",
                       "finite-volume", "--fluid25d-dye-pulse-start-seconds", "0",
                       "--fluid25d-dye-pulse-duration-seconds", "0.25"});
    require(
        transport_inspection.catchment_view == "transport-inspection" &&
            fluid_25d_transport_inspection_available(transport_inspection.simulation),
        "transport inspection should be available only with the concrete source/outlet dye pulse");
    const Fluid25DProjectConfig headwaters_transport_inspection = parse_project(
        {"fluid_25d", "--fluid25d-catchment-view", "transport-inspection", "--fluid25d-scenario",
         "sustained-headwaters-demo", "--fluid25d-solver", "finite-volume", "--grid-width", "65",
         "--grid-height", "33", "--fluid25d-cell-size-m", "4", "--fluid25d-dye-pulse-start-seconds",
         "0", "--fluid25d-dye-pulse-duration-seconds", "0.25"});
    require(
        headwaters_transport_inspection.simulation.scenario ==
                Fluid25DScenario::SustainedHeadwatersDemo &&
            headwaters_transport_inspection.catchment_view == "transport-inspection" &&
            fluid_25d_transport_inspection_available(headwaters_transport_inspection.simulation),
        "sustained headwaters should expose transport inspection for an explicit dye pulse");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-catchment-view", "transport-inspection",
                               "--fluid25d-scenario", "source-outlet-demo", "--fluid25d-solver",
                               "finite-volume"}));
        },
        "transport inspection should reject a source/outlet scene without a dye pulse");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-catchment-view", "transport-inspection",
                               "--fluid25d-scenario", "sustained-headwaters-demo",
                               "--fluid25d-solver", "finite-volume", "--grid-width", "65",
                               "--grid-height", "33", "--fluid25d-cell-size-m", "4"}));
        },
        "headwater transport inspection should require explicit dye timing");

    const Fluid25DProjectConfig terrain = parse_project(
        {"fluid_25d", "--fluid25d-scenario", "terrain-case", "--terrain-heightfield",
         "terrain-fixture", "--fluid25d-terrain-crop-x", "12", "--fluid25d-terrain-crop-z", "7"});
    require(terrain.simulation.scenario == Fluid25DScenario::TerrainCase,
            "fluid 2.5D parser should bind the terrain case scenario");
    require(terrain.terrain.heightfield_path == "terrain-fixture" &&
                terrain.terrain.crop_x == 12U && terrain.terrain.crop_z == 7U,
            "fluid 2.5D parser should retain the terrain path and native crop coordinates");
    const Fluid25DProjectConfig thin_water =
        parse_project({"fluid_25d", "--fluid25d-scenario", "terrain-case", "--terrain-heightfield",
                       "terrain-fixture", "--fluid25d-terrain-thin-water-composite"});
    require(thin_water.catchment_render.terrain_thin_water_composite,
            "terrain thin-water Composite should be an explicit render-only opt-in");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-terrain-thin-water-composite"}));
        },
        "thin-water Composite should reject analytic scenarios");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-scenario", "terrain-case",
                               "--terrain-heightfield", "terrain-fixture", "--fluid25d-view",
                               "diagnostics", "--fluid25d-terrain-thin-water-composite"}));
        },
        "thin-water Composite should reject diagnostic views");
    require_throws(
        [] {
            static_cast<void>(parse_project({"fluid_25d", "--fluid25d-scenario", "terrain-case",
                                             "--terrain-heightfield", "terrain-fixture",
                                             "--fluid25d-catchment-view", "flow-inspection",
                                             "--fluid25d-terrain-thin-water-composite"}));
        },
        "thin-water Composite should reject non-Composite catchment views");
    require_throws(
        [] { static_cast<void>(parse_project({"fluid_25d", "--terrain-heightfield", "terrain"})); },
        "fluid 2.5D should reject a terrain path with an analytic scenario");
    require_throws(
        [] {
            static_cast<void>(parse_project({"fluid_25d", "--fluid25d-scenario", "terrain-case"}));
        },
        "fluid 2.5D should reject terrain-case without a terrain path");
    require_throws(
        [] { static_cast<void>(parse_project({"fluid_25d", "--fluid25d-terrain-crop-x", "1"})); },
        "fluid 2.5D should reject terrain crop options for an analytic scenario");

    const Fluid25DProjectConfig mountain = parse_project(
        {"fluid_25d", "--fluid25d-scenario", "mountain-source-outlet-demo", "--terrain-heightfield",
         "terrain-fixture", "--fluid25d-solver", "finite-volume"});
    require(
        mountain.simulation.scenario == Fluid25DScenario::MountainSourceOutletDemo &&
            mountain.simulation.solver == Fluid25DSolver::FiniteVolume &&
            mountain.simulation.grid_width == kFluid25DMountainSourceOutletGridWidth &&
            mountain.simulation.grid_height == kFluid25DMountainSourceOutletGridHeight &&
            mountain.simulation.cell_size_m == kFluid25DMountainSourceOutletCellSizeM,
        "mountain source/outlet parser should establish its pinned finite-volume grid contract");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-scenario", "mountain-source-outlet-demo",
                               "--fluid25d-solver", "finite-volume"}));
        },
        "mountain source/outlet demo should require a terrain heightfield");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-scenario", "mountain-source-outlet-demo",
                               "--terrain-heightfield", "terrain-fixture"}));
        },
        "mountain source/outlet demo should require finite-volume explicitly");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-scenario", "mountain-source-outlet-demo",
                               "--terrain-heightfield", "terrain-fixture", "--fluid25d-solver",
                               "finite-volume", "--grid-width", "255"}));
        },
        "mountain source/outlet demo should reject non-pinned grid dimensions");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-scenario", "mountain-source-outlet-demo",
                               "--terrain-heightfield", "terrain-fixture", "--fluid25d-solver",
                               "finite-volume", "--grid-height", "127"}));
        },
        "mountain source/outlet demo should reject non-pinned grid height");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-scenario", "mountain-source-outlet-demo",
                               "--terrain-heightfield", "terrain-fixture", "--fluid25d-solver",
                               "finite-volume", "--fluid25d-cell-size-m", "20"}));
        },
        "mountain source/outlet demo should reject non-native cell spacing");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-scenario", "mountain-source-outlet-demo",
                               "--terrain-heightfield", "terrain-fixture", "--fluid25d-solver",
                               "finite-volume", "--fluid25d-terrain-crop-x", "1"}));
        },
        "mountain source/outlet demo should reject crop overrides");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-scenario", "mountain-source-outlet-demo",
                               "--terrain-heightfield", "terrain-fixture", "--fluid25d-solver",
                               "finite-volume", "--fluid25d-terrain-water-protocol", "none"}));
        },
        "mountain source/outlet demo should reject terrain-water protocol overrides");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-scenario", "mountain-source-outlet-demo",
                               "--terrain-heightfield", "terrain-fixture", "--fluid25d-solver",
                               "finite-volume", "--fluid25d-rainfall-rate-mm-per-hour", "1"}));
        },
        "mountain source/outlet demo should reject rainfall forcing overrides");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-scenario", "mountain-source-outlet-demo",
                               "--terrain-heightfield", "terrain-fixture", "--fluid25d-solver",
                               "finite-volume", "--fluid25d-sheet-depth-m", "0.01"}));
        },
        "mountain source/outlet demo should reject sheet-release forcing overrides");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-scenario", "mountain-source-outlet-demo",
                               "--terrain-heightfield", "terrain-fixture", "--fluid25d-solver",
                               "finite-volume", "--fluid25d-source-active-duration-seconds", "1"}));
        },
        "mountain source/outlet demo should reject terrain-water forcing duration overrides");

    const Fluid25DProjectConfig rain_pulse =
        parse_project({"fluid_25d", "--fluid25d-scenario", "terrain-case", "--terrain-heightfield",
                       "terrain-fixture", "--fluid25d-terrain-water-protocol", "rain-pulse",
                       "--fluid25d-rainfall-rate-mm-per-hour", "720",
                       "--fluid25d-source-active-duration-seconds", "2.5"});
    require(rain_pulse.simulation.terrain_water_protocol ==
                    Fluid25DTerrainWaterProtocol::RainPulse &&
                rain_pulse.simulation.rainfall_depth_rate_m_per_s > 0.0F &&
                rain_pulse.simulation.source_active_duration_seconds == 2.5F,
            "fluid 2.5D should retain the complete rain-pulse contract");
    const Fluid25DProjectConfig sheet_release =
        parse_project({"fluid_25d", "--fluid25d-scenario", "terrain-case", "--terrain-heightfield",
                       "terrain-fixture", "--fluid25d-terrain-water-protocol", "sheet-release",
                       "--fluid25d-sheet-depth-m", "0.025"});
    require(sheet_release.simulation.terrain_water_protocol ==
                    Fluid25DTerrainWaterProtocol::SheetRelease &&
                sheet_release.simulation.sheet_initial_depth_m == 0.025F,
            "fluid 2.5D should retain the complete sheet-release contract");
    require_throws(
        [] {
            static_cast<void>(parse_project({"fluid_25d", "--fluid25d-scenario", "terrain-case",
                                             "--terrain-heightfield", "terrain-fixture",
                                             "--fluid25d-terrain-water-protocol", "rain-pulse",
                                             "--fluid25d-rainfall-rate-mm-per-hour", "720"}));
        },
        "fluid 2.5D should require a fixed source duration for rain-pulse");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-terrain-water-protocol", "none"}));
        },
        "fluid 2.5D should reject explicit terrain-water protocol outside terrain-case");
    require_throws(
        [] {
            static_cast<void>(parse_project({"fluid_25d", "--fluid25d-scenario", "terrain-case",
                                             "--terrain-heightfield", "terrain-fixture",
                                             "--fluid25d-sheet-depth-m", "0.025"}));
        },
        "fluid 2.5D should reject forcing parameters with terrain protocol none");

    const Fluid25DProjectConfig diagnostic_presentation =
        parse_project({"fluid_25d", "--fluid25d-view", "diagnostics", "--debug-view", "wet-dry"});
    require(diagnostic_presentation.view == "diagnostics",
            "fluid 2.5D parser should preserve the selected presentation view");

    const Fluid25DProjectConfig validation = parse_project(
        {"fluid_25d", "--headless", "--debug-view", "wet-dry", "--fluid25d-gpu-oracle-validation"});
    require(validation.gpu_oracle_validation,
            "fluid 2.5D parser should retain the explicit GPU oracle switch");
    require(validation.debug_view == "wet-dry",
            "fluid 2.5D parser should retain the selected diagnostic view");
    require_throws(
        [] { static_cast<void>(parse_project({"fluid_25d", "--fluid25d-gpu-oracle-validation"})); },
        "fluid 2.5D should reject GPU oracle validation outside headless operation");
    require_throws(
        [] {
            static_cast<void>(parse_project(
                {"fluid_25d", "--profile-output", "fluid-profile", "--profile-diagnostics"}));
        },
        "fluid 2.5D should reject profile diagnostics outside headless operation");

    const Fluid25DProjectConfig deferred =
        parse_project({"fluid_25d", "--set", "grid.size=7", "--set", "fluid25d.scenario=dry-bed",
                       "--set", "fluid25d.simulation_substeps=4"});
    require(deferred.simulation.grid_width == 7 && deferred.simulation.grid_height == 7,
            "fluid 2.5D should support the shared grid-size override");
    require(deferred.simulation.scenario == Fluid25DScenario::DryBed,
            "fluid 2.5D should support deferred scenario overrides");
    require(deferred.simulation.simulation_substeps == 4,
            "fluid 2.5D should support deferred substep overrides");

    Fluid25DConfig invalid = defaults;
    invalid.fixed_delta_seconds = 0.0F;
    require_throws([&] { validate_fluid_25d_config(invalid); },
                   "fluid 2.5D should reject a zero fixed delta");
    invalid = defaults;
    invalid.simulation_substeps = kMaxFluid25DSubsteps + 1U;
    require_throws([&] { validate_fluid_25d_config(invalid); },
                   "fluid 2.5D should reject excessive substeps");
    invalid = defaults;
    invalid.scenario = static_cast<Fluid25DScenario>(99U);
    require_throws([&] { validate_fluid_25d_config(invalid); },
                   "fluid 2.5D should reject an invalid scenario enum");
    invalid = defaults;
    invalid.grid_height = 1U;
    require_throws([&] { validate_fluid_25d_config(invalid); },
                   "fluid 2.5D should reject product grids with fewer than two rows");
    invalid = defaults;
    invalid.source_active_duration_seconds = -0.1F;
    require_throws([&] { validate_fluid_25d_config(invalid); },
                   "fluid 2.5D should reject a negative source active duration");
}

void test_hillside_rain_study_config_and_cli() {
    using namespace cubey::projects::fluid::fluid_25d;

    const Fluid25DConfig rain = rain_study_test_config(512U, 512U, 12.0F);
    require(fluid_25d_scenario_from_name("hillside-rain-study") ==
                    Fluid25DScenario::HillsideRainStudy &&
                std::string(fluid_25d_scenario_name(Fluid25DScenario::HillsideRainStudy)) ==
                    "hillside-rain-study",
            "hillside rain should have its own stable opt-in scenario name");
    require(!fluid_25d_is_natural_terrain_study(Fluid25DScenario::HillsideRainStudy) &&
                fluid_25d_is_natural_terrain_study(Fluid25DScenario::HillsideFlowStudy),
            "rain must not inherit the point-source natural-flow recipe predicate");
    require(fluid_25d_is_macro_hillside_study(Fluid25DScenario::HillsideRainStudy),
            "rain should reuse hillside macro presentation helpers");
    require_close(rain.rainfall_depth_rate_m_per_s, 12.0 / 3'600'000.0, 1.0e-12,
                  "rain CLI rate should use the existing mm/hour conversion");

    const std::vector<std::string> rain_cli_base{"fluid_25d",
                                                 "--grid-width",
                                                 "512",
                                                 "--grid-height",
                                                 "512",
                                                 "--fluid25d-scenario",
                                                 "hillside-rain-study",
                                                 "--fluid25d-solver",
                                                 "finite-volume",
                                                 "--fluid25d-cell-size-m",
                                                 "30",
                                                 "--terrain-heightfield",
                                                 "terrain-fixture",
                                                 "--fluid25d-terrain-crop-x",
                                                 "1152",
                                                 "--fluid25d-terrain-crop-z",
                                                 "1408",
                                                 "--fluid25d-rainfall-rate-mm-per-hour",
                                                 "12",
                                                 "--fluid25d-hillside-camera",
                                                 "collection",
                                                 "--fluid25d-natural-flow-home-pitch-radians",
                                                 "-0.6",
                                                 "--fluid25d-hillside-depth-cues",
                                                 "--fluid25d-terrain-thin-water-composite",
                                                 "--fluid25d-motion-markers",
                                                 "--fluid25d-motion-marker-mode",
                                                 "local",
                                                 "--fluid25d-hillside-inspection-advance-seconds",
                                                 "2"};
    const auto rain_cli_with_option = [&](std::string_view option, std::string value) {
        std::vector<std::string> arguments = rain_cli_base;
        const auto found = std::find(arguments.begin(), arguments.end(), option);
        if (found == arguments.end() || found + 1 == arguments.end())
            throw std::runtime_error("test rain CLI option was not found");
        *(found + 1) = std::move(value);
        return arguments;
    };
    const auto rain_cli_with_extra = [&](std::string option, std::string value) {
        std::vector<std::string> arguments = rain_cli_base;
        arguments.push_back(std::move(option));
        arguments.push_back(std::move(value));
        return arguments;
    };
    const Fluid25DProjectConfig project = parse_project(rain_cli_base);
    require(project.simulation.scenario == Fluid25DScenario::HillsideRainStudy &&
                project.simulation.source_active_duration_seconds == std::nullopt &&
                project.terrain.crop_x == 1152U && project.terrain.crop_z == 1408U &&
                project.hillside_camera == "collection" &&
                project.natural_flow_home_pitch_radians == -0.6F &&
                project.catchment_render.hillside_depth_cues &&
                project.catchment_render.terrain_thin_water_composite && project.motion_markers &&
                project.motion_marker_mode == "local" &&
                project.hillside_inspection_advance_seconds == 2.0F,
            "rain CLI should admit pinned native terrain and presentation-only controls");

    const std::vector<std::string> source_camera =
        rain_cli_with_option("--fluid25d-hillside-camera", "source");
    require_throws([&] { static_cast<void>(parse_project(source_camera)); },
                   "rain must not expose source-relative camera presets");
    const std::vector<std::string> source_markers =
        rain_cli_with_option("--fluid25d-motion-marker-mode", "source");
    require_throws([&] { static_cast<void>(parse_project(source_markers)); },
                   "rain markers must use the local, not source, release mode");
    std::vector<std::string> missing_rate = rain_cli_base;
    const auto rainfall_option =
        std::find(missing_rate.begin(), missing_rate.end(), "--fluid25d-rainfall-rate-mm-per-hour");
    require(rainfall_option != missing_rate.end(), "rain CLI fixture should contain its rate");
    missing_rate.erase(rainfall_option, rainfall_option + 2);
    require_throws([&] { static_cast<void>(parse_project(missing_rate)); },
                   "rain scenario should require an explicit positive rainfall rate");
    const std::vector<std::string> neutral_protocol =
        rain_cli_with_extra("--fluid25d-terrain-water-protocol", "none");
    require_throws([&] { static_cast<void>(parse_project(neutral_protocol)); },
                   "rain should reject even an explicit neutral terrain-water protocol");
    const std::vector<std::string> point_source_q =
        rain_cli_with_extra("--fluid25d-natural-flow-source-m3-per-s", "100");
    require_throws([&] { static_cast<void>(parse_project(point_source_q)); },
                   "rain CLI should reject the point-source Q recipe option");
    const std::vector<std::string> dye_timing =
        rain_cli_with_extra("--fluid25d-dye-pulse-start-seconds", "0");
    std::vector<std::string> dye_timing_pair = dye_timing;
    dye_timing_pair.insert(dye_timing_pair.end(), {"--fluid25d-dye-pulse-duration-seconds", "1"});
    require_throws([&] { static_cast<void>(parse_project(dye_timing_pair)); },
                   "rain CLI should reject dye timing options");
    const std::vector<std::string> point_source_duration =
        rain_cli_with_extra("--fluid25d-source-active-duration-seconds", "5");
    require_throws([&] { static_cast<void>(parse_project(point_source_duration)); },
                   "rain CLI should reject source-active-duration scheduling");

    require_throws(
        [] {
            Fluid25DConfig invalid = rain_study_test_config();
            invalid.solver = Fluid25DSolver::VirtualPipes;
            validate_fluid_25d_config(invalid);
        },
        "rain should require the finite-volume solver");
    require_throws(
        [] {
            Fluid25DConfig invalid = rain_study_test_config();
            invalid.cell_size_m = 29.0F;
            validate_fluid_25d_config(invalid);
        },
        "rain should require exact native 30 m cells");
    require_throws(
        [] {
            Fluid25DConfig invalid = rain_study_test_config();
            invalid.source_active_duration_seconds = 5.0F;
            validate_fluid_25d_config(invalid);
        },
        "continuous rain should reject point-source duration scheduling");
    require_throws(
        [] {
            Fluid25DConfig invalid = rain_study_test_config();
            invalid.natural_flow_source_m3_per_s = 100.0F;
            validate_fluid_25d_config(invalid);
        },
        "rain should reject point-source Q");
    require_throws(
        [] {
            Fluid25DConfig invalid = rain_study_test_config();
            invalid.dye_pulse_start_seconds = 1.0F;
            invalid.dye_pulse_duration_seconds = 1.0F;
            validate_fluid_25d_config(invalid);
        },
        "rain should reject dye timing");
    require_throws(
        [] {
            Fluid25DConfig invalid = rain_study_test_config();
            invalid.terrain_water_protocol = Fluid25DTerrainWaterProtocol::RainPulse;
            validate_fluid_25d_config(invalid);
        },
        "rain should remain separate from TerrainCase RainPulse");
}

void test_dye_config_and_fixed_step_schedule() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config = finite_volume_test_config(6U, 3U, Fluid25DScenario::SourceOutletDemo);
    config.fixed_delta_seconds = 0.25F;
    config.dye_pulse_start_seconds = 0.50F;
    config.dye_pulse_duration_seconds = 0.50F;
    validate_fluid_25d_config(config);

    Fluid25DDyeSourceSchedule schedule;
    require(schedule.source_concentration(config) == 0.0F,
            "dye schedule should begin inactive before its pulse start");
    schedule.advance_fixed_step();
    require(schedule.source_concentration(config) == 0.0F,
            "dye schedule should remain inactive strictly before its pulse start");
    schedule.advance_fixed_step();
    require(schedule.source_concentration(config) == 1.0F,
            "dye schedule should activate exactly at its pulse start boundary");
    schedule.advance_fixed_step();
    require(schedule.source_concentration(config) == 1.0F,
            "dye schedule should remain active within its half-open pulse interval");
    schedule.advance_fixed_step();
    require(schedule.source_concentration(config) == 0.0F,
            "dye schedule should deactivate exactly at its pulse end boundary");
    schedule.reset();
    require(schedule.completed_steps() == 0U && schedule.source_concentration(config) == 0.0F,
            "dye schedule reset should restore its inactive zero-step state");

    const Fluid25DProjectConfig parsed =
        parse_project({"fluid_25d", "--fluid25d-scenario", "source-outlet-demo",
                       "--fluid25d-solver", "finite-volume", "--fluid25d-dye-pulse-start-seconds",
                       "0.5", "--fluid25d-dye-pulse-duration-seconds", "1.25"});
    require(parsed.simulation.dye_pulse_start_seconds == 0.5F &&
                parsed.simulation.dye_pulse_duration_seconds == 1.25F,
            "dye CLI options should bind the validated source-outlet pulse timing");
    const Fluid25DProjectConfig headwaters_parsed =
        parse_project({"fluid_25d", "--fluid25d-scenario", "sustained-headwaters-demo",
                       "--fluid25d-solver", "finite-volume", "--grid-width", "65", "--grid-height",
                       "33", "--fluid25d-cell-size-m", "4", "--fluid25d-dye-pulse-start-seconds",
                       "0.25", "--fluid25d-dye-pulse-duration-seconds", "0.5"});
    require(headwaters_parsed.simulation.dye_pulse_start_seconds == 0.25F &&
                headwaters_parsed.simulation.dye_pulse_duration_seconds == 0.5F &&
                fluid_25d_transport_inspection_available(headwaters_parsed.simulation),
            "dye CLI options should bind the opt-in sustained-headwaters pulse");
    require_throws(
        [] {
            static_cast<void>(parse_project(
                {"fluid_25d", "--fluid25d-scenario", "source-outlet-demo", "--fluid25d-solver",
                 "finite-volume", "--fluid25d-dye-pulse-start-seconds", "0.5"}));
        },
        "dye CLI should require pulse start and duration together");
    require_throws(
        [] {
            static_cast<void>(parse_project({"fluid_25d", "--fluid25d-scenario",
                                             "source-outlet-demo", "--fluid25d-solver",
                                             "finite-volume", "--fluid25d-dye-pulse-start-seconds",
                                             "0.5", "--fluid25d-dye-pulse-duration-seconds", "0"}));
        },
        "dye CLI should reject a nonpositive pulse duration");
    require_throws(
        [] {
            static_cast<void>(parse_project({"fluid_25d", "--fluid25d-scenario", "river-catchment",
                                             "--fluid25d-dye-pulse-start-seconds", "0",
                                             "--fluid25d-dye-pulse-duration-seconds", "1"}));
        },
        "dye CLI should reject timing options for non-demo scenarios");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-scenario", "source-outlet-demo",
                               "--fluid25d-dye-pulse-start-seconds", "0",
                               "--fluid25d-dye-pulse-duration-seconds", "1"}));
        },
        "dye CLI should reject timing options without finite-volume");
    require_throws(
        [] {
            static_cast<void>(parse_project(
                {"fluid_25d", "--fluid25d-scenario", "sustained-headwaters-demo",
                 "--fluid25d-solver", "finite-volume", "--grid-width", "65", "--grid-height", "33",
                 "--fluid25d-cell-size-m", "4", "--fluid25d-source-active-duration-seconds", "10",
                 "--fluid25d-dye-pulse-start-seconds", "0", "--fluid25d-dye-pulse-duration-seconds",
                 "1"}));
        },
        "headwater dye CLI should reject hydraulic source shutoff scheduling");
    require_throws(
        [] {
            static_cast<void>(parse_project(
                {"fluid_25d", "--fluid25d-scenario", "sustained-headwaters-demo",
                 "--fluid25d-solver", "finite-volume", "--grid-width", "65", "--grid-height", "33",
                 "--fluid25d-cell-size-m", "4", "--fluid25d-rainfall-rate-mm-per-hour", "10"}));
        },
        "headwater CLI should reject rain forcing");
    require_throws(
        [] {
            static_cast<void>(parse_project(
                {"fluid_25d", "--fluid25d-scenario", "sustained-headwaters-demo",
                 "--fluid25d-solver", "finite-volume", "--grid-width", "65", "--grid-height", "33",
                 "--fluid25d-cell-size-m", "4", "--fluid25d-sheet-depth-m", "0.1"}));
        },
        "headwater CLI should reject sheet forcing");
}

void test_mass_audit_cli_contract() {
    using namespace cubey::projects::fluid::fluid_25d;

    const Fluid25DProjectConfig audited = parse_project(
        {"fluid_25d", "--headless", "--fluid25d-solver", "finite-volume", "--fluid25d-mass-audit",
         "--profile-diagnostics", "--profile-output", "mass-audit-test"});
    require(audited.mass_audit && audited.simulation.mass_audit && audited.common.headless &&
                audited.common.profile_diagnostics &&
                audited.simulation.solver == Fluid25DSolver::FiniteVolume,
            "mass audit should parse only with its headless finite-volume diagnostic contract");

    for (const std::string control : {"source-basin", "dam-break"}) {
        const Fluid25DProjectConfig parsed = parse_project(
            {"fluid_25d", "--headless", "--fluid25d-solver", "finite-volume", "--fluid25d-scenario",
             "dry-bed", "--fluid25d-mass-audit-control", control});
        require(parsed.mass_audit_control == control && !parsed.mass_audit,
                "mass audit control fixtures should be selectable without enabling the audit");
    }
    const Fluid25DProjectConfig no_control =
        parse_project({"fluid_25d", "--fluid25d-mass-audit-control", "none"});
    require(no_control.mass_audit_control == "none",
            "the explicit no-control mass audit option should remain available");

    require_throws(
        [] {
            static_cast<void>(parse_project({"fluid_25d", "--fluid25d-solver", "finite-volume",
                                             "--fluid25d-mass-audit", "--profile-diagnostics",
                                             "--profile-output", "mass-audit-test"}));
        },
        "mass audit should reject windowed operation");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--headless", "--fluid25d-solver", "finite-volume",
                               "--fluid25d-mass-audit", "--profile-output", "mass-audit-test"}));
        },
        "mass audit should require profile diagnostics");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--headless", "--fluid25d-mass-audit",
                               "--profile-diagnostics", "--profile-output", "mass-audit-test"}));
        },
        "mass audit should require the finite-volume solver");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-mass-audit-control", "source-basin"}));
        },
        "mass audit controls should reject windowed operation");
    require_throws(
        [] {
            static_cast<void>(parse_project(
                {"fluid_25d", "--headless", "--fluid25d-mass-audit-control", "dam-break"}));
        },
        "mass audit controls should require the finite-volume solver");
    require_throws(
        [] {
            static_cast<void>(parse_project({"fluid_25d", "--headless", "--fluid25d-solver",
                                             "finite-volume", "--fluid25d-scenario", "lake-at-rest",
                                             "--fluid25d-mass-audit-control", "dam-break"}));
        },
        "mass audit controls should require the dry-bed scenario");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-mass-audit-control", "unknown"}));
        },
        "mass audit controls should reject unknown fixture names");
}

} // namespace cubey::projects::fluid::fluid_25d::test_detail
