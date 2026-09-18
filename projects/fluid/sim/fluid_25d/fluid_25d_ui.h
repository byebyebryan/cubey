#pragma once

#include "fluid_25d_config.h"

namespace cubey::projects::fluid::fluid_25d {

struct Fluid25DUiContext {
    const char* title = nullptr;
    Fluid25DPresentationView& presentation_view;
    Fluid25DCatchmentView& catchment_view;
    Fluid25DDebugView& debug_view;
    float& presentation_time_scale;
    Fluid25DWindowedPacing& windowed_pacing;
    bool& paused;
    bool& reset_requested;
    bool& presentation_cue_reset_requested;
    bool& streamlet_reset_requested;
};

void draw_fluid_25d_ui(Fluid25DUiContext ui);

} // namespace cubey::projects::fluid::fluid_25d
