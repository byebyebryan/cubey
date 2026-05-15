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
    const std::string textured_app = read_example_sources(
        root, {"examples/textured_cube/textured_cube_app_internal.h",
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
