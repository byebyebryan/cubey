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
    struct GridOptions {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t depth = 0;
    };

    struct SmokeOptions {
        std::string pressure_solver{};
        std::uint32_t injectors = 0;
        std::uint32_t pressure_iterations = 0;
        float dye_decay = kRunConfigUnsetFloat;
        float velocity_decay = kRunConfigUnsetFloat;
        float injector_radius = kRunConfigUnsetFloat;
        float injector_force = kRunConfigUnsetFloat;
        float injector_propulsion = kRunConfigUnsetFloat;
        float injector_orbit_radius = kRunConfigUnsetFloat;
        float injector_orbit_radius_spread = kRunConfigUnsetFloat;
        float injector_orbit_angular_speed = kRunConfigUnsetFloat;
        float injector_orbit_angular_speed_spread = kRunConfigUnsetFloat;
        float injector_orbit_phase_spread = kRunConfigUnsetFloat;
        float vorticity = kRunConfigUnsetFloat;
    };

    struct PyroOptions {
        GridOptions shadow_grid{};
        std::uint32_t shadow_steps = 0;
        std::uint32_t shadow_update_interval = 0;
        std::uint32_t sources = 0;
        float source_height = kRunConfigUnsetFloat;
        float source_radius = kRunConfigUnsetFloat;
        float source_force = kRunConfigUnsetFloat;
        float soot = kRunConfigUnsetFloat;
        float temperature = kRunConfigUnsetFloat;
        float fuel = kRunConfigUnsetFloat;
        float buoyancy = kRunConfigUnsetFloat;
        float ignition_temperature = kRunConfigUnsetFloat;
        float burn_rate = kRunConfigUnsetFloat;
        float heat_output = kRunConfigUnsetFloat;
        float soot_yield = kRunConfigUnsetFloat;
        float expansion = kRunConfigUnsetFloat;
        float flame_cooling = kRunConfigUnsetFloat;
        float shredding = kRunConfigUnsetFloat;
        float turbulence = kRunConfigUnsetFloat;
        float obstacle_height = kRunConfigUnsetFloat;
        float obstacle_radius = kRunConfigUnsetFloat;
        float explosion_interval_seconds = kRunConfigUnsetFloat;
        float explosion_duration_seconds = kRunConfigUnsetFloat;
        float explosion_boost = kRunConfigUnsetFloat;
    };

    struct Water2DOptions {
        std::string transfer_mode{};
        std::uint32_t transfer_limit = 0;
        int hose = -1;
        int drain = -1;
        int wave = -1;
    };

    struct Water3DOptions {
        std::string transfer_mode{};
        std::uint32_t transfer_limit = 0;
        std::string p2g_mode{};
        int hose = -1;
        int drain = -1;
        int rain = -1;
        int wave = -1;
        int whitewater = -1;
    };

    struct PbrOptions {
        std::filesystem::path environment_path{};
        float ibl_intensity = 1.0F;
        float environment_rotation_degrees = 0.0F;
        float exposure = 0.0F;
        bool exposure_explicit = false;
    };

    struct GltfOptions {
        std::filesystem::path input_path{};
        std::uint32_t animation_index = 0;
        float animation_speed = 1.0F;
        bool animation_paused = false;
    };

    struct AtmosphereOptions {
        std::string preset{};
        std::string time_of_day_mode{};
        std::string night_sky_mode{};
        std::string milky_way_layer{};
        float sun_elevation_degrees = kRunConfigUnsetFloat;
        float sun_azimuth_degrees = kRunConfigUnsetFloat;
        float camera_altitude_km = kRunConfigUnsetFloat;
        float mie_scale = kRunConfigUnsetFloat;
        float time_hours = kRunConfigUnsetFloat;
        float day_of_year = kRunConfigUnsetFloat;
        float latitude_degrees = kRunConfigUnsetFloat;
        float sun_azimuth_offset_degrees = kRunConfigUnsetFloat;
        float time_speed_hours_per_second = kRunConfigUnsetFloat;
        float exposure_bias = kRunConfigUnsetFloat;
        float twilight_strength = kRunConfigUnsetFloat;
        float twilight_horizon_warmth = kRunConfigUnsetFloat;
        float star_intensity = kRunConfigUnsetFloat;
        float star_density = kRunConfigUnsetFloat;
        float milky_way_intensity = kRunConfigUnsetFloat;
        float milky_way_contrast = kRunConfigUnsetFloat;
        float light_pollution = kRunConfigUnsetFloat;
        float milky_way_variation = kRunConfigUnsetFloat;
        float moon_intensity = kRunConfigUnsetFloat;
        float moonlight_intensity = kRunConfigUnsetFloat;
        float moon_phase_offset_days = kRunConfigUnsetFloat;
        float moon_size_scale = kRunConfigUnsetFloat;
        int time_paused = -1;
        int auto_exposure = -1;
        int moon = -1;
    };

    std::string title = "cubey";
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    GridOptions grid{};
    SmokeOptions smoke{};
    PyroOptions pyro{};
    Water2DOptions water2d{};
    Water3DOptions water3d{};
    PbrOptions pbr{};
    GltfOptions gltf{};
    AtmosphereOptions atmosphere{};
    std::uint32_t frames = 0;
    std::uint32_t fps = 60;
    std::filesystem::path output_path = "cubey-output.png";
    std::filesystem::path profile_output_prefix{};
    std::string debug_view{};
    CaptureMode capture_mode = CaptureMode::Png;
    std::uint32_t profile_warmup_frames = 0;
    std::uint32_t profile_diagnostic_interval = 1;
    bool headless = false;
    bool print_frame_stats = false;
    bool profile_diagnostics = false;
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
