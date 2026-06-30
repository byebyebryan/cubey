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

    struct OceanOptions {
        std::uint32_t map_size = 0;
        std::string field_precision{};
        std::string surface_mode{};
        std::string camera_preset{};
        int cascade = -1;
        int spectral_domains = -1;
        int terrain_fields = -1;
        float planet_radius_scale = kRunConfigUnsetFloat;
        float curvature_start_ratio = kRunConfigUnsetFloat;
        float curvature_end_ratio = kRunConfigUnsetFloat;
        float curvature_strength = kRunConfigUnsetFloat;
        float wire_opacity = kRunConfigUnsetFloat;
        bool wire_overlay = false;
    };

    struct PlanetOptions {
        std::string scale_preset{};
        float radius_m = kRunConfigUnsetFloat;
        float atmosphere_height_m = kRunConfigUnsetFloat;
        float camera_altitude_m = kRunConfigUnsetFloat;
        float camera_orbit_spin_degrees_per_second = kRunConfigUnsetFloat;
        float camera_surface_pitch_degrees = kRunConfigUnsetFloat;
        float camera_surface_yaw_degrees = kRunConfigUnsetFloat;
        std::string camera_surface_look{};
        std::uint32_t patches_per_face = 0;
        std::uint32_t patch_resolution = 0;
        std::uint32_t max_lod_level = 0;
        bool max_lod_level_set = false;
        float lod_target_edge_px = kRunConfigUnsetFloat;
        float lod_hysteresis = kRunConfigUnsetFloat;
        std::uint32_t local_detail_lod_levels = 0;
        std::uint32_t local_detail_cells_per_axis = 0;
        float local_detail_outer_half_extent_m = kRunConfigUnsetFloat;
        int local_detail_enabled = -1;
        float local_detail_height_strength_m = kRunConfigUnsetFloat;
        float local_detail_scale_m = kRunConfigUnsetFloat;
        int wire_overlay = -1;
        int skirts_enabled = -1;
        float skirt_depth_scale = kRunConfigUnsetFloat;
        int terrain_enabled = -1;
        float terrain_height_scale_m = kRunConfigUnsetFloat;
        float terrain_noise_scale = kRunConfigUnsetFloat;
        float terrain_mid_detail_strength = kRunConfigUnsetFloat;
        float terrain_fine_detail_strength = kRunConfigUnsetFloat;
        float terrain_fine_detail_scale = kRunConfigUnsetFloat;
        std::uint32_t terrain_seed = 0;
        float sea_level_m = kRunConfigUnsetFloat;
        float bathymetry_depth_scale_m = kRunConfigUnsetFloat;
        float shoreline_width_m = kRunConfigUnsetFloat;
        float atmosphere_haze_strength = kRunConfigUnsetFloat;
        float atmosphere_haze_start = kRunConfigUnsetFloat;
        float atmosphere_haze_end = kRunConfigUnsetFloat;
        float atmosphere_aerial_strength = kRunConfigUnsetFloat;
        float day_of_year = kRunConfigUnsetFloat;
        float time_hours = kRunConfigUnsetFloat;
        float time_speed_hours_per_second = kRunConfigUnsetFloat;
        int time_paused = -1;
        std::string camera_mode{};
        std::string atmosphere_mode{};
        bool terrain_seed_set = false;
    };

    struct PbrOptions {
        std::filesystem::path environment_path{};
        std::string environment_source{};
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

    struct TerrainOptions {
        std::uint64_t seed = 0;
        float cell_size = kRunConfigUnsetFloat;
        float sea_level = kRunConfigUnsetFloat;
        float land_extent = kRunConfigUnsetFloat;
        float coast_noise = kRunConfigUnsetFloat;
        float relief = kRunConfigUnsetFloat;
        float ridges = kRunConfigUnsetFloat;
        float valleys = kRunConfigUnsetFloat;
        float vertical_scale = kRunConfigUnsetFloat;
        std::string recipe{};
        std::string camera_preset{};
        int water_surface = -1;
        bool seed_set = false;
    };

    struct TerrainLabOptions {
        std::string slice_preset{};
        std::string camera_preset{};
        std::string noise_source{};
    };

    struct AtmosphereOptions {
        std::string preset{};
        std::string time_of_day_mode{};
        std::string night_sky_mode{};
        std::string milky_way_layer{};
        float sun_elevation_degrees = kRunConfigUnsetFloat;
        float sun_azimuth_degrees = kRunConfigUnsetFloat;
        float camera_altitude_km = kRunConfigUnsetFloat;
        float rayleigh_scale = kRunConfigUnsetFloat;
        float mie_scale = kRunConfigUnsetFloat;
        float ozone_scale = kRunConfigUnsetFloat;
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
        int reference_geometry = -1;
    };

    struct CloudOptions {
        std::string camera_mode{};
        std::string quality{};
        std::string weather_preset{};
        std::string cache_frames{};
        std::uint32_t cache_texture_size = 0;
        std::string render_path{};
        std::string sampling_mode{};
        std::string background_mode{};
        std::string distance_mode{};
        std::string orbit_representation{};
        float planet_radius_m = kRunConfigUnsetFloat;
        float camera_altitude_m = kRunConfigUnsetFloat;
        float bottom_altitude_m = kRunConfigUnsetFloat;
        float top_altitude_m = kRunConfigUnsetFloat;
        float coverage = kRunConfigUnsetFloat;
        float density = kRunConfigUnsetFloat;
        float weather_scale_km = kRunConfigUnsetFloat;
        float vertical_shear_fraction = kRunConfigUnsetFloat;
        float wind_speed_mps = kRunConfigUnsetFloat;
        float shadow_strength = kRunConfigUnsetFloat;
        float horizon_strength = kRunConfigUnsetFloat;
        float weather_fronts = kRunConfigUnsetFloat;
        float weather_cells = kRunConfigUnsetFloat;
        float weather_streaks = kRunConfigUnsetFloat;
        float weather_softness = kRunConfigUnsetFloat;
        float weather_influence = kRunConfigUnsetFloat;
        float detail_erosion = kRunConfigUnsetFloat;
        float ambient_strength = kRunConfigUnsetFloat;
        float direct_strength = kRunConfigUnsetFloat;
        float phase_strength = kRunConfigUnsetFloat;
        float final_contrast = kRunConfigUnsetFloat;
        float final_saturation = kRunConfigUnsetFloat;
        float resolve_strength = kRunConfigUnsetFloat;
        float horizon_glow_strength = kRunConfigUnsetFloat;
        float sun_glare_strength = kRunConfigUnsetFloat;
        float jitter_strength = kRunConfigUnsetFloat;
        float orbit_transition_start_m = kRunConfigUnsetFloat;
        float orbit_transition_end_m = kRunConfigUnsetFloat;
        float far_shell_start_m = kRunConfigUnsetFloat;
        float far_shell_end_m = kRunConfigUnsetFloat;
        float far_shell_strength = kRunConfigUnsetFloat;
        float orbit_detail_strength = kRunConfigUnsetFloat;
        float orbit_density_scale = kRunConfigUnsetFloat;
        float orbit_fill = kRunConfigUnsetFloat;
        float orbit_motion_strength = kRunConfigUnsetFloat;
        float orbit_shell_extinction = kRunConfigUnsetFloat;
        int temporal = -1;
        int local_volume = -1;
        int horizon_layer = -1;
    };

    std::string title = "cubey";
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    GridOptions grid{};
    SmokeOptions smoke{};
    PyroOptions pyro{};
    Water2DOptions water2d{};
    Water3DOptions water3d{};
    OceanOptions ocean{};
    PlanetOptions planet{};
    PbrOptions pbr{};
    GltfOptions gltf{};
    TerrainOptions terrain{};
    TerrainLabOptions terrain_lab{};
    AtmosphereOptions atmosphere{};
    CloudOptions clouds{};
    std::uint32_t frames = 0;
    std::uint32_t fps = 60;
    std::filesystem::path output_path = "cubey-output.png";
    std::filesystem::path config_path{};
    std::filesystem::path write_config_template_path{};
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
        if (!config.write_config_template_path.empty()) {
            return 0;
        }
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
