#include <cubey/render/cloud_layer.h>
#include <cubey/render/cloud_layer_config.h>

#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(float actual, float expected, float tolerance, const char* message) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

std::filesystem::path source_root_path() {
    return std::filesystem::path(CUBEY_SOURCE_DIR);
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open source file: " + path.string());
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

std::unordered_map<std::string, int> cloud_debug_glsl_constants() {
    const std::filesystem::path path = source_root_path() / "shaders/cubey/cloud/cloud_common.glsl";
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open cloud_common.glsl");
    }

    std::unordered_map<std::string, int> constants;
    std::string line;
    constexpr std::string_view prefix = "const int CLOUD_DEBUG_";
    while (std::getline(input, line)) {
        if (line.rfind(prefix, 0) != 0) {
            continue;
        }
        const std::size_t name_start = prefix.size();
        const std::size_t name_end = line.find(' ', name_start);
        const std::size_t value_start = line.find('=', name_end);
        const std::size_t value_end = line.find(';', value_start);
        if (name_end == std::string::npos || value_start == std::string::npos ||
            value_end == std::string::npos) {
            throw std::runtime_error("malformed cloud debug constant line");
        }

        constants.emplace("CLOUD_DEBUG_" + line.substr(name_start, name_end - name_start),
                          std::stoi(line.substr(value_start + 1, value_end - value_start - 1)));
    }
    return constants;
}

std::string cloud_debug_constant_name(cubey::render::CloudLayerDebugView view) {
    std::string name = "CLOUD_DEBUG_";
    for (const char c : std::string_view{cubey::render::cloud_layer_debug_view_name(view)}) {
        if (c == '-') {
            name.push_back('_');
        } else {
            name.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
    }
    return name;
}

cubey::render::CloudLayerViewRegimeInput regime_input(float altitude_m, cubey::math::Vec3 forward) {
    constexpr float kRadius = 6371000.0F;
    return {
        .camera_position = {0.0F, kRadius + altitude_m, 0.0F},
        .camera_forward = forward,
        .planet_radius_m = kRadius,
        .orbit_transition_start_m = 45000.0F,
        .orbit_transition_end_m = 180000.0F,
    };
}

} // namespace

void test_cloud_layer_view_regime_resolves_surface_camera() {
    const cubey::render::CloudLayerViewRegime regime =
        cubey::render::cloud_layer_view_regime(regime_input(150.0F, {0.0F, 0.0F, -1.0F}));

    require_near(regime.camera_mode, 0.0F, 0.001F, "surface cloud camera should use local mode");
    require_near(regime.altitude_m, 150.0F, 0.5F, "surface cloud camera should report altitude");
    require(regime.horizon_grazing > 0.95F,
            "level surface cloud view should report grazing horizon factor");
}

void test_cloud_layer_view_regime_resolves_high_transition_camera() {
    const cubey::render::CloudLayerViewRegime regime =
        cubey::render::cloud_layer_view_regime(regime_input(90000.0F, {0.0F, -1.0F, 0.0F}));

    require_near(regime.camera_mode, 1.0F, 0.001F, "high cloud camera should use transition mode");
    require(regime.altitude_blend > 0.2F && regime.altitude_blend < 0.5F,
            "high cloud camera should be inside the transition band");
}

void test_cloud_layer_view_regime_resolves_orbit_camera() {
    const cubey::render::CloudLayerViewRegime regime =
        cubey::render::cloud_layer_view_regime(regime_input(240000.0F, {0.0F, -1.0F, 0.0F}));

    require_near(regime.camera_mode, 4.0F, 0.001F,
                 "orbit cloud camera should enable full shell mode");
    require_near(regime.altitude_blend, 1.0F, 0.001F,
                 "orbit cloud camera should be beyond the transition band");
}

void test_cloud_layer_view_regime_promotes_grazing_high_camera() {
    const cubey::render::CloudLayerViewRegime regime =
        cubey::render::cloud_layer_view_regime(regime_input(12000.0F, {1.0F, 0.0F, 0.0F}));

    require_near(regime.camera_mode, 1.0F, 0.001F,
                 "grazing cloud camera should use transition mode for far shell support");
    require(regime.horizon_grazing > 0.95F,
            "grazing cloud camera should report strong horizon factor");
}

void test_cloud_layer_edge_mask_debug_view_round_trips() {
    const cubey::render::CloudLayerDebugView view =
        cubey::render::cloud_layer_debug_view_from_name("edge-mask");

    require(view == cubey::render::CloudLayerDebugView::EdgeMask,
            "cloud debug parser should recognize edge-mask");
    require(cubey::render::cloud_layer_debug_view_name(view) == std::string_view{"edge-mask"},
            "cloud debug names should round-trip edge-mask");
}

void test_cloud_layer_debug_views_round_trip_all_names() {
    cubey::render::CloudLayerDebugView current = cubey::render::kCloudLayerDebugViews.front();
    for (const cubey::render::CloudLayerDebugView view : cubey::render::kCloudLayerDebugViews) {
        const std::string_view name = cubey::render::cloud_layer_debug_view_name(view);
        require(!name.empty(), "cloud debug view should have a name");
        require(cubey::render::cloud_layer_debug_view_from_name(name) == view,
                "cloud debug view names should round-trip");
        require(current == view, "cloud debug next cycle should follow the public debug list");
        current = cubey::render::next_cloud_layer_debug_view(current);
    }
    require(current == cubey::render::kCloudLayerDebugViews.front(),
            "cloud debug next cycle should wrap");
}

void test_cloud_layer_debug_views_match_glsl_constants() {
    const std::unordered_map<std::string, int> constants = cloud_debug_glsl_constants();
    for (const cubey::render::CloudLayerDebugView view : cubey::render::kCloudLayerDebugViews) {
        const std::string constant_name = cloud_debug_constant_name(view);
        const auto it = constants.find(constant_name);
        require(it != constants.end(), "cloud debug view should have a GLSL constant");
        require(it->second == static_cast<int>(static_cast<std::uint32_t>(view)),
                "cloud debug view enum should match GLSL constant value");
    }
}

void test_cloud_layer_runtime_shader_files_select_composite_variants() {
    const std::filesystem::path shader_dir = "/tmp/cubey-cloud-shaders";
    const cubey::render::CloudLayerRuntimeShaderFiles background =
        cubey::render::cloud_layer_runtime_shader_files(
            shader_dir, cubey::render::CloudLayerCompositeMode::ExternalBackground);
    const cubey::render::CloudLayerRuntimeShaderFiles background_depth =
        cubey::render::cloud_layer_runtime_shader_files(
            shader_dir, cubey::render::CloudLayerCompositeMode::ExternalBackgroundSceneDepth);

    require(background.composite_fragment.stage == VK_SHADER_STAGE_FRAGMENT_BIT,
            "cloud composite should use a fragment shader");
    require(background.composite_fragment.path.filename() ==
                std::filesystem::path("cloud_composite_background.frag.spv"),
            "external background clouds should use the background composite shader");
    require(background_depth.composite_fragment.path.filename() ==
                std::filesystem::path("cloud_composite_background_depth.frag.spv"),
            "scene-depth clouds should use the background-depth composite shader");
}

void test_cloud_layer_cmake_package_tracks_composite_modes() {
    const std::filesystem::path source_root = source_root_path();
    const std::string shader_cmake = read_text_file(source_root / "cmake/CubeyShaders.cmake");
    const std::string atmosphere_cmake =
        read_text_file(source_root / "projects/atmosphere/CMakeLists.txt");
    const std::string ocean_cmake = read_text_file(source_root / "projects/ocean/CMakeLists.txt");
    const std::string planet_cmake =
        read_text_file(source_root / "projects/planet/CMakeLists.txt");

    require(shader_cmake.find("CUBEY_CLOUD_COMPOSITE STREQUAL \"background\"") !=
                std::string::npos,
            "shared cloud shader package should expose the background composite mode");
    require(shader_cmake.find("CUBEY_CLOUD_COMPOSITE STREQUAL \"background-depth\"") !=
                std::string::npos,
            "shared cloud shader package should expose the background-depth composite mode");
    require(shader_cmake.find("cubey_shared_shader_depends") != std::string::npos,
            "shared cloud shader package should consume common shader dependencies");
    require(shader_cmake.find("cloud_layer_shared_shader_depends") != std::string::npos,
            "cloud dependency package should thread shared shader dependencies");
    require(!std::filesystem::exists(source_root / "shaders/cubey/cloud/cloud_composite.frag"),
            "shared cloud package should not keep an unused standalone composite shader");
    require(atmosphere_cmake.find("COMPOSITE background") != std::string::npos,
            "atmosphere should request the background cloud composite package");
    require(atmosphere_cmake.find("COMPOSITE background-depth") == std::string::npos,
            "atmosphere should not request the depth-aware cloud composite package");
    require(ocean_cmake.find("COMPOSITE background-depth") != std::string::npos,
            "ocean should request the depth-aware cloud composite package");
    require(planet_cmake.find("COMPOSITE background-depth") != std::string::npos,
            "planet should request the depth-aware cloud composite package");
    require(planet_cmake.find("list(APPEND CUBEY_PLANET_CLOUD_SHADERS") == std::string::npos,
            "planet should not manually append cloud composite shader variants");
}

void test_cloud_layer_frame_uniforms_pack_environment_lighting() {
    cubey::render::CloudLayerConfig config{};
    cubey::render::CloudLayerFrameInfo frame{};
    frame.sun_direction = {0.0F, 0.8F, 0.6F};
    frame.sun_color = {1.0F, 0.9F, 0.7F};
    frame.sun_intensity = 0.35F;
    frame.moon_direction = {0.2F, 0.7F, -0.1F};
    frame.moon_color = {0.5F, 0.6F, 0.8F};
    frame.moon_intensity = 0.12F;
    frame.ambient_color = {0.04F, 0.05F, 0.07F};
    frame.ambient_intensity = 1.4F;
    frame.target_extent = {1280U, 720U};

    const cubey::render::CloudLayerFrameUniforms uniforms =
        cubey::render::cloud_layer_frame_uniforms(config, frame);

    require_near(uniforms.sun_direction_intensity.w, 0.35F, 0.001F,
                 "cloud uniforms should pack resolved sun intensity");
    require_near(uniforms.sun_color.y, 0.9F, 0.001F,
                 "cloud uniforms should pack resolved sun color");
    require_near(uniforms.moon_direction_intensity.x, 0.2F, 0.001F,
                 "cloud uniforms should pack moon direction");
    require_near(uniforms.moon_direction_intensity.w, 0.12F, 0.001F,
                 "cloud uniforms should pack moon intensity separately from sun");
    require_near(uniforms.moon_color.z, 0.8F, 0.001F, "cloud uniforms should pack moon color");
    require_near(uniforms.ambient_color_intensity.z, 0.07F, 0.001F,
                 "cloud uniforms should pack environment ambient color");
    require_near(uniforms.ambient_color_intensity.w, 1.4F, 0.001F,
                 "cloud uniforms should pack environment ambient intensity");
}
