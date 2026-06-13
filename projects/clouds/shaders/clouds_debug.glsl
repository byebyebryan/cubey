#ifndef CUBEY_CLOUDS_DEBUG_GLSL
#define CUBEY_CLOUDS_DEBUG_GLSL

const int CLOUDS_VIEW_FINAL = 0;
const int CLOUDS_VIEW_WEATHER = 1;
const int CLOUDS_VIEW_DENSITY = 2;
const int CLOUDS_VIEW_TRANSMITTANCE = 3;
const int CLOUDS_VIEW_LIGHTING = 4;
const int CLOUDS_VIEW_SHADOW = 5;
const int CLOUDS_VIEW_STEPS = 6;
const int CLOUDS_VIEW_BACKGROUND = 7;
const int CLOUDS_VIEW_ATMOSPHERE = 8;
const int CLOUDS_VIEW_GROUND = 9;
const int CLOUDS_VIEW_GROUND_HIT = 10;
const int CLOUDS_VIEW_CLOUD_ALPHA = 11;
const int CLOUDS_VIEW_SHELL = 12;
const int CLOUDS_VIEW_SURFACE_SHADOW = 13;
const int CLOUDS_VIEW_DOMAIN = 14;
const int CLOUDS_VIEW_DISTANCE = 15;
const int CLOUDS_VIEW_BASE_DENSITY = 16;
const int CLOUDS_VIEW_DETAIL_DENSITY = 17;
const int CLOUDS_VIEW_DENSITY_LOD = 18;
const int CLOUDS_VIEW_STEP_LENGTH = 19;
const int CLOUDS_VIEW_LOCAL_MARCH = 20;
const int CLOUDS_VIEW_FAR_HORIZON = 21;

bool cloud_product_debug_view(int debug_view) {
    return debug_view == CLOUDS_VIEW_WEATHER || debug_view == CLOUDS_VIEW_DENSITY ||
           debug_view == CLOUDS_VIEW_TRANSMITTANCE || debug_view == CLOUDS_VIEW_LIGHTING ||
           debug_view == CLOUDS_VIEW_SHADOW || debug_view == CLOUDS_VIEW_STEPS ||
           debug_view == CLOUDS_VIEW_CLOUD_ALPHA || debug_view == CLOUDS_VIEW_SHELL ||
           debug_view == CLOUDS_VIEW_DOMAIN || debug_view == CLOUDS_VIEW_DISTANCE ||
           debug_view == CLOUDS_VIEW_BASE_DENSITY ||
           debug_view == CLOUDS_VIEW_DETAIL_DENSITY ||
           debug_view == CLOUDS_VIEW_DENSITY_LOD ||
           debug_view == CLOUDS_VIEW_STEP_LENGTH ||
           debug_view == CLOUDS_VIEW_LOCAL_MARCH ||
           debug_view == CLOUDS_VIEW_FAR_HORIZON;
}

#endif
