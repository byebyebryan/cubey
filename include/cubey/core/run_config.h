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
    std::uint32_t shadow_grid_width = 0;
    std::uint32_t shadow_grid_height = 0;
    std::uint32_t shadow_grid_depth = 0;
    std::uint32_t shadow_steps = 0;
    std::uint32_t shadow_update_interval = 0;
    std::uint32_t smoke_injectors = 0;
    float smoke_injector_force = 6.0F;
    float smoke_injector_propulsion = 1.0F;
    float smoke_injector_orbit_radius = 0.25F;
    float smoke_injector_orbit_radius_spread = 0.22F;
    float smoke_injector_orbit_angular_speed = 0.0F;
    float smoke_injector_orbit_angular_speed_spread = 0.8F;
    float smoke_injector_orbit_phase_spread = 1.0F;
    std::uint32_t pyro_sources = 0;
    float pyro_source_radius = 0.05F;
    float pyro_source_force = 6.0F;
    float pyro_soot = 6.0F;
    float pyro_temperature = 1.4F;
    float pyro_fuel = 2.0F;
    float pyro_buoyancy = 1.0F;
    float pyro_ignition_temperature = 0.22F;
    float pyro_burn_rate = 3.4F;
    float pyro_heat_output = 2.6F;
    float pyro_soot_yield = 0.16F;
    float pyro_expansion = 1.35F;
    float pyro_flame_cooling = 3.0F;
    float pyro_shredding = 2.4F;
    float pyro_turbulence = 0.55F;
    float pyro_obstacle_height = 0.48F;
    float pyro_obstacle_radius = 0.15F;
    float explosion_interval_seconds = 3.0F;
    float explosion_duration_seconds = 0.12F;
    float explosion_boost = 18.0F;
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
    bool smoke_obstacles = false;
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
