#include "terrain_shadertoy_ref_config.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Callback> void require_throws(Callback&& callback, std::string_view message) {
    try {
        callback();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

[[nodiscard]] cubey::projects::terrain_shadertoy_ref::ParsedTerrainShadertoyRefArgs
parse(std::vector<std::string> arguments) {
    std::vector<char*> argv;
    argv.reserve(arguments.size());
    for (std::string& argument : arguments) {
        argv.push_back(argument.data());
    }
    return cubey::projects::terrain_shadertoy_ref::parse_terrain_shadertoy_ref_args(
        static_cast<int>(argv.size()), argv.data());
}

void test_defaults_and_forwarding() {
    using namespace cubey::projects::terrain_shadertoy_ref;
    const ParsedTerrainShadertoyRefArgs parsed =
        parse({"terrain_shadertoy_ref", "--headless", "--width", "640"});
    require(parsed.reference_config.render == ReferenceRender::Raymarch,
            "reference renderer should default to raymarch");
    require(parsed.reference_config.reference_time_seconds == 20.0F,
            "reference time should default to 20 seconds");
    require(parsed.reference_config.mesh_cells == 1024U,
            "reference grid should default to 1024 cells");
    require(parsed.reference_config.mesh_surface == ReferenceMeshSurface::Map,
            "reference mesh should default to Map geometry");
    require(parsed.reference_config.normal == ReferenceNormal::Detailed,
            "reference mesh should default to detailed normals");
    require(parsed.reference_config.shading == ReferenceShading::Original,
            "reference mesh should default to original shading");
    require(parsed.reference_config.diagnostic == ReferenceDiagnostic::Final,
            "reference mesh should default to final presentation");
    require(parsed.forwarded_arguments ==
                std::vector<std::string>{"terrain_shadertoy_ref", "--headless", "--width", "640"},
            "host arguments should be forwarded unchanged");
}

void test_full_reference_configuration() {
    using namespace cubey::projects::terrain_shadertoy_ref;
    const ParsedTerrainShadertoyRefArgs parsed = parse({
        "terrain_shadertoy_ref",
        "--reference-render",
        "mesh",
        "--reference-time",
        "40.5",
        "--reference-mesh-cells",
        "512",
        "--reference-mesh-surface",
        "terrain",
        "--reference-normal",
        "geometry",
        "--reference-shading",
        "clay",
        "--reference-diagnostic",
        "slope",
        "--headless",
    });
    require(parsed.reference_config.render == ReferenceRender::Mesh,
            "reference renderer should parse mesh");
    require(parsed.reference_config.reference_time_seconds == 40.5F,
            "reference time should parse decimals");
    require(parsed.reference_config.mesh_cells == 512U,
            "reference grid should parse supported sizes");
    require(parsed.reference_config.mesh_surface == ReferenceMeshSurface::Terrain,
            "reference surface should parse terrain");
    require(parsed.reference_config.normal == ReferenceNormal::Geometry,
            "reference normal should parse geometry");
    require(parsed.reference_config.shading == ReferenceShading::Clay,
            "reference shading should parse clay");
    require(parsed.reference_config.diagnostic == ReferenceDiagnostic::Slope,
            "reference diagnostic should parse slope");
    require(parsed.forwarded_arguments ==
                std::vector<std::string>{"terrain_shadertoy_ref", "--headless"},
            "reference arguments should be removed before host parsing");
}

void test_invalid_reference_options() {
    require_throws(
        [] { static_cast<void>(parse({"terrain_shadertoy_ref", "--reference-render", "tiles"})); },
        "invalid renderer should fail");
    require_throws(
        [] { static_cast<void>(parse({"terrain_shadertoy_ref", "--reference-time", "-1"})); },
        "negative time should fail");
    require_throws(
        [] {
            static_cast<void>(parse({"terrain_shadertoy_ref", "--reference-mesh-cells", "2048"}));
        },
        "unsupported mesh size should fail");
    require_throws(
        [] { static_cast<void>(parse({"terrain_shadertoy_ref", "--reference-diagnostic"})); },
        "missing diagnostic value should fail");
}

} // namespace

int main() {
    try {
        test_defaults_and_forwarding();
        test_full_reference_configuration();
        test_invalid_reference_options();
        std::cout << "terrain_shadertoy_ref tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "terrain_shadertoy_ref tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
