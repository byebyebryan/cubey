#pragma once
#include "raytracing_scene.h"

namespace cubey::projects::raytracing {
// No runtime welding, orientation changes or degenerate removal: the exact
// serialized model must match the reviewed offline boundary certificate.
void admit_optical_dragon(Mesh&, const std::filesystem::path& model);
} // namespace cubey::projects::raytracing
