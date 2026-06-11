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
    require(cubey::projects::clouds::clouds_quality_from_string("full") ==
                cubey::projects::clouds::CloudsQuality::Full,
            "full quality should parse");
    require(cubey::projects::clouds::next_clouds_debug_view(
                cubey::projects::clouds::CloudsDebugView::Shadow) ==
                cubey::projects::clouds::CloudsDebugView::Steps,
            "cloud debug view should advance");
    require(cubey::projects::clouds::next_clouds_debug_view(
                cubey::projects::clouds::CloudsDebugView::Steps) ==
                cubey::projects::clouds::CloudsDebugView::Final,
            "cloud debug view should wrap");
}

void test_run_config_mapping() {
    cubey::RunConfig run_config{};
    run_config.debug_view = "density";
    run_config.clouds.camera_mode = "orbit";
    run_config.clouds.quality = "quarter";
    run_config.clouds.planet_radius_m = 1000000.0F;
    run_config.clouds.bottom_altitude_m = 2000.0F;
    run_config.clouds.top_altitude_m = 9000.0F;
    run_config.clouds.coverage = 0.75F;
    run_config.clouds.density = 1.25F;
    run_config.clouds.weather_scale_km = 240.0F;
    run_config.clouds.wind_speed_mps = 35.0F;
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
    require(config.debug_view == cubey::projects::clouds::CloudsDebugView::Density,
            "cloud debug view should map from common debug config");
    require_near(config.camera_altitude_m,
                 cubey::projects::clouds::clouds_default_camera_altitude_m(
                     cubey::projects::clouds::CloudsCameraMode::Orbit),
                 0.001F, "cloud camera altitude should follow camera mode default");
    require_near(config.coverage, 0.75F, 0.001F, "cloud coverage should map");
    require_near(config.time.time_hours, 6.5F, 0.001F, "cloud solar time should map");
    require(!config.time.playing, "cloud pause flag should map");
}

void test_config_descriptors() {
    cubey::RunConfig config{};
    cubey::set_run_config_option_from_string(config, "clouds.camera_mode", "high");
    cubey::set_run_config_option_from_string(config, "clouds.quality", "full");
    cubey::set_run_config_option_from_string(config, "clouds.coverage", "0.44");
    cubey::set_run_config_option_from_string(config, "clouds.wind_speed_mps", "22");
    require(config.clouds.camera_mode == "high", "cloud camera mode descriptor should set");
    require(config.clouds.quality == "full", "cloud quality descriptor should set");
    require_near(config.clouds.coverage, 0.44F, 0.001F,
                 "cloud coverage descriptor should set");
    require_near(config.clouds.wind_speed_mps, 22.0F, 0.001F,
                 "cloud wind descriptor should set");
}

} // namespace

int main() {
    try {
        test_names_and_next_debug_view();
        test_run_config_mapping();
        test_config_descriptors();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
