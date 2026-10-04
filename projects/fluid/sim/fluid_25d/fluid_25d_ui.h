#pragma once

#include "fluid_25d_config.h"
#include "fluid_25d_hillside_supply.h"
#include "fluid_25d_rain_study.h"

namespace cubey::projects::fluid::fluid_25d {

struct Fluid25DBackendMetadata;

struct Fluid25DUiContext {
    const char* title = nullptr;
    Fluid25DScenario scenario = Fluid25DScenario::RiverCatchment;
    Fluid25DSolver solver = Fluid25DSolver::VirtualPipes;
    const Fluid25DBackendMetadata* backend_metadata = nullptr;
    bool transport_inspection_available = false;
    Fluid25DPresentationView& presentation_view;
    Fluid25DCatchmentView& catchment_view;
    Fluid25DDebugView& debug_view;
    float& presentation_time_scale;
    Fluid25DWindowedPacing& windowed_pacing;
    Fluid25DInspectionAdvance& inspection_advance;
    float simulation_elapsed_seconds = 0.0F;
    float fixed_delta_seconds = 0.0F;
    float continuous_source_m3_per_s = 0.0F;
    Fluid25DHillsideSupply& hillside_supply;
    Fluid25DRainStudyControl& rain_study;
    double rainfall_rate_mm_per_hour = 0.0;
    double rainfall_total_input_m3_per_s = 0.0;
    bool hillside_depth_cues = false;
    bool downstream_hillside_cameras_available = false;
    bool& hillside_source_context;
    std::string& hillside_camera;
    bool& resume_after_advance;
    bool motion_markers_available = false;
    bool local_motion_markers = false;
    bool& show_motion_markers;
    bool dye_enabled = false;
    float dye_start_seconds = 0.0F;
    float dye_end_seconds = 0.0F;
    bool& paused;
    bool& stopped;
    bool backend_failed = false;
    bool& reset_requested;
    bool& presentation_cue_reset_requested;
    bool& quiver_reset_requested;
};

void draw_fluid_25d_ui(Fluid25DUiContext ui);

} // namespace cubey::projects::fluid::fluid_25d
