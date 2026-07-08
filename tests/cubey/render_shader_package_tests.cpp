#include "source_file_test_helpers.h"

#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::filesystem::path source_root() {
    return std::filesystem::path{CUBEY_SOURCE_DIR};
}

std::vector<std::string> shader_includes(const std::string& source) {
    std::vector<std::string> includes;
    std::istringstream lines{source};
    std::string line;
    while (std::getline(lines, line)) {
        const std::size_t include_pos = line.find("#include");
        if (include_pos == std::string::npos) {
            continue;
        }
        const std::size_t first_quote = line.find('"', include_pos);
        if (first_quote == std::string::npos) {
            continue;
        }
        const std::size_t second_quote = line.find('"', first_quote + 1U);
        if (second_quote == std::string::npos) {
            continue;
        }
        includes.push_back(line.substr(first_quote + 1U, second_quote - first_quote - 1U));
    }
    return includes;
}

std::filesystem::path include_dependency_path(const std::filesystem::path& shader_path,
                                              std::string_view include_path) {
    constexpr std::string_view cubey_prefix{"cubey/"};
    if (include_path.starts_with(cubey_prefix)) {
        return (source_root() / "shaders" / std::string{include_path}).lexically_normal();
    }
    return (shader_path.parent_path() / std::string{include_path}).lexically_normal();
}

std::string dependency_needle(const std::filesystem::path& dependency_path) {
    return std::filesystem::relative(dependency_path, source_root()).generic_string();
}

void require_package_tracks_includes(const std::string& cmake_source,
                                     const std::vector<std::filesystem::path>& shader_files,
                                     std::string_view package_name) {
    for (const std::filesystem::path& shader_file : shader_files) {
        const std::filesystem::path shader_path = source_root() / shader_file;
        const std::string shader_source = cubey::tests::read_source_file(shader_path);
        for (const std::string& include_path : shader_includes(shader_source)) {
            const std::filesystem::path dependency_path =
                include_dependency_path(shader_path, include_path);
            const std::string needle = dependency_needle(dependency_path);
            if (cmake_source.find(needle) == std::string::npos) {
                throw std::runtime_error(std::string{package_name} +
                                         " shader package does not track include " + needle +
                                         " from " + shader_file.generic_string());
            }
        }
    }
}

} // namespace

void test_shader_package_dependencies_track_atmosphere_includes() {
    const std::string cmake_source =
        cubey::tests::read_source_file(source_root() / "cmake/CubeyShaders.cmake");

    require_package_tracks_includes(
        cmake_source,
        {
            "shaders/cubey/atmosphere/atmosphere.frag",
            "shaders/cubey/atmosphere/atmosphere_common.glsl",
            "shaders/cubey/atmosphere/atmosphere_night_sky.glsl",
            "shaders/cubey/atmosphere/atmosphere_sun.glsl",
            "shaders/cubey/atmosphere/atmosphere_ground.glsl",
            "shaders/cubey/atmosphere/atmosphere_debug.glsl",
        },
        "atmosphere");
}

void test_shader_package_dependencies_track_cloud_layer_includes() {
    const std::string cmake_source =
        cubey::tests::read_source_file(source_root() / "cmake/CubeyShaders.cmake");

    require_package_tracks_includes(
        cmake_source,
        {
            "shaders/cubey/cloud/cloud_composite_background.frag",
            "shaders/cubey/cloud/cloud_composite_background_depth.frag",
            "shaders/cubey/cloud/cloud_march.comp",
            "shaders/cubey/cloud/surface_cloud_march.comp",
            "shaders/cubey/cloud/cloud_perlin_worley.comp",
            "shaders/cubey/cloud/cloud_weather.comp",
            "shaders/cubey/cloud/cloud_worley.comp",
            "shaders/cubey/cloud/cloud_common.glsl",
            "shaders/cubey/cloud/cloud_composite_post.glsl",
            "shaders/cubey/cloud/cloud_noise_common.glsl",
            "shaders/cubey/cloud/cloud_resolve_common.glsl",
            "shaders/cubey/cloud/cloud_weather_common.glsl",
        },
        "cloud");
}

void test_shader_package_dependencies_track_forward_pbr_includes() {
    const std::string cmake_source =
        cubey::tests::read_source_file(source_root() / "cmake/CubeyShaders.cmake");

    require_package_tracks_includes(
        cmake_source,
        {
            "shaders/cubey/forward_pbr/forward_pbr.vert",
            "shaders/cubey/forward_pbr/forward_pbr.frag",
            "shaders/cubey/forward_pbr/forward_pbr_skybox.vert",
            "shaders/cubey/forward_pbr/forward_pbr_skybox.frag",
            "shaders/cubey/atmosphere/atmosphere.frag",
            "shaders/cubey/atmosphere_reflection_prefilter.frag",
            "shaders/cubey/forward_pbr/forward_pbr_post.vert",
            "shaders/cubey/forward_pbr/forward_pbr_post.frag",
            "shaders/cubey/forward_pbr/forward_pbr_shadow_depth.vert",
            "shaders/cubey/forward_pbr/forward_pbr_shadow_depth.frag",
        },
        "forward PBR");
}
