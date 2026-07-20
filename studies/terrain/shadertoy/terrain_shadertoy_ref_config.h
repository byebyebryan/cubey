#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cubey::projects::terrain_shadertoy_ref {

enum class ReferenceStudy : std::uint8_t {
    Mountains,
    SwissAlps,
    MountainPeak,
    ErosionFilter,
};

enum class ReferenceRender : std::uint8_t {
    Raymarch,
    Mesh,
};

enum class ReferenceMeshSurface : std::uint8_t {
    Terrain,
    Map,
};

enum class ReferenceNormal : std::uint8_t {
    Geometry,
    Atlas,
    Detailed,
};

enum class ReferenceShading : std::uint8_t {
    Original,
    Clay,
};

enum class ReferenceDiagnostic : std::uint8_t {
    Final,
    Height,
    Slope,
    Envelope,
    Structure,
    Uplift,
};

struct TerrainShadertoyRefConfig {
    ReferenceStudy study = ReferenceStudy::Mountains;
    ReferenceRender render = ReferenceRender::Raymarch;
    ReferenceMeshSurface mesh_surface = ReferenceMeshSurface::Map;
    ReferenceNormal normal = ReferenceNormal::Detailed;
    ReferenceShading shading = ReferenceShading::Original;
    ReferenceDiagnostic diagnostic = ReferenceDiagnostic::Final;
    float reference_time_seconds = 20.0F;
    float yaw_offset_degrees = 0.0F;
    std::uint32_t mesh_cells = 1024U;
};

[[nodiscard]] std::string_view reference_study_name(ReferenceStudy study);
[[nodiscard]] ReferenceStudy reference_study_from_name(std::string_view name);

struct ParsedTerrainShadertoyRefArgs {
    TerrainShadertoyRefConfig reference_config{};
    std::vector<std::string> forwarded_arguments{};
};

[[nodiscard]] ParsedTerrainShadertoyRefArgs parse_terrain_shadertoy_ref_args(int argc, char** argv);

} // namespace cubey::projects::terrain_shadertoy_ref
