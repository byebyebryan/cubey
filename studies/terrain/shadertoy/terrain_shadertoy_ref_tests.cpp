#include "terrain_shadertoy_ref_camera.h"
#include "terrain_shadertoy_ref_config.h"

#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <numbers>
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

void require_near(float actual, float expected, float tolerance, std::string_view message) {
    require(std::abs(actual - expected) <= tolerance, message);
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

void test_defaults() {
    using namespace cubey::projects::terrain_shadertoy_ref;
    const ParsedTerrainShadertoyRefArgs parsed = parse({"terrain_shadertoy_ref"});
    require(parsed.reference_config.study == ReferenceStudy::Mountains,
            "reference study should default to Mountains");
    require(parsed.reference_config.render == ReferenceRender::Raymarch,
            "reference renderer should default to raymarch");
    require(parsed.reference_config.reference_time_seconds == 20.0F,
            "reference time should default to 20 seconds");
    require(parsed.reference_config.yaw_offset_degrees == 0.0F,
            "reference yaw should default to the source view");
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
}

void test_full_reference_configuration() {
    using namespace cubey::projects::terrain_shadertoy_ref;
    const ParsedTerrainShadertoyRefArgs parsed = parse({
        "terrain_shadertoy_ref",
        "--reference-render",
        "mesh",
        "--reference-time",
        "40.5",
        "--reference-yaw-offset-deg",
        "450",
        "--reference-mesh-cells",
        "512",
        "--reference-mesh-surface",
        "terrain",
        "--reference-normal",
        "atlas",
        "--reference-shading",
        "clay",
        "--reference-diagnostic",
        "final",
    });
    require(parsed.reference_config.render == ReferenceRender::Mesh,
            "reference renderer should parse mesh");
    require(parsed.reference_config.reference_time_seconds == 40.5F,
            "reference time should parse decimals");
    require(parsed.reference_config.yaw_offset_degrees == 90.0F,
            "reference yaw should normalize full rotations");
    require(parsed.reference_config.mesh_cells == 512U,
            "reference grid should parse supported sizes");
    require(parsed.reference_config.mesh_surface == ReferenceMeshSurface::Terrain,
            "reference surface should parse terrain");
    require(parsed.reference_config.normal == ReferenceNormal::Atlas,
            "reference normal should parse atlas");
    require(parsed.reference_config.shading == ReferenceShading::Clay,
            "reference shading should parse clay");
    require(parsed.reference_config.diagnostic == ReferenceDiagnostic::Final,
            "reference diagnostic should parse final");
}

void test_mountains_component_diagnostics() {
    using namespace cubey::projects::terrain_shadertoy_ref;
    const ParsedTerrainShadertoyRefArgs envelope =
        parse({"terrain_shadertoy_ref", "--reference-render", "mesh", "--reference-diagnostic",
               "envelope"});
    const ParsedTerrainShadertoyRefArgs structure =
        parse({"terrain_shadertoy_ref", "--reference-render", "mesh", "--reference-diagnostic",
               "structure"});
    const ParsedTerrainShadertoyRefArgs uplift =
        parse({"terrain_shadertoy_ref", "--reference-render", "mesh", "--reference-diagnostic",
               "uplift"});
    require(envelope.reference_config.diagnostic == ReferenceDiagnostic::Envelope &&
                structure.reference_config.diagnostic == ReferenceDiagnostic::Structure &&
                uplift.reference_config.diagnostic == ReferenceDiagnostic::Uplift,
            "Mountains component diagnostics should parse");
    require_throws(
        [] {
            static_cast<void>(parse({"terrain_shadertoy_ref", "--reference-study", "swiss-alps",
                                     "--reference-diagnostic", "envelope"}));
        },
        "non-Mountains component diagnostic should fail");
}

void test_non_mountains_study_defaults() {
    using namespace cubey::projects::terrain_shadertoy_ref;
    const ParsedTerrainShadertoyRefArgs swiss =
        parse({"terrain_shadertoy_ref", "--reference-study", "swiss-alps"});
    require(swiss.reference_config.study == ReferenceStudy::SwissAlps,
            "Swiss Alps study should parse");
    require(swiss.reference_config.render == ReferenceRender::Mesh &&
                swiss.reference_config.mesh_surface == ReferenceMeshSurface::Terrain &&
                swiss.reference_config.normal == ReferenceNormal::Atlas &&
                swiss.reference_config.shading == ReferenceShading::Clay,
            "non-Mountains studies should select the comparison defaults");
    require(reference_study_from_name("mountain-peak") == ReferenceStudy::MountainPeak &&
                reference_study_from_name("erosion-filter") == ReferenceStudy::ErosionFilter &&
                reference_study_name(ReferenceStudy::Mountains) == "mountains",
            "all source-shape study names should round trip");
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
    require_throws(
        [] { static_cast<void>(parse({"terrain_shadertoy_ref", "--reference-study", "unknown"})); },
        "unknown reference study should fail");
    require_throws(
        [] {
            static_cast<void>(parse({"terrain_shadertoy_ref", "--reference-study", "swiss-alps",
                                     "--reference-render", "raymarch"}));
        },
        "Swiss Alps raymarch request should fail");
    require_throws(
        [] {
            static_cast<void>(parse({"terrain_shadertoy_ref", "--reference-study", "mountain-peak",
                                     "--reference-normal", "detailed"}));
        },
        "Mountain Peak detailed-normal request should fail");
    require_throws(
        [] {
            static_cast<void>(parse({"terrain_shadertoy_ref", "--reference-study", "erosion-filter",
                                     "--reference-shading", "original"}));
        },
        "erosion original-shading request should fail");
    require_throws(
        [] {
            static_cast<void>(
                parse({"terrain_shadertoy_ref", "--reference-yaw-offset-deg", "nan"}));
        },
        "non-finite yaw should fail");
    require_throws(
        [] {
            static_cast<void>(parse({"terrain_shadertoy_ref", "--reference-yaw-offset-deg", "90"}));
        },
        "raymarch yaw override should fail");
    require_throws(
        [] {
            static_cast<void>(
                parse({"terrain_shadertoy_ref", "--reference-render", "mesh",
                       "--reference-yaw-offset-deg", "90", "--reference-diagnostic", "height"}));
        },
        "diagnostic yaw override should fail");
}

void test_reference_camera_yaw() {
    using namespace cubey::projects::terrain_shadertoy_ref;
    const ReferenceCamera camera{
        .position = {1.0F, 2.0F, 3.0F},
        .right = {1.0F, 0.0F, 0.0F},
        .up = {0.0F, 1.0F, 0.0F},
        .forward = {0.0F, 0.0F, -1.0F},
        .focus_distance = 10.0F,
    };
    const ReferenceCamera identity = rotate_reference_camera_yaw(camera, 360.0F);
    require(identity.position == camera.position && identity.right == camera.right &&
                identity.up == camera.up && identity.forward == camera.forward,
            "full yaw rotation should retain the exact source camera");

    const ReferenceCamera rotated = rotate_reference_camera_yaw(camera, 90.0F);
    require(rotated.position == camera.position, "yaw should not translate the source camera");
    require_near(rotated.forward.x, -1.0F, 0.00001F,
                 "positive yaw should rotate forward around world up");
    require_near(rotated.forward.y, 0.0F, 0.00001F, "yaw should preserve forward elevation");
    require_near(rotated.forward.z, 0.0F, 0.00001F,
                 "quarter yaw should rotate forward off source z");
    require_near(glm::length(rotated.right), 1.0F, 0.00001F,
                 "yaw should preserve right basis length");
    require_near(glm::dot(rotated.right, rotated.up), 0.0F, 0.00001F,
                 "yaw should preserve an orthogonal camera basis");

    const ReferenceCamera orbit_identity = orbit_reference_camera(camera, 0.0F, 0.0F, 10.0F);
    require(orbit_identity.position == camera.position && orbit_identity.right == camera.right &&
                orbit_identity.up == camera.up && orbit_identity.forward == camera.forward,
            "zero inspection orbit should retain the exact source camera");

    const ReferenceCamera orbit =
        orbit_reference_camera(camera, std::numbers::pi_v<float> * 0.5F, 0.0F, 20.0F);
    const cubey::math::Vec3 target = camera.position + camera.forward * camera.focus_distance;
    require_near(glm::length(target - orbit.position), 20.0F, 0.0001F,
                 "inspection orbit should honor its requested distance");
    require_near(glm::dot(glm::normalize(target - orbit.position), orbit.forward), 1.0F, 0.0001F,
                 "inspection orbit should keep looking at the source target");
}

} // namespace

int main() {
    try {
        test_defaults();
        test_full_reference_configuration();
        test_non_mountains_study_defaults();
        test_mountains_component_diagnostics();
        test_invalid_reference_options();
        test_reference_camera_yaw();
        std::cout << "terrain_shadertoy_ref tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "terrain_shadertoy_ref tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
