#include "source_file_test_helpers.h"

#include <filesystem>
#include <initializer_list>
#include <string>

namespace {

using cubey::tests::read_source_file;
using cubey::tests::require_contains;
using cubey::tests::require_not_contains;

std::string read_example_sources(const std::filesystem::path& root,
                                 std::initializer_list<const char*> paths) {
    std::string result;
    for (const char* path : paths) {
        result += read_source_file(root / path);
    }
    return result;
}

} // namespace

void test_example_lighting_uses_low_linear_ambient_terms() {
    const std::filesystem::path root{CUBEY_SOURCE_DIR};
    const std::string instanced =
        read_source_file(root / "examples/instanced_cubes/shaders/instanced_cubes.frag");
    const std::string particle =
        read_source_file(root / "examples/particle_cubes/shaders/particle_cubes.frag");
    const std::string shadow =
        read_source_file(root / "examples/shadow_cube/shaders/shadow_cube.frag");
    const std::string textured_app =
        read_example_sources(root, {"examples/textured_cube/textured_cube_app_internal.h",
                                    "examples/textured_cube/textured_cube_resources.cpp",
                                    "examples/textured_cube/textured_cube_scene.cpp",
                                    "examples/textured_cube/textured_cube_render.cpp"});

    for (const std::string* shader : {&instanced, &particle, &shadow}) {
        require_contains(*shader, "kAmbientRadiance",
                         "simple example shaders should name their linear ambient term");
        require_contains(*shader, "0.045",
                         "simple example shaders should use a low linear ambient term");
        require_not_contains(*shader, "vec3(0.22)",
                             "simple example shaders should not use old display-space ambient");
    }

    require_contains(textured_app, ".ambient_color = {0.045F, 0.045F, 0.045F}",
                     "textured cube should pass a low linear ambient color");
    require_not_contains(textured_app, ".ambient_color = {0.24F, 0.24F, 0.24F}",
                         "textured cube should not pass display-space ambient as linear");
}

void test_smoke_tests_fail_on_vulkan_validation_errors() {
    const std::filesystem::path root{CUBEY_SOURCE_DIR};
    const std::string smoke = read_source_file(root / "cmake/CubeySmokeTests.cmake");

    require_contains(smoke, "vulkan validation error",
                     "smoke tests should fail when validation reports Vulkan errors");
    require_contains(smoke, "set_tests_properties(\"${name}\" PROPERTIES TIMEOUT 20)",
                     "PNG/video smoke tests should allow heavier GPU startup paths");
    require_not_contains(smoke, "|vkCreateInstance",
                         "smoke tests should not treat generic Vulkan instance failures as no-GPU skips");
}

void test_pbr_furnace_headless_path_transitions_depth_attachment() {
    const std::filesystem::path root{CUBEY_SOURCE_DIR};
    const std::string app = read_source_file(root / "projects/pbr_furnace/pbr_furnace_render.cpp");

    require_contains(app, "begin_depth_attachment_transition",
                     "pbr_furnace headless target path should transition its depth attachment");
}

void test_hostless_cmake_defaults_disable_host_dependent_targets() {
    const std::filesystem::path root{CUBEY_SOURCE_DIR};
    const std::string cmake = read_source_file(root / "CMakeLists.txt");

    require_contains(cmake,
                     "option(CUBEY_BUILD_EXAMPLES \"Build Cubey examples\" ${CUBEY_BUILD_HOST})",
                     "hostless builds should default examples off with the host layer");
    require_contains(cmake,
                     "option(CUBEY_BUILD_PROJECTS \"Build Cubey projects\" ${CUBEY_BUILD_HOST})",
                     "hostless builds should default projects off with the host layer");
    require_contains(cmake, "set(BUILD_TESTING ${CUBEY_BUILD_HOST}",
                     "hostless builds should default tests off with the host layer");
}

void test_render_app_dynamic_rendering_scan_covers_built_projects() {
    const std::filesystem::path root{CUBEY_SOURCE_DIR};
    const std::string cmake = read_source_file(root / "CMakeLists.txt");

    require_contains(cmake, "NAME render_apps_use_dynamic_rendering",
                     "dynamic-rendering hygiene test should name examples and projects");
    require_contains(cmake, "BUILD_TESTING AND (CUBEY_BUILD_EXAMPLES OR CUBEY_BUILD_PROJECTS)",
                     "dynamic-rendering hygiene test should register for project-only builds");
    require_contains(cmake, "CUBEY_DYNAMIC_RENDERING_SCAN_DIRS",
                     "dynamic-rendering hygiene test should build scan dirs conditionally");
    require_contains(cmake, "projects/gltf_viewer",
                     "dynamic-rendering hygiene test should scan the glTF viewer");
    require_contains(cmake, "projects/ocean_ref",
                     "dynamic-rendering hygiene test should scan the ocean reference project");
    require_contains(cmake, "projects/ocean_exp",
                     "dynamic-rendering hygiene test should scan the ocean experimental snapshot");
    require_contains(cmake, "projects/ocean_legacy",
                     "dynamic-rendering hygiene test should scan the ocean legacy project");
    require_contains(cmake, "projects/procedural_terrain",
                     "dynamic-rendering hygiene test should scan procedural terrain");
}
