#include <cubey/core/config_options.h>
#include <cubey/core/run_config.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace cubey {

RunConfig parse_run_config(int argc, char** argv) {
    RunConfig config;
    bool output_path_explicit = false;
    std::vector<std::string> deferred_sets;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        auto need_special_value = [&](std::string_view name) -> std::string_view {
            if (i + 1 >= argc) {
                throw std::runtime_error("missing value for " + std::string(name));
            }
            ++i;
            return argv[i];
        };

        if (arg == "--config") {
            const std::filesystem::path path{std::string(need_special_value("--config"))};
            const RunConfigFileApplyResult result = apply_run_config_file(config, path);
            output_path_explicit = output_path_explicit || result.output_path_set;
        } else if (arg == "--set") {
            deferred_sets.emplace_back(need_special_value("--set"));
        } else if (arg == "--write-config-template") {
            config.write_config_template_path =
                std::string(need_special_value("--write-config-template"));
        }
    }

    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        auto need_value = [&](std::string_view name) -> std::string_view {
            if (i + 1 >= argc) {
                throw std::runtime_error("missing value for " + std::string(name));
            }
            ++i;
            return argv[i];
        };

        if (arg == "--config" || arg == "--set" || arg == "--write-config-template") {
            static_cast<void>(need_value(arg));
        } else if (const ConfigOptionDescriptor* option = find_run_config_option_by_cli_name(arg)) {
            const std::string_view value =
                option->type == ConfigOptionType::Bool
                    ? (arg == option->negative_cli_name ? std::string_view("false")
                                                        : std::string_view("true"))
                    : need_value(arg);
            set_run_config_option_from_string(config, *option, value);
            if (option->id == RunConfigOptionId::OutputPath) {
                output_path_explicit = true;
            }
        } else {
            throw std::runtime_error("unknown argument: " + std::string(arg));
        }
    }

    for (const std::string& assignment : deferred_sets) {
        const std::size_t separator = assignment.find('=');
        if (separator == std::string::npos || separator == 0U) {
            throw std::runtime_error("--set expects path=value");
        }
        const std::string_view path{assignment.data(), separator};
        const std::string_view value{assignment.data() + separator + 1U,
                                     assignment.size() - separator - 1U};
        set_run_config_option_from_string(config, path, value);
        if (path == "output") {
            output_path_explicit = true;
        }
    }

    if (config.width == 0 || config.height == 0) {
        throw std::runtime_error("width and height must be positive");
    }
    if (config.pbr.ibl_intensity < 0.0F) {
        throw std::runtime_error("IBL intensity must be nonnegative");
    }
    if (config.fps == 0) {
        throw std::runtime_error("fps must be positive");
    }
    if (run_config_float_is_set(config.ocean.wire_opacity) &&
        (config.ocean.wire_opacity < 0.0F || config.ocean.wire_opacity > 1.0F)) {
        throw std::runtime_error("ocean wire opacity must be in [0, 1]");
    }
    if (run_config_float_is_set(config.ocean.camera_orbit_spin_degrees_per_second) &&
        (config.ocean.camera_orbit_spin_degrees_per_second < -360.0F ||
         config.ocean.camera_orbit_spin_degrees_per_second > 360.0F)) {
        throw std::runtime_error("ocean camera orbit spin must be in [-360, 360]");
    }
    if (config.planet.radius_m <= 0.0F) {
        throw std::runtime_error("planet radius must be positive");
    }
    if (config.planet.atmosphere_height_m < 0.0F) {
        throw std::runtime_error("planet atmosphere height must be nonnegative");
    }
    if (config.planet.camera_altitude_m < 0.0F) {
        throw std::runtime_error("planet camera altitude must be nonnegative");
    }
    if (run_config_float_is_set(config.planet.camera_orbit_spin_degrees_per_second) &&
        (config.planet.camera_orbit_spin_degrees_per_second < -360.0F ||
         config.planet.camera_orbit_spin_degrees_per_second > 360.0F)) {
        throw std::runtime_error("planet camera orbit spin must be in [-360, 360]");
    }
    if (run_config_float_is_set(config.planet.camera_surface_pitch_degrees) &&
        (config.planet.camera_surface_pitch_degrees < -90.0F ||
         config.planet.camera_surface_pitch_degrees > 90.0F)) {
        throw std::runtime_error("planet camera surface pitch must be in [-90, 90]");
    }
    if (run_config_float_is_set(config.planet.camera_surface_yaw_degrees) &&
        (config.planet.camera_surface_yaw_degrees < -360.0F ||
         config.planet.camera_surface_yaw_degrees > 360.0F)) {
        throw std::runtime_error("planet camera surface yaw must be in [-360, 360]");
    }
    if (!config.planet.camera_surface_look.empty() &&
        config.planet.camera_surface_look != "default" &&
        config.planet.camera_surface_look != "sun" &&
        config.planet.camera_surface_look != "antisun") {
        throw std::runtime_error("planet camera surface look must be default, sun, or antisun");
    }
    if (config.profile_diagnostics && config.profile_output_prefix.empty()) {
        throw std::runtime_error("profile diagnostics require --profile-output");
    }
    if (!config.atmosphere.time_of_day_mode.empty() &&
        config.atmosphere.time_of_day_mode != "manual" &&
        config.atmosphere.time_of_day_mode != "solar") {
        throw std::runtime_error("atmosphere time-of-day mode must be manual or solar");
    }
    if (!config.pbr.environment_source.empty() && config.pbr.environment_source != "static" &&
        config.pbr.environment_source != "atmosphere") {
        throw std::runtime_error("PBR environment source must be static or atmosphere");
    }
    if (!config.atmosphere.night_sky_mode.empty() && config.atmosphere.night_sky_mode != "human" &&
        config.atmosphere.night_sky_mode != "camera") {
        throw std::runtime_error("atmosphere night sky mode must be human or camera");
    }
    if (!config.atmosphere.ground_mode.empty() && config.atmosphere.ground_mode != "ground" &&
        config.atmosphere.ground_mode != "sky-only" &&
        config.atmosphere.ground_mode != "sky-only-no-ground-occlusion") {
        throw std::runtime_error(
            "atmosphere ground mode must be ground, sky-only, or sky-only-no-ground-occlusion");
    }
    if (!config.atmosphere.milky_way_layer.empty() &&
        config.atmosphere.milky_way_layer != "final" &&
        config.atmosphere.milky_way_layer != "stellar-emission" &&
        config.atmosphere.milky_way_layer != "dust-tau" &&
        config.atmosphere.milky_way_layer != "star-clouds" &&
        config.atmosphere.milky_way_layer != "hii-emission" &&
        config.atmosphere.milky_way_layer != "speckles") {
        throw std::runtime_error(
            "atmosphere Milky Way layer must be final, stellar-emission, dust-tau, "
            "star-clouds, hii-emission, or speckles");
    }
    if (config.atmosphere.time_of_day_mode == "solar" &&
        (run_config_float_is_set(config.atmosphere.sun_elevation_degrees) ||
         run_config_float_is_set(config.atmosphere.sun_azimuth_degrees))) {
        throw std::runtime_error("manual sun elevation/azimuth cannot be combined with solar time");
    }
    if (config.atmosphere.sun_elevation_degrees < -90.0F ||
        config.atmosphere.sun_elevation_degrees > 90.0F) {
        throw std::runtime_error("atmosphere sun elevation must be in [-90, 90]");
    }
    if (config.atmosphere.sun_azimuth_degrees < -360.0F ||
        config.atmosphere.sun_azimuth_degrees > 360.0F) {
        throw std::runtime_error("atmosphere sun azimuth must be in [-360, 360]");
    }
    if (config.atmosphere.camera_altitude_km < 0.0F) {
        throw std::runtime_error("atmosphere camera altitude must be nonnegative");
    }
    if (config.atmosphere.rayleigh_scale < 0.0F) {
        throw std::runtime_error("atmosphere Rayleigh scale must be nonnegative");
    }
    if (config.atmosphere.mie_scale < 0.0F) {
        throw std::runtime_error("atmosphere Mie scale must be nonnegative");
    }
    if (config.atmosphere.ozone_scale < 0.0F) {
        throw std::runtime_error("atmosphere ozone scale must be nonnegative");
    }
    if (config.atmosphere.time_hours < 0.0F || config.atmosphere.time_hours > 24.0F) {
        throw std::runtime_error("atmosphere time hours must be in [0, 24]");
    }
    if (config.atmosphere.day_of_year < 1.0F || config.atmosphere.day_of_year > 366.0F) {
        throw std::runtime_error("atmosphere day of year must be in [1, 366]");
    }
    if (config.atmosphere.latitude_degrees < -90.0F || config.atmosphere.latitude_degrees > 90.0F) {
        throw std::runtime_error("atmosphere latitude must be in [-90, 90]");
    }
    if (config.atmosphere.sun_azimuth_offset_degrees < -360.0F ||
        config.atmosphere.sun_azimuth_offset_degrees > 360.0F) {
        throw std::runtime_error("atmosphere sun azimuth offset must be in [-360, 360]");
    }
    if (config.atmosphere.time_speed_hours_per_second < 0.0F) {
        throw std::runtime_error("atmosphere time speed must be nonnegative");
    }
    if (config.atmosphere.exposure_bias < -4.0F || config.atmosphere.exposure_bias > 4.0F) {
        throw std::runtime_error("atmosphere exposure bias must be in [-4, 4]");
    }
    if (config.atmosphere.twilight_strength < 0.0F || config.atmosphere.twilight_strength > 4.0F) {
        throw std::runtime_error("atmosphere twilight strength must be in [0, 4]");
    }
    if (config.atmosphere.twilight_horizon_warmth < 0.0F ||
        config.atmosphere.twilight_horizon_warmth > 2.0F) {
        throw std::runtime_error("atmosphere twilight horizon warmth must be in [0, 2]");
    }
    if (config.atmosphere.star_intensity < 0.0F || config.atmosphere.star_intensity > 4.0F) {
        throw std::runtime_error("atmosphere star intensity must be in [0, 4]");
    }
    if (config.atmosphere.star_density < 0.0F || config.atmosphere.star_density > 1.0F) {
        throw std::runtime_error("atmosphere star density must be in [0, 1]");
    }
    if (config.atmosphere.milky_way_intensity < 0.0F ||
        config.atmosphere.milky_way_intensity > 4.0F) {
        throw std::runtime_error("atmosphere Milky Way intensity must be in [0, 4]");
    }
    if (config.atmosphere.milky_way_contrast < 0.0F ||
        config.atmosphere.milky_way_contrast > 4.0F) {
        throw std::runtime_error("atmosphere Milky Way contrast must be in [0, 4]");
    }
    if (config.atmosphere.light_pollution < 0.0F || config.atmosphere.light_pollution > 1.0F) {
        throw std::runtime_error("atmosphere light pollution must be in [0, 1]");
    }
    if (config.atmosphere.milky_way_variation < 0.0F ||
        config.atmosphere.milky_way_variation > 16.0F) {
        throw std::runtime_error("atmosphere Milky Way variation must be in [0, 16]");
    }
    if (config.atmosphere.moon_intensity < 0.0F || config.atmosphere.moon_intensity > 4.0F) {
        throw std::runtime_error("atmosphere moon intensity must be in [0, 4]");
    }
    if (config.atmosphere.moonlight_intensity < 0.0F ||
        config.atmosphere.moonlight_intensity > 4.0F) {
        throw std::runtime_error("atmosphere moonlight intensity must be in [0, 4]");
    }
    if (config.atmosphere.moon_phase_offset_days < 0.0F ||
        config.atmosphere.moon_phase_offset_days > 29.530588F) {
        throw std::runtime_error("atmosphere moon phase offset must be in [0, 29.530588]");
    }
    if (config.atmosphere.moon_size_scale <= 0.0F || config.atmosphere.moon_size_scale > 8.0F) {
        throw std::runtime_error("atmosphere moon size scale must be in (0, 8]");
    }
    if (config.smoke.dye_decay < 0.0F || config.smoke.dye_decay > 1.0F) {
        throw std::runtime_error("smoke dye decay must be in [0, 1]");
    }
    if (config.smoke.velocity_decay < 0.0F || config.smoke.velocity_decay > 1.0F) {
        throw std::runtime_error("smoke velocity decay must be in [0, 1]");
    }
    if (config.smoke.injector_radius <= 0.0F) {
        throw std::runtime_error("smoke injector radius must be positive");
    }
    if (config.smoke.injector_force < 0.0F) {
        throw std::runtime_error("smoke injector force must be nonnegative");
    }
    if (config.smoke.injector_propulsion < 0.0F) {
        throw std::runtime_error("smoke injector propulsion must be nonnegative");
    }
    if (config.smoke.injector_orbit_radius <= 0.0F) {
        throw std::runtime_error("smoke injector orbit radius must be positive");
    }
    if (config.smoke.injector_orbit_radius_spread < 0.0F) {
        throw std::runtime_error("smoke injector orbit radius spread must be nonnegative");
    }
    if (config.smoke.injector_orbit_angular_speed_spread < 0.0F) {
        throw std::runtime_error("smoke injector orbit angular speed spread must be nonnegative");
    }
    if (config.smoke.injector_orbit_phase_spread < 0.0F) {
        throw std::runtime_error("smoke injector orbit phase spread must be nonnegative");
    }
    if (config.smoke.vorticity < 0.0F) {
        throw std::runtime_error("smoke vorticity must be nonnegative");
    }
    if (config.pyro.source_radius <= 0.0F) {
        throw std::runtime_error("pyro source radius must be positive");
    }
    if (config.pyro.source_height < 0.0F || config.pyro.source_height > 1.0F) {
        throw std::runtime_error("pyro source height must be in [0, 1]");
    }
    if (config.pyro.source_force < 0.0F) {
        throw std::runtime_error("pyro source force must be nonnegative");
    }
    if (config.pyro.soot < 0.0F) {
        throw std::runtime_error("pyro soot must be nonnegative");
    }
    if (config.pyro.temperature < 0.0F) {
        throw std::runtime_error("pyro temperature must be nonnegative");
    }
    if (config.pyro.fuel < 0.0F) {
        throw std::runtime_error("pyro fuel must be nonnegative");
    }
    if (config.pyro.ignition_temperature < 0.0F) {
        throw std::runtime_error("pyro ignition temperature must be nonnegative");
    }
    if (config.pyro.burn_rate < 0.0F) {
        throw std::runtime_error("pyro burn rate must be nonnegative");
    }
    if (config.pyro.heat_output < 0.0F) {
        throw std::runtime_error("pyro heat output must be nonnegative");
    }
    if (config.pyro.soot_yield < 0.0F) {
        throw std::runtime_error("pyro soot yield must be nonnegative");
    }
    if (config.pyro.expansion < 0.0F) {
        throw std::runtime_error("pyro expansion must be nonnegative");
    }
    if (config.pyro.flame_cooling < 0.0F) {
        throw std::runtime_error("pyro flame cooling must be nonnegative");
    }
    if (config.pyro.shredding < 0.0F) {
        throw std::runtime_error("pyro shredding must be nonnegative");
    }
    if (config.pyro.turbulence < 0.0F) {
        throw std::runtime_error("pyro turbulence must be nonnegative");
    }
    if (config.pyro.obstacle_height < 0.0F || config.pyro.obstacle_height > 1.0F) {
        throw std::runtime_error("pyro obstacle height must be in [0, 1]");
    }
    if (config.pyro.obstacle_radius < 0.0F || config.pyro.obstacle_radius > 0.5F) {
        throw std::runtime_error("pyro obstacle radius must be in [0, 0.5]");
    }
    if (config.pyro.explosion_interval_seconds <= 0.0F) {
        throw std::runtime_error("explosion interval must be positive");
    }
    if (config.pyro.explosion_duration_seconds <= 0.0F) {
        throw std::runtime_error("explosion duration must be positive");
    }
    if (config.pyro.explosion_duration_seconds > config.pyro.explosion_interval_seconds) {
        throw std::runtime_error("explosion duration must not exceed the interval");
    }
    if (config.pyro.explosion_boost < 0.0F) {
        throw std::runtime_error("explosion boost must be nonnegative");
    }
    if (config.capture_mode == CaptureMode::Video) {
        if (!config.headless) {
            throw std::runtime_error("video capture requires --headless");
        }
        if (config.frames == 0) {
            config.frames = 300;
        }
        if (!output_path_explicit) {
            config.output_path = "cubey-output.mp4";
        }
    }

    if (!config.write_config_template_path.empty()) {
        write_run_config_template(config, config.write_config_template_path);
    }

    return config;
}

} // namespace cubey
