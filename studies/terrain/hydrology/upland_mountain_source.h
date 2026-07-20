#pragma once

#include <cubey/procedural/field_2d.h>
#include <cubey/procedural/field_set_2d.h>

#include <cstdint>

namespace cubey::projects::terrain_hydrology_lab {

struct UplandMountainSampleV1 {
    float height_m = 0.0F;
    float support = 0.0F;
};

[[nodiscard]] UplandMountainSampleV1 sample_upland_mountain_v1(float world_x, float world_z,
                                                               std::uint64_t seed);
[[nodiscard]] cubey::procedural::FieldSet2D
sample_upland_mountain_fields_v1(cubey::procedural::Grid2DDesc desc, std::uint64_t seed);

} // namespace cubey::projects::terrain_hydrology_lab
