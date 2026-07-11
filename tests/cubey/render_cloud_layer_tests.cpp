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

float transform_point(const cubey::math::Vec4& row, const cubey::math::Vec3& point) {
    return row.x * point.x + row.y * point.y + row.z * point.z + row.w;
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
    require(background.shadow.stage == VK_SHADER_STAGE_COMPUTE_BIT,
            "cloud shadows should use a compute shader");
    require(background.shadow.path.filename() == std::filesystem::path("cloud_shadow.comp.spv"),
            "cloud runtime should publish the projected shadow shader");
}

void test_cloud_layer_shadow_projection_is_snapped_and_centered() {
    const cubey::render::CloudLayerShadowRequest request{
        .receiver_center = {123.1F, 4.0F, -56.9F},
        .receiver_axis_u = {2.0F, 0.0F, 0.0F},
        .receiver_axis_v = {0.2F, 0.0F, 3.0F},
        .half_extent_m = 1024.0F,
    };
    const cubey::render::CloudLayerShadowProjection projection =
        cubey::render::cloud_layer_shadow_projection(request);

    require(projection.extent.width == cubey::render::kCloudLayerShadowTextureSize &&
                projection.extent.height == cubey::render::kCloudLayerShadowTextureSize,
            "cloud shadow projection should use the fixed product extent");
    require_near(projection.texel_world_size_m, 8.0F, 0.001F,
                 "cloud shadow projection should report world texel size");
    require_near(projection.receiver_center.x, 120.0F, 0.001F,
                 "cloud shadow center should snap along receiver U");
    require_near(projection.receiver_center.z, -56.0F, 0.001F,
                 "cloud shadow center should snap along receiver V");
    require_near(transform_point(projection.world_to_uv_x, projection.receiver_center), 0.5F,
                 0.0001F, "snapped cloud shadow center should map to U center");
    require_near(transform_point(projection.world_to_uv_y, projection.receiver_center), 0.5F,
                 0.0001F, "snapped cloud shadow center should map to V center");
    require_near(transform_point(projection.world_to_uv_x,
                                 projection.receiver_center +
                                     projection.receiver_axis_u * request.half_extent_m),
                 1.0F, 0.0001F, "cloud shadow positive U edge should map to one");
    require_near(transform_point(projection.world_to_uv_y,
                                 projection.receiver_center +
                                     projection.receiver_axis_v * request.half_extent_m),
                 1.0F, 0.0001F, "cloud shadow positive V edge should map to one");
}

void test_cloud_layer_runtime_separates_product_and_composite_descriptors() {
    const std::filesystem::path source_root = source_root_path();
    const std::string header =
        read_text_file(source_root / "include/cubey/render/cloud_layer.h");
    const std::string source =
        read_text_file(source_root / "src/cubey/render/cloud_layer.cpp");

    require(header.find("update_product_descriptors") != std::string::npos,
            "cloud runtime should expose product-only descriptor updates");
    require(header.find("update_composite_descriptors") != std::string::npos,
            "cloud runtime should expose visible-composite descriptor updates");
    require(source.find("update_product_descriptors(device, frame_slot, graph, resources, frame)") !=
                std::string::npos,
            "combined cloud descriptor updates should preserve product setup");
    require(source.find("update_composite_descriptors(device, frame_slot, graph, resources, frame") !=
                std::string::npos,
            "combined cloud descriptor updates should preserve composite setup");
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
    require(shader_cmake.find("cloud_shadow.comp") != std::string::npos,
            "shared cloud package should compile the projected shadow shader");
    require(shader_cmake.find("cloud_surface_density.glsl") != std::string::npos,
            "shared cloud package should track the surface density include");
    const std::string surface_march =
        read_text_file(source_root / "shaders/cubey/cloud/surface_cloud_march.comp");
    const std::string shadow_march =
        read_text_file(source_root / "shaders/cubey/cloud/cloud_shadow.comp");
    require(surface_march.find("#include \"cubey/cloud/cloud_surface_density.glsl\"") !=
                std::string::npos,
            "surface cloud march should consume the shared density field");
    require(shadow_march.find("#include \"cubey/cloud/cloud_surface_density.glsl\"") !=
                std::string::npos,
            "cloud shadow pass should consume the shared density field");
    require(shadow_march.find("cloud_sample_density_terrain_ref(") != std::string::npos &&
                shadow_march.find("true, 0.0") != std::string::npos,
            "cloud shadow pass should evaluate the detailed visible-cloud density field");
    require(shadow_march.find("optical_depth +=") != std::string::npos &&
                shadow_march.find("exp(-optical_depth)") != std::string::npos,
            "cloud shadow pass should integrate Beer optical depth along the light ray");
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
