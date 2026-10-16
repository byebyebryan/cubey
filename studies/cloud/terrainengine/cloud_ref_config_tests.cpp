#include "cloud_ref_project_config.h"


#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Fn> void require_throws(Fn&& fn, std::string_view message) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

void require_near(float actual, float expected, float epsilon, std::string_view message) {
    if (std::abs(actual - expected) > epsilon) {
        throw std::runtime_error(std::string(message));
    }
}

void require_near(const std::optional<float>& actual, float expected, float epsilon,
                  std::string_view message) {
    require(actual.has_value(), message);
    require_near(*actual, expected, epsilon, message);
}

void test_names_and_next_debug_view() {
    require(cubey::projects::cloud_ref::clouds_camera_mode_from_string("surface") ==
                cubey::projects::cloud_ref::CloudsCameraMode::Surface,
            "surface camera mode should parse");
    require(cubey::projects::cloud_ref::clouds_camera_mode_from_string("surface-up") ==
                cubey::projects::cloud_ref::CloudsCameraMode::SurfaceUp,
            "surface-up camera mode should parse");
    require(cubey::projects::cloud_ref::clouds_camera_mode_from_string("surface-sun") ==
                cubey::projects::cloud_ref::CloudsCameraMode::SurfaceSun,
            "surface-sun camera mode should parse");
    require(cubey::projects::cloud_ref::clouds_camera_mode_from_string("high-oblique") ==
                cubey::projects::cloud_ref::CloudsCameraMode::HighOblique,
            "high-oblique camera mode should parse");
    require(cubey::projects::cloud_ref::clouds_camera_mode_from_string("orbit-terminator") ==
                cubey::projects::cloud_ref::CloudsCameraMode::OrbitTerminator,
            "orbit-terminator camera mode should parse");
    require(cubey::projects::cloud_ref::clouds_quality_from_string("full") ==
                cubey::projects::cloud_ref::CloudsQuality::Full,
            "full quality should parse");
    require(cubey::projects::cloud_ref::clouds_resolve_mode_from_string("") ==
                cubey::projects::cloud_ref::CloudsResolveMode::TerrainPost,
            "empty cloud resolve mode should use terrain post");
    require(cubey::projects::cloud_ref::clouds_resolve_mode_from_string("gaussian") ==
                cubey::projects::cloud_ref::CloudsResolveMode::TerrainPost,
            "gaussian alias should use terrain post resolve");
    require(cubey::projects::cloud_ref::clouds_resolve_mode_from_string("metadata-bilateral") ==
                cubey::projects::cloud_ref::CloudsResolveMode::MetadataBilateral,
            "metadata-bilateral resolve mode should parse");
    require(cubey::projects::cloud_ref::clouds_resolve_mode_name(
                cubey::projects::cloud_ref::CloudsResolveMode::MetadataBilateral) ==
                std::string_view("metadata-bilateral"),
            "cloud resolve mode should report canonical spelling");
    require(cubey::projects::cloud_ref::clouds_weather_preset_from_string("storm-cells") ==
                cubey::projects::cloud_ref::CloudsWeatherPreset::StormCells,
            "storm-cells weather preset should parse");
    require(cubey::projects::cloud_ref::clouds_weather_preset_from_string("storm") ==
                cubey::projects::cloud_ref::CloudsWeatherPreset::StormCells,
            "legacy storm weather preset should parse");
    require(cubey::projects::cloud_ref::clouds_weather_preset_name(
                cubey::projects::cloud_ref::CloudsWeatherPreset::BrokenCumulus) ==
                std::string_view("broken-cumulus"),
            "weather preset name should use canonical spelling");
    require(cubey::projects::cloud_ref::next_clouds_debug_view(
                cubey::projects::cloud_ref::CloudsDebugView::Final) ==
                cubey::projects::cloud_ref::CloudsDebugView::RawFinal,
            "cloud debug view should expose raw final after final");
    require(cubey::projects::cloud_ref::next_clouds_debug_view(
                cubey::projects::cloud_ref::CloudsDebugView::Lighting) ==
                cubey::projects::cloud_ref::CloudsDebugView::AmbientLight,
            "cloud debug view should include ambient-light diagnostics");
    require(cubey::projects::cloud_ref::next_clouds_debug_view(
                cubey::projects::cloud_ref::CloudsDebugView::AmbientLight) ==
                cubey::projects::cloud_ref::CloudsDebugView::DirectLight,
            "cloud debug view should include direct-light diagnostics");
    require(cubey::projects::cloud_ref::next_clouds_debug_view(
                cubey::projects::cloud_ref::CloudsDebugView::DirectLight) ==
                cubey::projects::cloud_ref::CloudsDebugView::PhaseLight,
            "cloud debug view should include phase-light diagnostics");
    require(cubey::projects::cloud_ref::next_clouds_debug_view(
                cubey::projects::cloud_ref::CloudsDebugView::Shadow) ==
                cubey::projects::cloud_ref::CloudsDebugView::Steps,
            "cloud debug view should advance after shadow");
    require(cubey::projects::cloud_ref::next_clouds_debug_view(
                cubey::projects::cloud_ref::CloudsDebugView::Background) ==
                cubey::projects::cloud_ref::CloudsDebugView::CloudAlpha,
            "cloud debug view should include cloud alpha");
    require(cubey::projects::cloud_ref::next_clouds_debug_view(
                cubey::projects::cloud_ref::CloudsDebugView::Distance) ==
                cubey::projects::cloud_ref::CloudsDebugView::BaseDensity,
            "cloud debug view should include base-density diagnostics");
    require(cubey::projects::cloud_ref::next_clouds_debug_view(
                cubey::projects::cloud_ref::CloudsDebugView::DetailDensity) ==
                cubey::projects::cloud_ref::CloudsDebugView::ViewOpticalDepth,
            "cloud debug view should include view optical depth");
    require(cubey::projects::cloud_ref::next_clouds_debug_view(
                cubey::projects::cloud_ref::CloudsDebugView::LightOpticalDepth) ==
                cubey::projects::cloud_ref::CloudsDebugView::Final,
            "cloud debug view should wrap");
    require(cubey::projects::cloud_ref::clouds_debug_view_from_string("cloud-alpha") ==
                cubey::projects::cloud_ref::CloudsDebugView::CloudAlpha,
            "cloud alpha debug view should parse");
    require(cubey::projects::cloud_ref::clouds_debug_view_from_string("raw-final") ==
                cubey::projects::cloud_ref::CloudsDebugView::RawFinal,
            "raw final debug view should parse");
    require(cubey::projects::cloud_ref::clouds_debug_view_from_string("ambient-light") ==
                cubey::projects::cloud_ref::CloudsDebugView::AmbientLight,
            "cloud ambient light debug view should parse");
    require(cubey::projects::cloud_ref::clouds_debug_view_from_string("direct-light") ==
                cubey::projects::cloud_ref::CloudsDebugView::DirectLight,
            "cloud direct light debug view should parse");
    require(cubey::projects::cloud_ref::clouds_debug_view_from_string("phase-light") ==
                cubey::projects::cloud_ref::CloudsDebugView::PhaseLight,
            "cloud phase light debug view should parse");
    require(cubey::projects::cloud_ref::clouds_debug_view_from_string("distance") ==
                cubey::projects::cloud_ref::CloudsDebugView::Distance,
            "cloud distance debug view should parse");
    require(cubey::projects::cloud_ref::clouds_debug_view_from_string("base-density") ==
                cubey::projects::cloud_ref::CloudsDebugView::BaseDensity,
            "cloud base-density debug view should parse");
    require(cubey::projects::cloud_ref::clouds_debug_view_from_string("detail-density") ==
                cubey::projects::cloud_ref::CloudsDebugView::DetailDensity,
            "cloud detail-density debug view should parse");
    require(cubey::projects::cloud_ref::clouds_debug_view_from_string("view-optical-depth") ==
                cubey::projects::cloud_ref::CloudsDebugView::ViewOpticalDepth,
            "cloud view optical depth debug view should parse");
    require(cubey::projects::cloud_ref::clouds_debug_view_from_string("light-optical-depth") ==
                cubey::projects::cloud_ref::CloudsDebugView::LightOpticalDepth,
            "cloud light optical depth debug view should parse");
}

void test_run_config_mapping() {
    cubey::projects::cloud_ref::CloudsStartupOptions run_config{};
    run_config.debug_view = "density";
    run_config.clouds.camera_mode = "orbit";
    run_config.clouds.quality = "quarter";
    run_config.clouds.view_steps = 96;
    run_config.clouds.view_samples = 4;
    run_config.clouds.weather_preset = "storm";
    run_config.clouds.planet_radius_m = 1000000.0F;
    run_config.clouds.bottom_altitude_m = 2000.0F;
    run_config.clouds.top_altitude_m = 9000.0F;
    run_config.clouds.coverage = 0.75F;
    run_config.clouds.density = 0.025F;
    run_config.clouds.weather_scale_km = 240.0F;
    run_config.clouds.wind_speed_mps = 35.0F;
    run_config.clouds.shadow_strength = 0.8F;
    run_config.clouds.horizon_strength = 0.9F;
    run_config.clouds.weather_fronts = 0.25F;
    run_config.clouds.weather_cells = 0.5F;
    run_config.clouds.weather_streaks = 0.75F;
    run_config.clouds.detail_erosion = 0.35F;
    run_config.clouds.ambient_strength = 0.7F;
    run_config.clouds.direct_strength = 1.4F;
    run_config.clouds.phase_strength = 0.9F;
    run_config.clouds.powder_strength = 0.35F;
    run_config.clouds.final_contrast = 1.2F;
    run_config.clouds.final_saturation = 1.1F;
    run_config.clouds.horizon_glow_strength = 0.6F;
    run_config.clouds.sun_glare_strength = 0.8F;
    run_config.clouds.resolve_mode = "metadata-bilateral";
    run_config.clouds.resolve_strength = 0.65F;
    run_config.clouds.resolve_radius_px = 4.0F;
    run_config.clouds.temporal = 0;
    run_config.clouds.local_volume = 0;
    run_config.clouds.horizon_layer = 1;
    run_config.atmosphere.time_of_day_mode = "solar";
    run_config.atmosphere.time_hours = 6.5F;
    run_config.atmosphere.time_speed_hours_per_second = 0.25F;
    run_config.atmosphere.time_paused = 1;

    const cubey::projects::cloud_ref::CloudsConfig config =
        cubey::projects::cloud_ref::clouds_config_from_options(run_config);
    require(config.camera_mode == cubey::projects::cloud_ref::CloudsCameraMode::Orbit,
            "cloud camera mode should map from run config");
    require(config.quality == cubey::projects::cloud_ref::CloudsQuality::Quarter,
            "cloud quality should map from run config");
    require(config.view_steps_override == 96, "cloud view steps should map from run config");
    require(config.view_samples == 4, "cloud view samples should map from run config");
    require(config.weather_preset == cubey::projects::cloud_ref::CloudsWeatherPreset::StormCells,
            "cloud weather preset should map from run config");
    require(config.cloud_style == cubey::projects::cloud_ref::CloudsCloudStyle::StormCells,
            "cloud weather preset should select cloud style");
    require(config.debug_view == cubey::projects::cloud_ref::CloudsDebugView::Density,
            "cloud debug view should map from common debug config");
    require_near(config.camera_altitude_m,
                 cubey::projects::cloud_ref::clouds_default_camera_altitude_m(
                     cubey::projects::cloud_ref::CloudsCameraMode::Orbit),
                 0.001F, "cloud camera altitude should follow camera mode default");
    require_near(config.coverage, 0.75F, 0.001F, "cloud coverage should map");
    require_near(config.density, 0.025F, 0.001F, "cloud density should map");
    require_near(config.shadow_strength, 0.8F, 0.001F,
                 "cloud shadow strength should map");
    require_near(config.horizon_strength, 0.9F, 0.001F,
                 "cloud horizon strength should map");
    require_near(config.weather_fronts, 0.25F, 0.001F,
                 "cloud weather fronts should map");
    require_near(config.weather_cells, 0.5F, 0.001F,
                 "cloud weather cells should map");
    require_near(config.weather_streaks, 0.75F, 0.001F,
                 "cloud weather streaks should map");
    require_near(config.detail_erosion, 0.35F, 0.001F,
                 "cloud detail erosion should map");
    require_near(config.ambient_strength, 0.7F, 0.001F,
                 "cloud ambient strength should map");
    require_near(config.direct_strength, 1.4F, 0.001F,
                 "cloud direct strength should map");
    require_near(config.phase_strength, 0.9F, 0.001F,
                 "cloud phase strength should map");
    require_near(config.powder_strength, 0.35F, 0.001F,
                 "cloud powder strength should map");
    require_near(config.final_contrast, 1.2F, 0.001F,
                 "cloud final contrast should map");
    require_near(config.final_saturation, 1.1F, 0.001F,
                 "cloud final saturation should map");
    require_near(config.horizon_glow_strength, 0.6F, 0.001F,
                 "cloud horizon glow strength should map");
    require_near(config.sun_glare_strength, 0.8F, 0.001F,
                 "cloud sun glare strength should map");
    require(config.resolve_mode ==
                cubey::projects::cloud_ref::CloudsResolveMode::MetadataBilateral,
            "cloud resolve mode should map from run config");
    require(config.post_blur_enabled, "cloud resolve strength should keep post blur enabled");
    require_near(config.post_blur_strength, 0.65F, 0.001F,
                 "cloud resolve strength should map to post blur strength");
    require_near(config.post_blur_radius_px, 4.0F, 0.001F,
                 "cloud resolve radius should map to post blur radius");
    require(!config.temporal_enabled, "cloud temporal option should map");
    require(!config.local_volume_enabled, "cloud local volume option should map");
    require(config.horizon_layer_enabled, "cloud horizon layer option should map");
    require_near(config.time.time_hours, 6.5F, 0.001F, "cloud solar time should map");
    require(!config.time.playing, "cloud pause flag should map");
}

void test_camera_preset_defaults() {
    cubey::projects::cloud_ref::CloudsStartupOptions run_config{};
    run_config.clouds.camera_mode = "orbit-terminator";

    cubey::projects::cloud_ref::CloudsConfig config =
        cubey::projects::cloud_ref::clouds_config_from_options(run_config);
    require(config.camera_mode == cubey::projects::cloud_ref::CloudsCameraMode::OrbitTerminator,
            "orbit terminator preset should map from run config");
    require_near(config.camera_altitude_m,
                 cubey::projects::cloud_ref::clouds_default_camera_altitude_m(
                     cubey::projects::cloud_ref::CloudsCameraMode::OrbitTerminator),
                 0.001F, "orbit terminator should use orbit altitude");
    require_near(config.time.time_hours, 6.0F, 0.001F,
                 "orbit terminator should default to dawn framing");

    run_config.atmosphere.time_hours = 12.5F;
    config = cubey::projects::cloud_ref::clouds_config_from_options(run_config);
    require_near(config.time.time_hours, 12.5F, 0.001F,
                 "explicit time should override orbit terminator preset time");
}

void test_weather_preset_defaults() {
    cubey::projects::cloud_ref::CloudsStartupOptions run_config{};
    run_config.clouds.weather_preset = "overcast";

    cubey::projects::cloud_ref::CloudsConfig config =
        cubey::projects::cloud_ref::clouds_config_from_options(run_config);
    require(config.weather_preset ==
                cubey::projects::cloud_ref::CloudsWeatherPreset::OvercastStratus,
            "overcast weather preset should map from run config");
    require(config.cloud_style == cubey::projects::cloud_ref::CloudsCloudStyle::OvercastStratus,
            "overcast weather preset should select stratus style");
    require_near(config.coverage, 0.72F, 0.001F,
                 "overcast weather preset should set coverage");
    require_near(config.weather_scale_km, 280.0F, 0.001F,
                 "overcast weather preset should set scale");
    require_near(config.bottom_altitude_m, 3000.0F, 0.001F,
                 "overcast weather preset should set bottom altitude");
    require_near(config.top_altitude_m, 12000.0F, 0.001F,
                 "overcast weather preset should set top altitude");

    run_config.clouds.coverage = 0.35F;
    config = cubey::projects::cloud_ref::clouds_config_from_options(run_config);
    require(config.weather_preset ==
                cubey::projects::cloud_ref::CloudsWeatherPreset::OvercastStratus,
            "explicit coverage should preserve selected weather preset");
    require_near(config.coverage, 0.35F, 0.001F,
                 "explicit coverage should override weather preset coverage");

    run_config = {};
    run_config.clouds.weather_preset = "high-cirrus";
    config = cubey::projects::cloud_ref::clouds_config_from_options(run_config);
    require(config.weather_preset == cubey::projects::cloud_ref::CloudsWeatherPreset::HighCirrus,
            "high cirrus weather preset should map from run config");
    require(config.cloud_style == cubey::projects::cloud_ref::CloudsCloudStyle::HighCirrus,
            "high cirrus weather preset should select cloud style");
    require_near(config.bottom_altitude_m, 11000.0F, 0.001F,
                 "high cirrus weather preset should set bottom altitude");
}

void test_config_schema() {
    const char* argv[] = {
        "cloud_ref",
        "--cloud-camera-mode", "high",
        "--cloud-quality", "full",
        "--cloud-view-steps", "96",
        "--cloud-view-samples", "2",
        "--cloud-weather-preset", "storm",
        "--cloud-coverage", "0.44",
        "--cloud-wind-speed-mps", "22",
        "--cloud-shadow-strength", "0.7",
        "--cloud-horizon-strength", "0.8",
        "--cloud-weather-fronts", "0.2",
        "--cloud-weather-cells", "0.4",
        "--cloud-weather-streaks", "0.6",
        "--cloud-detail-erosion", "0.5",
        "--cloud-ambient-strength", "0.7",
        "--cloud-direct-strength", "1.4",
        "--cloud-phase-strength", "0.9",
        "--cloud-powder-strength", "0.35",
        "--cloud-final-contrast", "1.2",
        "--cloud-final-saturation", "1.1",
        "--cloud-horizon-glow-strength", "0.6",
        "--cloud-sun-glare-strength", "0.8",
        "--cloud-resolve-mode", "metadata-bilateral",
        "--cloud-resolve-strength", "0.25",
        "--cloud-resolve-radius-px", "3.5",
        "--no-cloud-temporal",
        "--no-cloud-local-volume",
        "--cloud-horizon-layer",
    };
    const auto config = cubey::projects::cloud_ref::parse_cloud_ref_project_config(
        static_cast<int>(std::size(argv)), const_cast<char**>(argv));
    require(config.options.clouds.camera_mode == "high",
            "cloud camera mode schema should set");
    require(config.options.clouds.quality == "full", "cloud quality schema should set");
    require(config.options.clouds.view_steps == 96, "cloud view steps schema should set");
    require(config.options.clouds.view_samples == 2, "cloud view samples schema should set");
    require(config.options.clouds.weather_preset == "storm",
            "cloud weather preset schema should preserve aliases");
    require_near(config.options.clouds.coverage, 0.44F, 0.001F,
                 "cloud coverage schema should set");
    require_near(config.options.clouds.wind_speed_mps, 22.0F, 0.001F,
                 "cloud wind schema should set");
    require_near(config.options.clouds.shadow_strength, 0.7F, 0.001F,
                 "cloud shadow schema should set");
    require_near(config.options.clouds.horizon_strength, 0.8F, 0.001F,
                 "cloud horizon schema should set");
    require_near(config.options.clouds.weather_fronts, 0.2F, 0.001F,
                 "cloud weather fronts schema should set");
    require_near(config.options.clouds.weather_cells, 0.4F, 0.001F,
                 "cloud weather cells schema should set");
    require_near(config.options.clouds.weather_streaks, 0.6F, 0.001F,
                 "cloud weather streaks schema should set");
    require_near(config.options.clouds.detail_erosion, 0.5F, 0.001F,
                 "cloud detail erosion schema should set");
    require_near(config.options.clouds.ambient_strength, 0.7F, 0.001F,
                 "cloud ambient schema should set");
    require_near(config.options.clouds.direct_strength, 1.4F, 0.001F,
                 "cloud direct schema should set");
    require_near(config.options.clouds.phase_strength, 0.9F, 0.001F,
                 "cloud phase schema should set");
    require_near(config.options.clouds.powder_strength, 0.35F, 0.001F,
                 "cloud powder schema should set");
    require_near(config.options.clouds.final_contrast, 1.2F, 0.001F,
                 "cloud contrast schema should set");
    require_near(config.options.clouds.final_saturation, 1.1F, 0.001F,
                 "cloud saturation schema should set");
    require_near(config.options.clouds.horizon_glow_strength, 0.6F, 0.001F,
                 "cloud horizon glow schema should set");
    require_near(config.options.clouds.sun_glare_strength, 0.8F, 0.001F,
                 "cloud glare schema should set");
    require(config.options.clouds.resolve_mode == "metadata-bilateral",
            "cloud resolve mode schema should set");
    require_near(config.options.clouds.resolve_strength, 0.25F, 0.001F,
                 "cloud resolve strength schema should set");
    require_near(config.options.clouds.resolve_radius_px, 3.5F, 0.001F,
                 "cloud resolve radius schema should set");
    require(config.options.clouds.temporal == 0, "cloud temporal schema should set");
    require(config.options.clouds.local_volume == 0, "cloud local volume schema should set");
    require(config.options.clouds.horizon_layer == 1, "cloud horizon schema should set");

    cubey::projects::cloud_ref::CloudRefProjectConfig json_config;
    auto schema = cubey::projects::cloud_ref::cloud_ref_project_config_schema(json_config);
    schema.apply_json({{"clouds", {{"coverage", 0.7}}},
                       {"atmosphere", {{"time_hours", 12.5}}}});
    require_near(json_config.options.clouds.coverage, 0.7F, 0.001F,
                 "cloud JSON coverage should bind");
    require_near(json_config.options.atmosphere.time_hours, 12.5F, 0.001F,
                 "cloud JSON atmosphere time should bind");
    const auto template_path =
        std::filesystem::temp_directory_path() / "cubey-cloud-ref-schema-template-v2.json";
    schema.write_template(template_path);
    require(std::filesystem::exists(template_path), "cloud template should be written");
    std::filesystem::remove(template_path);
    bool rejected = false;
    try {
        schema.apply_json({{"smoke", {{"injectors", 1}}}});
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "cloud schema should reject unrelated options");
}

void test_shared_schema_validation_and_scope() {
    cubey::projects::cloud_ref::CloudRefProjectConfig config;
    const auto schema = cubey::projects::cloud_ref::cloud_ref_project_config_schema(config);
    require_throws([&] { schema.set("clouds.view_samples", "5"); },
                   "cloud_ref shared range should reject five view samples");
    const char* invalid_samples[] = {"cloud_ref", "--cloud-view-samples", "3"};
    require_throws(
        [&] {
            static_cast<void>(cubey::projects::cloud_ref::parse_cloud_ref_project_config(
                3, const_cast<char**>(invalid_samples)));
        },
        "cloud_ref parser should reject the unsupported three-sample cloud mode");
    require_throws([&] { schema.set("clouds.resolve_radius_px", "9"); },
                   "cloud_ref resolve radius should preserve its upper bound");
    require_throws([&] { schema.set("clouds.weather_preset", "surface-volume"); },
                   "cloud_ref should reject an unconsumed weather preset");
    require(schema.find("clouds.view_sample_mode") == nullptr &&
                schema.find_by_cli_name("--cloud-view-sample-mode") == nullptr,
            "cloud_ref schema should omit unconsumed sampling controls");

    cubey::projects::cloud_ref::CloudRefProjectConfig defaults;
    const auto default_schema = cubey::projects::cloud_ref::cloud_ref_project_config_schema(defaults);
    const auto default_document = default_schema.template_json();
    require(default_document.at("clouds").at("view_steps").is_null() &&
                default_document.at("clouds").at("view_samples").is_null() &&
                default_document.at("clouds").at("coverage").is_null() &&
                default_document.at("clouds").at("temporal").is_null(),
            "cloud_ref template should preserve absent optional cloud values as null");
}

} // namespace

int main() {
    try {
        test_names_and_next_debug_view();
        test_run_config_mapping();
        test_camera_preset_defaults();
        test_weather_preset_defaults();
        test_config_schema();
        test_shared_schema_validation_and_scope();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
