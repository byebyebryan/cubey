#pragma once

#include "fluid_25d_config.h"

namespace cubey::projects::fluid::fluid_25d {

struct Fluid25DUiContext {
    const char* title = nullptr;
    Fluid25DScenario scenario = Fluid25DScenario::RiverCatchment;
    bool transport_inspection_available = false;
    Fluid25DPresentationView& presentation_view;
    Fluid25DCatchmentView& catchment_view;
    Fluid25DDebugView& debug_view;
    float& presentation_time_scale;
    Fluid25DWindowedPacing& windowed_pacing;
    Fluid25DInspectionAdvance& inspection_advance;
    float simulation_elapsed_seconds = 0.0F;
    float fixed_delta_seconds = 0.0F;
    bool& hillside_source_context;
    bool& paused;
    bool& reset_requested;
    bool& presentation_cue_reset_requested;
    bool& quiver_reset_requested;
};

void draw_fluid_25d_ui(Fluid25DUiContext ui);

} // namespace cubey::projects::fluid::fluid_25d
