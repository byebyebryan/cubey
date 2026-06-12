#include "clouds_config.h"

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
    require(cubey::projects::clouds::clouds_camera_mode_from_string("surface") ==
                cubey::projects::clouds::CloudsCameraMode::Surface,
            "surface camera mode should parse");
    require(cubey::projects::clouds::clouds_camera_mode_from_string("surface-up") ==
                cubey::projects::clouds::CloudsCameraMode::SurfaceUp,
            "surface-up camera mode should parse");
    require(cubey::projects::clouds::clouds_camera_mode_from_string("high-oblique") ==
                cubey::projects::clouds::CloudsCameraMode::HighOblique,
            "high-oblique camera mode should parse");
    require(cubey::projects::clouds::clouds_camera_mode_from_string("orbit-terminator") ==
                cubey::projects::clouds::CloudsCameraMode::OrbitTerminator,
            "orbit-terminator camera mode should parse");
    require(cubey::projects::clouds::clouds_quality_from_string("full") ==
                cubey::projects::clouds::CloudsQuality::Full,
            "full quality should parse");
    require(cubey::projects::clouds::clouds_weather_preset_from_string("storm") ==
                cubey::projects::clouds::CloudsWeatherPreset::Storm,
            "storm weather preset should parse");
    require(cubey::projects::clouds::next_clouds_debug_view(
                cubey::projects::clouds::CloudsDebugView::Shadow) ==
                cubey::projects::clouds::CloudsDebugView::Steps,
            "cloud debug view should advance");
    require(cubey::projects::clouds::next_clouds_debug_view(
                cubey::projects::clouds::CloudsDebugView::GroundHit) ==
                cubey::projects::clouds::CloudsDebugView::CloudAlpha,
            "cloud composition debug view should advance");
    require(cubey::projects::clouds::next_clouds_debug_view(
                cubey::projects::clouds::CloudsDebugView::Shell) ==
                cubey::projects::clouds::CloudsDebugView::SurfaceShadow,
            "cloud debug view should include surface shadow");
    require(cubey::projects::clouds::next_clouds_debug_view(
                cubey::projects::clouds::CloudsDebugView::SurfaceShadow) ==
                cubey::projects::clouds::CloudsDebugView::Final,
            "cloud debug view should wrap");
    require(cubey::projects::clouds::clouds_debug_view_from_string("cloud-alpha") ==
                cubey::projects::clouds::CloudsDebugView::CloudAlpha,
            "cloud alpha debug view should parse");
    require(cubey::projects::clouds::clouds_debug_view_from_string("surface-shadow") ==
                cubey::projects::clouds::CloudsDebugView::SurfaceShadow,
            "cloud surface shadow debug view should parse");
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
    run_config.atmosphere.time_of_day_mode = "solar";
    run_config.atmosphere.time_hours = 6.5F;
    run_config.atmosphere.time_speed_hours_per_second = 0.25F;
    run_config.atmosphere.time_paused = 1;

    const cubey::projects::clouds::CloudsConfig config =
        cubey::projects::clouds::clouds_config_from_run_config(run_config);
    require(config.camera_mode == cubey::projects::clouds::CloudsCameraMode::Orbit,
            "cloud camera mode should map from run config");
    require(config.quality == cubey::projects::clouds::CloudsQuality::Quarter,
            "cloud quality should map from run config");
    require(config.weather_preset == cubey::projects::clouds::CloudsWeatherPreset::Storm,
            "cloud weather preset should map from run config");
    require(config.debug_view == cubey::projects::clouds::CloudsDebugView::Density,
            "cloud debug view should map from common debug config");
    require_near(config.camera_altitude_m,
                 cubey::projects::clouds::clouds_default_camera_altitude_m(
                     cubey::projects::clouds::CloudsCameraMode::Orbit),
                 0.001F, "cloud camera altitude should follow camera mode default");
    require_near(config.coverage, 0.75F, 0.001F, "cloud coverage should map");
    require_near(config.shadow_strength, 0.8F, 0.001F,
                 "cloud shadow strength should map");
    require_near(config.time.time_hours, 6.5F, 0.001F, "cloud solar time should map");
    require(!config.time.playing, "cloud pause flag should map");
}

void test_camera_preset_defaults() {
    cubey::RunConfig run_config{};
    run_config.clouds.camera_mode = "orbit-terminator";

    cubey::projects::clouds::CloudsConfig config =
        cubey::projects::clouds::clouds_config_from_run_config(run_config);
    require(config.camera_mode == cubey::projects::clouds::CloudsCameraMode::OrbitTerminator,
            "orbit terminator preset should map from run config");
    require_near(config.camera_altitude_m,
                 cubey::projects::clouds::clouds_default_camera_altitude_m(
                     cubey::projects::clouds::CloudsCameraMode::OrbitTerminator),
                 0.001F, "orbit terminator should use orbit altitude");
    require_near(config.time.time_hours, 6.0F, 0.001F,
                 "orbit terminator should default to dawn framing");

    run_config.atmosphere.time_hours = 12.5F;
    config = cubey::projects::clouds::clouds_config_from_run_config(run_config);
    require_near(config.time.time_hours, 12.5F, 0.001F,
                 "explicit time should override orbit terminator preset time");
}

void test_weather_preset_defaults() {
    cubey::RunConfig run_config{};
    run_config.clouds.weather_preset = "overcast";

    cubey::projects::clouds::CloudsConfig config =
        cubey::projects::clouds::clouds_config_from_run_config(run_config);
    require(config.weather_preset == cubey::projects::clouds::CloudsWeatherPreset::Overcast,
            "overcast weather preset should map from run config");
    require_near(config.coverage, 0.86F, 0.001F,
                 "overcast weather preset should set coverage");
    require_near(config.weather_scale_km, 240.0F, 0.001F,
                 "overcast weather preset should set scale");

    run_config.clouds.coverage = 0.35F;
    config = cubey::projects::clouds::clouds_config_from_run_config(run_config);
    require(config.weather_preset == cubey::projects::clouds::CloudsWeatherPreset::Overcast,
            "explicit coverage should preserve selected weather preset");
    require_near(config.coverage, 0.35F, 0.001F,
                 "explicit coverage should override weather preset coverage");
}

void test_config_descriptors() {
    cubey::RunConfig config{};
    cubey::set_run_config_option_from_string(config, "clouds.camera_mode", "high");
    cubey::set_run_config_option_from_string(config, "clouds.quality", "full");
    cubey::set_run_config_option_from_string(config, "clouds.weather_preset", "storm");
    cubey::set_run_config_option_from_string(config, "clouds.coverage", "0.44");
    cubey::set_run_config_option_from_string(config, "clouds.wind_speed_mps", "22");
    cubey::set_run_config_option_from_string(config, "clouds.shadow_strength", "0.7");
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
