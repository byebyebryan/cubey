#include <cubey/render/cloud_layer.h>
#include <cubey/render/cloud_layer_config.h>
#include <cubey/render/cloud_environment_probe.h>
#include <cubey/render/cloud_planar_reflection.h>

#include <array>
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

void test_shared_cloud_ui_defaults_to_surface_controls() {
    const std::filesystem::path source_root = source_root_path();
    const std::string ui_header =
        read_text_file(source_root / "include/cubey/host/cloud_environment_ui.h");
    const std::string ui_source =
        read_text_file(source_root / "src/cubey/host/cloud_environment_ui.cpp");

    require(ui_header.find("show_aerial_orbit_controls = false") != std::string::npos,
            "shared cloud UI should hide deferred aerial/orbit controls by default");
    require(ui_source.find("Horizon handoff") != std::string::npos,
            "shared cloud UI should expose the surface horizon handoff control");
    require(ui_source.find("Cloud V1 surfaces hide the deferred aerial/orbit controls") !=
                std::string::npos,
            "shared cloud UI should label the hidden deferred-control contract");
}

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

void test_cloud_layer_scene_depth_modes_match_shader_contract() {
    cubey::render::CloudLayerConfig config{};
    cubey::render::CloudLayerFrameInfo frame{};
    const std::array modes{
        cubey::render::CloudLayerSceneDepthMode::Disabled,
        cubey::render::CloudLayerSceneDepthMode::DistanceAware,
        cubey::render::CloudLayerSceneDepthMode::OpaqueForeground,
    };
    for (std::size_t index = 0; index < modes.size(); ++index) {
        frame.scene_depth_mode = modes[index];
        const cubey::render::CloudLayerFrameUniforms uniforms =
            cubey::render::cloud_layer_frame_uniforms(config, frame);
        require_near(uniforms.scene_depth_options.z, static_cast<float>(index), 0.0001F,
                     "cloud uniforms should encode the scene-depth mode");
    }

    const std::string common =
        read_text_file(source_root_path() / "shaders/cubey/cloud/cloud_common.glsl");
    require(common.find("const int CLOUD_SCENE_DEPTH_DISABLED = 0;") != std::string::npos &&
                common.find("const int CLOUD_SCENE_DEPTH_DISTANCE_AWARE = 1;") !=
                    std::string::npos &&
                common.find("const int CLOUD_SCENE_DEPTH_OPAQUE_FOREGROUND = 2;") !=
                    std::string::npos,
            "cloud scene-depth enum values should match GLSL constants");

    frame.external_background = true;
    const cubey::render::CloudLayerFrameUniforms external_background_uniforms =
        cubey::render::cloud_layer_frame_uniforms(config, frame);
    require_near(external_background_uniforms.background_options.w, 1.0F, 0.0001F,
                 "cloud uniforms should identify external atmosphere composition");
    require(common.find("float cloud_internal_star_visibility()") != std::string::npos &&
                common.find("return 1.0 - step(0.5, params.background_options.w);") !=
                    std::string::npos &&
                common.find("cloud_internal_star_visibility();") != std::string::npos,
            "external atmosphere composition should suppress the cloud fallback starfield");
}

void test_cloud_scene_depth_composite_bypasses_opaque_foreground_before_resolve() {
    const std::string shader = read_text_file(
        source_root_path() / "shaders/cubey/cloud/cloud_composite_background_depth.frag");
    const std::size_t foreground_bypass = shader.find("if ((final_view || raw_final_view)");
    const std::size_t foreground_mode =
        shader.find("CLOUD_SCENE_DEPTH_OPAQUE_FOREGROUND", foreground_bypass);
    const std::size_t scene_color_return =
        shader.find("out_color = vec4(max(background, vec3(0.0)), 1.0);", foreground_bypass);
    const std::size_t cloud_resolve = shader.find("vec4 raw_cloud =");

    require(foreground_bypass != std::string::npos && foreground_mode != std::string::npos &&
                scene_color_return != std::string::npos && cloud_resolve != std::string::npos &&
                foreground_mode < scene_color_return && scene_color_return < cloud_resolve,
            "foreground-only cloud composition should return before cloud resolve samples");
    require(shader.find("return;", scene_color_return) < cloud_resolve,
            "foreground-only cloud composition should preserve scene color exactly");
}

void test_cloud_scene_depth_composite_bypasses_fully_occluded_distance_before_resolve() {
    const std::string shader = read_text_file(
        source_root_path() / "shaders/cubey/cloud/cloud_composite_background_depth.frag");
    const std::size_t raw_visibility = shader.find("float raw_scene_visibility =");
    const std::size_t distance_bypass = shader.find(
        "if ((final_view || raw_final_view) && raw_scene_visibility <= 0.0001)", raw_visibility);
    const std::size_t scene_color_return =
        shader.find("out_color = vec4(max(background, vec3(0.0)), 1.0);", distance_bypass);
    const std::size_t cloud_resolve = shader.find("vec4 resolved_cloud =");

    require(raw_visibility != std::string::npos && distance_bypass != std::string::npos &&
                scene_color_return != std::string::npos && cloud_resolve != std::string::npos &&
                raw_visibility < distance_bypass && scene_color_return < cloud_resolve,
            "fully occluded distance-aware clouds should return before resolve samples");
    require(shader.find("return;", scene_color_return) < cloud_resolve,
            "fully occluded distance-aware clouds should preserve scene color exactly");
}

void test_cloud_scene_depth_consumers_select_explicit_policies() {
    const std::filesystem::path root = source_root_path();
    const std::string gltf =
        read_text_file(root / "projects/gltf_viewer/gltf_viewer_scene.cpp");
    const std::string ocean = read_text_file(root / "projects/ocean/ocean_app.cpp");
    const std::string water =
        read_text_file(root / "projects/fluid/sim/water_3d/water_3d_app.cpp");
    constexpr std::string_view opaque = "CloudLayerSceneDepthMode::OpaqueForeground";

    require(gltf.find(opaque) != std::string::npos && ocean.find(opaque) != std::string::npos &&
                water.find(opaque) != std::string::npos,
            "surface background consumers should preserve opaque foreground scene color");
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

void test_cloud_environment_probe_config_rejects_invalid_values() {
    cubey::render::CloudEnvironmentProbeConfig config{};
    config.mip_levels = 8;
    bool threw = false;
    try {
        cubey::render::validate_cloud_environment_probe_config(config);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    require(threw, "cloud environment probe should reject excessive mip counts");
}

void test_cloud_environment_probe_timeline_captures_coherently() {
    cubey::render::CloudEnvironmentProbeTimeline timeline;
    timeline.configure(4.0F);
    require(timeline.capture_pending(), "new cloud environment probe should capture immediately");
    require(!timeline.valid(), "new cloud environment probe should not expose invalid cubes");

    timeline.capture_recorded();
    require(timeline.valid(), "first coherent capture should make the probe valid");
    require_near(timeline.blend(), 1.0F, 0.001F,
                 "first coherent capture should not blend from undefined data");
    require(timeline.generation() == 1U, "first capture should advance generation");

    timeline.advance(0.125);
    require(!timeline.capture_pending(), "probe should retain its cube before the interval");
    timeline.advance(0.125);
    require(timeline.capture_pending(), "probe should request a coherent update at four hertz");

    timeline.capture_recorded();
    require_near(timeline.blend(), 0.0F, 0.001F,
                 "subsequent captures should start a crossfade");
    timeline.advance(0.125);
    require_near(timeline.blend(), 0.5F, 0.001F,
                 "crossfade should track the coherent update interval");
    require(!timeline.capture_pending(), "probe should not overwrite a cube mid-crossfade");
    timeline.advance(0.125);
    require_near(timeline.blend(), 1.0F, 0.001F,
                 "crossfade should finish before the next capture");
    require(timeline.capture_pending(), "finished crossfade should permit the next capture");
}

void test_cloud_planar_reflection_config_and_extent() {
    cubey::render::CloudPlanarReflectionConfig config{};
    config.target_extent = {1280U, 720U};
    cubey::render::validate_cloud_planar_reflection_config(config);
    const VkExtent2D extent = cubey::render::cloud_planar_reflection_extent(config);
    require(extent.width == 640U && extent.height == 360U,
            "planar cloud reflection should default to half resolution");

    config.mip_levels = 20U;
    bool threw = false;
    try {
        cubey::render::validate_cloud_planar_reflection_config(config);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    require(threw, "planar cloud reflection should reject excessive mip counts");
}

void test_cloud_planar_reflected_frame_mirrors_camera_and_expands_fov() {
    cubey::render::CloudLayerFrameInfo frame{};
    frame.camera_position = {4.0F, 12.0F, -3.0F};
    frame.camera_right = {1.0F, 0.0F, 0.0F};
    frame.camera_up = {0.0F, 0.8660254F, -0.5F};
    frame.camera_forward = {0.0F, -0.5F, -0.8660254F};
    frame.tan_half_fovy = 0.5F;
    frame.temporal_frame_index = 11U;
    frame.scene_depth_mode = cubey::render::CloudLayerSceneDepthMode::OpaqueForeground;

    const cubey::render::CloudLayerFrameInfo reflected =
        cubey::render::cloud_planar_reflected_frame(
            frame, {0.0F, 2.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {640U, 360U}, 0.15F);

    require_near(reflected.camera_position.y, -8.0F, 0.001F,
                 "planar cloud camera should mirror around the receiver plane");
    require_near(reflected.camera_forward.y, 0.5F, 0.001F,
                 "planar cloud forward should mirror vertically");
    require_near(reflected.camera_up.y, -0.8660254F, 0.001F,
                 "planar cloud up should mirror vertically");
    require_near(reflected.camera_right.x, 1.0F, 0.001F,
                 "planar cloud right should preserve the horizontal axis");
    require_near(reflected.tan_half_fovy, 0.575F, 0.001F,
                 "planar cloud view should include its guard band");
    require(reflected.target_extent.width == 640U && reflected.target_extent.height == 360U,
            "planar cloud frame should use the product extent");
    require(reflected.temporal_frame_index == 0U &&
                reflected.scene_depth_mode == cubey::render::CloudLayerSceneDepthMode::Disabled,
            "planar cloud frame should be coherent and independent of scene depth");
}

void test_cloud_planar_filter_declares_stable_mip_contract() {
    const cubey::render::MaterialPassInfo pass =
        cubey::render::cloud_planar_reflection_filter_pass_info();
    require(pass.label == "cloud.planar.filter",
            "planar cloud filter should keep its profiling label");
    require(pass.descriptor_sets.size() == 1U &&
                pass.descriptor_sets[0].bindings.size() == 1U,
            "planar cloud filter should consume one radiance-transmittance source");
    require(pass.push_constants.size() == 1U,
            "planar cloud filter should control the base copy and mip filter radius");

    const std::string shader = read_text_file(
        source_root_path() / "shaders/cubey/cloud/cloud_planar_filter.frag");
    require(shader.find("value * (1.0 / 16.0)") != std::string::npos,
            "planar cloud filter should use a normalized stable spatial kernel");
}

void test_cloud_environment_prefilter_declares_cloud_contract() {
    const cubey::render::MaterialPassInfo pass =
        cubey::render::cloud_environment_prefilter_pass_info();
    require(pass.label == "cloud.environment.prefilter",
            "cloud environment prefilter should keep its profiling label");
    require(pass.descriptor_sets.size() == 1U &&
                pass.descriptor_sets[0].bindings.size() == 3U,
            "cloud environment prefilter should bind uniforms, clear sky, and cloud product");

    const std::string shader = read_text_file(
        source_root_path() /
        "shaders/cubey/cloud/cloud_environment_prefilter.frag");
    require(shader.find("cloud.rgb + clamp(cloud.a") != std::string::npos,
            "cloud environment prefilter should compose radiance and transmittance");
    require(shader.find("reflection_prefilter.glsl") != std::string::npos,
            "cloud environment prefilter should reuse shared GGX sampling helpers");
}
