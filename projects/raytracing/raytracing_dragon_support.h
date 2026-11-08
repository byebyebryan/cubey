#pragma once

#include "raytracing_scene.h"

namespace cubey::projects::raytracing {

// Flat-capped stone slab with irregular cut edges around the lowest contact band.
// Geometry is in world space after the subject's vertical placement.
Mesh make_dragon_rock_footing(const Mesh& dragon, float dragon_y);

} // namespace cubey::projects::raytracing
