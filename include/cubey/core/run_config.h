#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <limits>
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

inline constexpr float kRunConfigUnsetFloat = std::numeric_limits<float>::quiet_NaN();

[[nodiscard]] inline bool run_config_float_is_set(float value) {
    return !std::isnan(value);
}

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
    float smoke_injector_force = kRunConfigUnsetFloat;
    float smoke_injector_propulsion = kRunConfigUnsetFloat;
    float smoke_injector_orbit_radius = kRunConfigUnsetFloat;
    float smoke_injector_orbit_radius_spread = kRunConfigUnsetFloat;
    float smoke_injector_orbit_angular_speed = kRunConfigUnsetFloat;
    float smoke_injector_orbit_angular_speed_spread = kRunConfigUnsetFloat;
    float smoke_injector_orbit_phase_spread = kRunConfigUnsetFloat;
    std::uint32_t pyro_sources = 0;
    float pyro_source_radius = kRunConfigUnsetFloat;
    float pyro_source_force = kRunConfigUnsetFloat;
    float pyro_soot = kRunConfigUnsetFloat;
    float pyro_temperature = kRunConfigUnsetFloat;
    float pyro_fuel = kRunConfigUnsetFloat;
    float pyro_buoyancy = kRunConfigUnsetFloat;
    float pyro_ignition_temperature = kRunConfigUnsetFloat;
    float pyro_burn_rate = kRunConfigUnsetFloat;
    float pyro_heat_output = kRunConfigUnsetFloat;
    float pyro_soot_yield = kRunConfigUnsetFloat;
    float pyro_expansion = kRunConfigUnsetFloat;
    float pyro_flame_cooling = kRunConfigUnsetFloat;
    float pyro_shredding = kRunConfigUnsetFloat;
    float pyro_turbulence = kRunConfigUnsetFloat;
    float pyro_obstacle_height = kRunConfigUnsetFloat;
    float pyro_obstacle_radius = kRunConfigUnsetFloat;
    float explosion_interval_seconds = kRunConfigUnsetFloat;
    float explosion_duration_seconds = kRunConfigUnsetFloat;
    float explosion_boost = kRunConfigUnsetFloat;
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
