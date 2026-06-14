#include "cloud_ref_config.h"

#include <cubey/core/config_options.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void require_near(float actual, float expected, float epsilon, std::string_view message) {
    if (std::abs(actual - expected) > epsilon) {
        throw std::runtime_error(std::string(message));
    }
}

void test_names_and_next_debug_view() {
    require(cubey::projects::cloud_ref::clouds_camera_mode_from_string("surface") ==
                cubey::projects::cloud_ref::CloudsCameraMode::Surface,
            "surface camera mode should parse");
    require(cubey::projects::cloud_ref::clouds_camera_mode_from_string("surface-up") ==
                cubey::projects::cloud_ref::CloudsCameraMode::SurfaceUp,
            "surface-up camera mode should parse");
    require(cubey::projects::cloud_ref::clouds_camera_mode_from_string("high-oblique") ==
                cubey::projects::cloud_ref::CloudsCameraMode::HighOblique,
            "high-oblique camera mode should parse");
    require(cubey::projects::cloud_ref::clouds_camera_mode_from_string("orbit-terminator") ==
                cubey::projects::cloud_ref::CloudsCameraMode::OrbitTerminator,
            "orbit-terminator camera mode should parse");
    require(cubey::projects::cloud_ref::clouds_quality_from_string("full") ==
                cubey::projects::cloud_ref::CloudsQuality::Full,
            "full quality should parse");
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
                cubey::projects::cloud_ref::CloudsDebugView::Shadow) ==
                cubey::projects::cloud_ref::CloudsDebugView::Steps,
            "cloud debug view should advance");
    require(cubey::projects::cloud_ref::next_clouds_debug_view(
                cubey::projects::cloud_ref::CloudsDebugView::GroundHit) ==
                cubey::projects::cloud_ref::CloudsDebugView::CloudAlpha,
            "cloud composition debug view should advance");
    require(cubey::projects::cloud_ref::next_clouds_debug_view(
                cubey::projects::cloud_ref::CloudsDebugView::Shell) ==
                cubey::projects::cloud_ref::CloudsDebugView::SurfaceShadow,
            "cloud debug view should include surface shadow");
    require(cubey::projects::cloud_ref::next_clouds_debug_view(
                cubey::projects::cloud_ref::CloudsDebugView::SurfaceShadow) ==
                cubey::projects::cloud_ref::CloudsDebugView::Domain,
            "cloud debug view should include domain diagnostics");
    require(cubey::projects::cloud_ref::next_clouds_debug_view(
                cubey::projects::cloud_ref::CloudsDebugView::Domain) ==
                cubey::projects::cloud_ref::CloudsDebugView::Distance,
            "cloud debug view should include distance diagnostics");
    require(cubey::projects::cloud_ref::next_clouds_debug_view(
                cubey::projects::cloud_ref::CloudsDebugView::Distance) ==
                cubey::projects::cloud_ref::CloudsDebugView::BaseDensity,
            "cloud debug view should include base-density diagnostics");
    require(cubey::projects::cloud_ref::next_clouds_debug_view(
                cubey::projects::cloud_ref::CloudsDebugView::FarHorizon) ==
                cubey::projects::cloud_ref::CloudsDebugView::CloudDepth,
            "cloud debug view should include cloud-depth diagnostics");
    require(cubey::projects::cloud_ref::next_clouds_debug_view(
                cubey::projects::cloud_ref::CloudsDebugView::HorizonBlend) ==
                cubey::projects::cloud_ref::CloudsDebugView::LocalVolume,
            "cloud debug view should include local-volume diagnostics");
    require(cubey::projects::cloud_ref::next_clouds_debug_view(
                cubey::projects::cloud_ref::CloudsDebugView::WeatherComponents) ==
                cubey::projects::cloud_ref::CloudsDebugView::Final,
            "cloud debug view should wrap");
    require(cubey::projects::cloud_ref::clouds_debug_view_from_string("cloud-alpha") ==
                cubey::projects::cloud_ref::CloudsDebugView::CloudAlpha,
            "cloud alpha debug view should parse");
    require(cubey::projects::cloud_ref::clouds_debug_view_from_string("surface-shadow") ==
                cubey::projects::cloud_ref::CloudsDebugView::SurfaceShadow,
            "cloud surface shadow debug view should parse");
    require(cubey::projects::cloud_ref::clouds_debug_view_from_string("domain") ==
                cubey::projects::cloud_ref::CloudsDebugView::Domain,
            "cloud domain debug view should parse");
    require(cubey::projects::cloud_ref::clouds_debug_view_from_string("distance") ==
                cubey::projects::cloud_ref::CloudsDebugView::Distance,
            "cloud distance debug view should parse");
    require(cubey::projects::cloud_ref::clouds_debug_view_from_string("base-density") ==
                cubey::projects::cloud_ref::CloudsDebugView::BaseDensity,
            "cloud base-density debug view should parse");
    require(cubey::projects::cloud_ref::clouds_debug_view_from_string("detail-density") ==
                cubey::projects::cloud_ref::CloudsDebugView::DetailDensity,
            "cloud detail-density debug view should parse");
    require(cubey::projects::cloud_ref::clouds_debug_view_from_string("density-lod") ==
                cubey::projects::cloud_ref::CloudsDebugView::DensityLod,
            "cloud density-lod debug view should parse");
    require(cubey::projects::cloud_ref::clouds_debug_view_from_string("step-length") ==
                cubey::projects::cloud_ref::CloudsDebugView::StepLength,
            "cloud step-length debug view should parse");
    require(cubey::projects::cloud_ref::clouds_debug_view_from_string("local-march") ==
                cubey::projects::cloud_ref::CloudsDebugView::LocalMarch,
            "cloud local-march debug view should parse");
    require(cubey::projects::cloud_ref::clouds_debug_view_from_string("far-horizon") ==
                cubey::projects::cloud_ref::CloudsDebugView::FarHorizon,
            "cloud far-horizon debug view should parse");
    require(cubey::projects::cloud_ref::clouds_debug_view_from_string("cloud-depth") ==
                cubey::projects::cloud_ref::CloudsDebugView::CloudDepth,
            "cloud depth debug view should parse");
    require(cubey::projects::cloud_ref::clouds_debug_view_from_string("cloud-confidence") ==
                cubey::projects::cloud_ref::CloudsDebugView::CloudConfidence,
            "cloud confidence debug view should parse");
    require(cubey::projects::cloud_ref::clouds_debug_view_from_string("horizon-blend") ==
                cubey::projects::cloud_ref::CloudsDebugView::HorizonBlend,
            "cloud horizon blend debug view should parse");
    require(cubey::projects::cloud_ref::clouds_debug_view_from_string("local-volume") ==
                cubey::projects::cloud_ref::CloudsDebugView::LocalVolume,
            "cloud local volume debug view should parse");
    require(cubey::projects::cloud_ref::clouds_debug_view_from_string("horizon-layer") ==
                cubey::projects::cloud_ref::CloudsDebugView::HorizonLayer,
            "cloud horizon layer debug view should parse");
    require(cubey::projects::cloud_ref::clouds_debug_view_from_string("weather-components") ==
                cubey::projects::cloud_ref::CloudsDebugView::WeatherComponents,
            "cloud weather components debug view should parse");
}

void test_run_config_mapping() {
    cubey::RunConfig run_config{};
    run_config.debug_view = "density";
    run_config.clouds.camera_mode = "orbit";
    run_config.clouds.quality = "quarter";
    run_config.clouds.weather_preset = "storm";
    run_config.clouds.planet_radius_m = 1000000.0F;
    run_config.clouds.bottom_altitude_m = 2000.0F;
    run_config.clouds.top_altitude_m = 9000.0F;
    run_config.clouds.coverage = 0.75F;
    run_config.clouds.density = 1.25F;
    run_config.clouds.weather_scale_km = 240.0F;
    run_config.clouds.wind_speed_mps = 35.0F;
    run_config.clouds.shadow_strength = 0.8F;
    run_config.clouds.horizon_strength = 0.9F;
    run_config.clouds.weather_fronts = 0.25F;
    run_config.clouds.weather_cells = 0.5F;
    run_config.clouds.weather_streaks = 0.75F;
    run_config.clouds.detail_erosion = 0.35F;
    run_config.clouds.temporal = 0;
    run_config.clouds.local_volume = 0;
    run_config.clouds.horizon_layer = 1;
    run_config.atmosphere.time_of_day_mode = "solar";
    run_config.atmosphere.time_hours = 6.5F;
    run_config.atmosphere.time_speed_hours_per_second = 0.25F;
    run_config.atmosphere.time_paused = 1;

    const cubey::projects::cloud_ref::CloudsConfig config =
        cubey::projects::cloud_ref::clouds_config_from_run_config(run_config);
    require(config.camera_mode == cubey::projects::cloud_ref::CloudsCameraMode::Orbit,
            "cloud camera mode should map from run config");
    require(config.quality == cubey::projects::cloud_ref::CloudsQuality::Quarter,
            "cloud quality should map from run config");
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
    require(!config.temporal_enabled, "cloud temporal option should map");
    require(!config.local_volume_enabled, "cloud local volume option should map");
    require(config.horizon_layer_enabled, "cloud horizon layer option should map");
    require_near(config.time.time_hours, 6.5F, 0.001F, "cloud solar time should map");
    require(!config.time.playing, "cloud pause flag should map");
}

void test_camera_preset_defaults() {
    cubey::RunConfig run_config{};
    run_config.clouds.camera_mode = "orbit-terminator";

    cubey::projects::cloud_ref::CloudsConfig config =
        cubey::projects::cloud_ref::clouds_config_from_run_config(run_config);
    require(config.camera_mode == cubey::projects::cloud_ref::CloudsCameraMode::OrbitTerminator,
            "orbit terminator preset should map from run config");
    require_near(config.camera_altitude_m,
                 cubey::projects::cloud_ref::clouds_default_camera_altitude_m(
                     cubey::projects::cloud_ref::CloudsCameraMode::OrbitTerminator),
                 0.001F, "orbit terminator should use orbit altitude");
    require_near(config.time.time_hours, 6.0F, 0.001F,
                 "orbit terminator should default to dawn framing");

    run_config.atmosphere.time_hours = 12.5F;
    config = cubey::projects::cloud_ref::clouds_config_from_run_config(run_config);
    require_near(config.time.time_hours, 12.5F, 0.001F,
                 "explicit time should override orbit terminator preset time");
}

void test_weather_preset_defaults() {
    cubey::RunConfig run_config{};
    run_config.clouds.weather_preset = "overcast";

    cubey::projects::cloud_ref::CloudsConfig config =
        cubey::projects::cloud_ref::clouds_config_from_run_config(run_config);
    require(config.weather_preset ==
                cubey::projects::cloud_ref::CloudsWeatherPreset::OvercastStratus,
            "overcast weather preset should map from run config");
    require(config.cloud_style == cubey::projects::cloud_ref::CloudsCloudStyle::OvercastStratus,
            "overcast weather preset should select stratus style");
    require_near(config.coverage, 0.88F, 0.001F,
                 "overcast weather preset should set coverage");
    require_near(config.weather_scale_km, 280.0F, 0.001F,
                 "overcast weather preset should set scale");
    require_near(config.bottom_altitude_m, 1200.0F, 0.001F,
                 "overcast weather preset should set bottom altitude");
    require_near(config.top_altitude_m, 5200.0F, 0.001F,
                 "overcast weather preset should set top altitude");

    run_config.clouds.coverage = 0.35F;
    config = cubey::projects::cloud_ref::clouds_config_from_run_config(run_config);
    require(config.weather_preset ==
                cubey::projects::cloud_ref::CloudsWeatherPreset::OvercastStratus,
            "explicit coverage should preserve selected weather preset");
    require_near(config.coverage, 0.35F, 0.001F,
                 "explicit coverage should override weather preset coverage");

    run_config = {};
    run_config.clouds.weather_preset = "high-cirrus";
    config = cubey::projects::cloud_ref::clouds_config_from_run_config(run_config);
    require(config.weather_preset == cubey::projects::cloud_ref::CloudsWeatherPreset::HighCirrus,
            "high cirrus weather preset should map from run config");
    require(config.cloud_style == cubey::projects::cloud_ref::CloudsCloudStyle::HighCirrus,
            "high cirrus weather preset should select cloud style");
    require_near(config.bottom_altitude_m, 7200.0F, 0.001F,
                 "high cirrus weather preset should set bottom altitude");
}

void test_config_descriptors() {
    cubey::RunConfig config{};
    cubey::set_run_config_option_from_string(config, "clouds.camera_mode", "high");
    cubey::set_run_config_option_from_string(config, "clouds.quality", "full");
    cubey::set_run_config_option_from_string(config, "clouds.weather_preset", "storm");
    cubey::set_run_config_option_from_string(config, "clouds.coverage", "0.44");
    cubey::set_run_config_option_from_string(config, "clouds.wind_speed_mps", "22");
    cubey::set_run_config_option_from_string(config, "clouds.shadow_strength", "0.7");
    cubey::set_run_config_option_from_string(config, "clouds.horizon_strength", "0.8");
    cubey::set_run_config_option_from_string(config, "clouds.weather_fronts", "0.2");
    cubey::set_run_config_option_from_string(config, "clouds.weather_cells", "0.4");
    cubey::set_run_config_option_from_string(config, "clouds.weather_streaks", "0.6");
    cubey::set_run_config_option_from_string(config, "clouds.detail_erosion", "0.5");
    cubey::set_run_config_option_from_string(config, "clouds.temporal", "false");
    cubey::set_run_config_option_from_string(config, "clouds.local_volume", "false");
    cubey::set_run_config_option_from_string(config, "clouds.horizon_layer", "true");
    require(config.clouds.camera_mode == "high", "cloud camera mode descriptor should set");
    require(config.clouds.quality == "full", "cloud quality descriptor should set");
    require(config.clouds.weather_preset == "storm",
            "cloud weather preset descriptor should set");
    require_near(config.clouds.coverage, 0.44F, 0.001F,
                 "cloud coverage descriptor should set");
    require_near(config.clouds.wind_speed_mps, 22.0F, 0.001F,
                 "cloud wind descriptor should set");
    require_near(config.clouds.shadow_strength, 0.7F, 0.001F,
                 "cloud shadow descriptor should set");
    require_near(config.clouds.horizon_strength, 0.8F, 0.001F,
                 "cloud horizon descriptor should set");
    require_near(config.clouds.weather_fronts, 0.2F, 0.001F,
                 "cloud weather fronts descriptor should set");
    require_near(config.clouds.weather_cells, 0.4F, 0.001F,
                 "cloud weather cells descriptor should set");
    require_near(config.clouds.weather_streaks, 0.6F, 0.001F,
                 "cloud weather streaks descriptor should set");
    require_near(config.clouds.detail_erosion, 0.5F, 0.001F,
                 "cloud detail erosion descriptor should set");
    require(config.clouds.temporal == 0, "cloud temporal descriptor should set");
    require(config.clouds.local_volume == 0, "cloud local volume descriptor should set");
    require(config.clouds.horizon_layer == 1, "cloud horizon layer descriptor should set");
}

} // namespace

int main() {
    try {
        test_names_and_next_debug_view();
        test_run_config_mapping();
        test_camera_preset_defaults();
        test_weather_preset_defaults();
        test_config_descriptors();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
