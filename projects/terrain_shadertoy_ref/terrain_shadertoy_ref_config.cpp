#include "terrain_shadertoy_ref_config.h"
#include "terrain_shadertoy_ref_camera.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string_view>

namespace cubey::projects::terrain_shadertoy_ref {
namespace {

[[nodiscard]] std::string_view require_value(int argc, char** argv, int& index,
                                             std::string_view option) {
    if (index + 1 >= argc) {
        throw std::runtime_error(std::string(option) + " requires a value");
    }
    ++index;
    return argv[index];
}

[[nodiscard]] float parse_time(std::string_view value) {
    std::size_t parsed = 0;
    const float result = std::stof(std::string(value), &parsed);
    if (parsed != value.size() || !std::isfinite(result) || result < 0.0F) {
        throw std::runtime_error("--reference-time requires a non-negative number");
    }
    return result;
}

[[nodiscard]] float parse_yaw(std::string_view value) {
    std::size_t parsed = 0;
    const float result = std::stof(std::string(value), &parsed);
    if (parsed != value.size() || !std::isfinite(result)) {
        throw std::runtime_error("--reference-yaw-offset-deg requires a finite number");
    }
    return normalize_reference_yaw_degrees(result);
}

[[nodiscard]] std::uint32_t parse_mesh_cells(std::string_view value) {
    constexpr std::array<std::pair<std::string_view, std::uint32_t>, 3> values{{
        {"256", 256U},
        {"512", 512U},
        {"1024", 1024U},
    }};
    for (const auto& [name, cells] : values) {
        if (value == name) {
            return cells;
        }
    }
    throw std::runtime_error("--reference-mesh-cells must be 256, 512, or 1024");
}

} // namespace

ParsedTerrainShadertoyRefArgs parse_terrain_shadertoy_ref_args(int argc, char** argv) {
    if (argc <= 0 || argv == nullptr || argv[0] == nullptr) {
        throw std::runtime_error("terrain ShaderToy reference requires argv[0]");
    }

    ParsedTerrainShadertoyRefArgs parsed;
    parsed.forwarded_arguments.emplace_back(argv[0]);
    for (int index = 1; index < argc; ++index) {
        const std::string_view option = argv[index];
        if (option == "--reference-render") {
            const std::string_view value = require_value(argc, argv, index, option);
            if (value == "raymarch") {
                parsed.reference_config.render = ReferenceRender::Raymarch;
            } else if (value == "mesh") {
                parsed.reference_config.render = ReferenceRender::Mesh;
            } else {
                throw std::runtime_error("--reference-render must be raymarch or mesh");
            }
        } else if (option == "--reference-time") {
            parsed.reference_config.reference_time_seconds =
                parse_time(require_value(argc, argv, index, option));
        } else if (option == "--reference-yaw-offset-deg") {
            parsed.reference_config.yaw_offset_degrees =
                parse_yaw(require_value(argc, argv, index, option));
        } else if (option == "--reference-mesh-cells") {
            parsed.reference_config.mesh_cells =
                parse_mesh_cells(require_value(argc, argv, index, option));
        } else if (option == "--reference-mesh-surface") {
            const std::string_view value = require_value(argc, argv, index, option);
            if (value == "terrain") {
                parsed.reference_config.mesh_surface = ReferenceMeshSurface::Terrain;
            } else if (value == "map") {
                parsed.reference_config.mesh_surface = ReferenceMeshSurface::Map;
            } else {
                throw std::runtime_error("--reference-mesh-surface must be terrain or map");
            }
        } else if (option == "--reference-normal") {
            const std::string_view value = require_value(argc, argv, index, option);
            if (value == "geometry") {
                parsed.reference_config.normal = ReferenceNormal::Geometry;
            } else if (value == "atlas") {
                parsed.reference_config.normal = ReferenceNormal::Atlas;
            } else if (value == "detailed") {
                parsed.reference_config.normal = ReferenceNormal::Detailed;
            } else {
                throw std::runtime_error(
                    "--reference-normal must be geometry, atlas, or detailed");
            }
        } else if (option == "--reference-shading") {
            const std::string_view value = require_value(argc, argv, index, option);
            if (value == "original") {
                parsed.reference_config.shading = ReferenceShading::Original;
            } else if (value == "clay") {
                parsed.reference_config.shading = ReferenceShading::Clay;
            } else {
                throw std::runtime_error("--reference-shading must be original or clay");
            }
        } else if (option == "--reference-diagnostic") {
            const std::string_view value = require_value(argc, argv, index, option);
            if (value == "final") {
                parsed.reference_config.diagnostic = ReferenceDiagnostic::Final;
            } else if (value == "height") {
                parsed.reference_config.diagnostic = ReferenceDiagnostic::Height;
            } else if (value == "slope") {
                parsed.reference_config.diagnostic = ReferenceDiagnostic::Slope;
            } else {
                throw std::runtime_error("--reference-diagnostic must be final, height, or slope");
            }
        } else {
            parsed.forwarded_arguments.emplace_back(argv[index]);
        }
    }
    if (parsed.reference_config.yaw_offset_degrees != 0.0F &&
        parsed.reference_config.render != ReferenceRender::Mesh) {
        throw std::runtime_error("reference yaw overrides require --reference-render mesh");
    }
    if (parsed.reference_config.yaw_offset_degrees != 0.0F &&
        parsed.reference_config.diagnostic != ReferenceDiagnostic::Final) {
        throw std::runtime_error("reference yaw overrides require final rendering");
    }
    return parsed;
}

} // namespace cubey::projects::terrain_shadertoy_ref
