#pragma once

#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>

namespace cubey {

struct CliAppInfo {
    const char* app_name = "cubey";
    const char* default_title = "cubey";
};

enum class CaptureMode {
    Png,
    Video,
};

struct RunConfig {
    std::string title = "cubey";
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    std::uint32_t grid_width = 0;
    std::uint32_t grid_height = 0;
    std::uint32_t grid_depth = 0;
    std::uint32_t injectors = 0;
    float injector_force = 6.0F;
    float injector_propulsion = 1.0F;
    float injector_orbit_radius = 0.25F;
    float injector_orbit_radius_spread = 0.22F;
    float injector_orbit_angular_speed = 0.0F;
    float injector_orbit_angular_speed_spread = 0.8F;
    float injector_orbit_phase_spread = 1.0F;
    std::uint32_t frames = 0;
    std::uint32_t fps = 60;
    std::filesystem::path input_path{};
    std::filesystem::path environment_path{};
    std::filesystem::path output_path = "cubey-output.png";
    std::string debug_view{};
    CaptureMode capture_mode = CaptureMode::Png;
    float ibl_intensity = 1.0F;
    float environment_rotation_degrees = 0.0F;
    float exposure = 0.0F;
    std::uint32_t animation_index = 0;
    float animation_speed = 1.0F;
    bool headless = false;
    bool animation_paused = false;
    bool obstacles = false;
    bool print_frame_stats = false;
    bool validation = true;
    bool require_validation = false;
};

RunConfig parse_run_config(int argc, char** argv);

template <typename RunFn> int run_cli_app(int argc, char** argv, CliAppInfo info, RunFn&& run) {
    try {
        RunConfig config = parse_run_config(argc, argv);
        if (config.title == "cubey") {
            config.title = info.default_title;
        }
        return std::forward<RunFn>(run)(config);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s: %s\n", info.app_name, error.what());
        return 1;
    }
}

} // namespace cubey
