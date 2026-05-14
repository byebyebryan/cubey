#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string read_source_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("failed to open source file: " + path.string());
    }
    return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

void require_contains(const std::string& text, const std::string& needle,
                      const char* message) {
    require(text.find(needle) != std::string::npos, message);
}

void require_not_contains(const std::string& text, const std::string& needle,
                          const char* message) {
    require(text.find(needle) == std::string::npos, message);
}

} // namespace

void test_example_lighting_uses_low_linear_ambient_terms() {
    const std::filesystem::path root{CUBEY_SOURCE_DIR};
    const std::string material =
        read_source_file(root / "examples/material_cubes/shaders/material_cubes.frag");
    const std::string instanced =
        read_source_file(root / "examples/instanced_cubes/shaders/instanced_cubes.frag");
    const std::string particle =
        read_source_file(root / "examples/particle_cubes/shaders/particle_cubes.frag");
    const std::string shadow =
        read_source_file(root / "examples/shadow_cube/shaders/shadow_cube.frag");
    const std::string textured_app =
        read_source_file(root / "examples/textured_cube/textured_cube_app.cpp");

    for (const std::string* shader : {&material, &instanced, &particle, &shadow}) {
        require_contains(*shader, "kAmbientRadiance",
                         "simple example shaders should name their linear ambient term");
        require_contains(*shader, "0.045",
                         "simple example shaders should use a low linear ambient term");
        require_not_contains(*shader, "vec3(0.22)",
                             "simple example shaders should not use old display-space ambient");
        require_not_contains(*shader, "0.24 + 0.88",
                             "material cubes should not use old display-space lighting factors");
    }

    require_contains(textured_app, ".ambient_color = {0.045F, 0.045F, 0.045F}",
                     "textured cube should pass a low linear ambient color");
    require_not_contains(textured_app, ".ambient_color = {0.24F, 0.24F, 0.24F}",
                         "textured cube should not pass display-space ambient as linear");
}
