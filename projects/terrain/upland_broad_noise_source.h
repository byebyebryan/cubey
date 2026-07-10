#pragma once

#include <cubey/procedural/field_2d.h>
#include <cubey/procedural/field_set_2d.h>

#include <cstdint>

namespace cubey::projects::terrain {

[[nodiscard]] cubey::procedural::FieldSet2D
sample_upland_broad_noise_fields_v1(cubey::procedural::Grid2DDesc desc, std::uint64_t seed);

} // namespace cubey::projects::terrain
