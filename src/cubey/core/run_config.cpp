#include <cubey/core/run_config.h>

#include <charconv>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cubey {
namespace {

std::uint32_t parse_u32(std::string_view value, const char* name) {
    std::uint64_t parsed = 0;
    const char* begin = value.data();
    const char* end = value.data() + value.size();
    auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end ||
        parsed > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("invalid unsigned integer for " + std::string(name));
    }
    return static_cast<std::uint32_t>(parsed);
}

std::uint32_t parse_positive_u32(std::string_view value, const char* name) {
    const std::uint32_t parsed = parse_u32(value, name);
    if (parsed == 0) {
        throw std::runtime_error(std::string(name) + " must be positive");
    }
    return parsed;
}

float parse_float(std::string_view value, const char* name) {
    float parsed = 0.0F;
    const char* begin = value.data();
    const char* end = value.data() + value.size();
    auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        throw std::runtime_error("invalid float for " + std::string(name));
    }
    return parsed;
}

} // namespace

RunConfig parse_run_config(int argc, char** argv) {
    RunConfig config;
    bool output_path_explicit = false;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        auto need_value = [&](std::string_view name) -> std::string_view {
            if (i + 1 >= argc) {
                throw std::runtime_error("missing value for " + std::string(name));
            }
            ++i;
            return argv[i];
        };

        if (arg == "--headless") {
            config.headless = true;
        } else if (arg == "--validation") {
            config.validation = true;
        } else if (arg == "--no-validation") {
            config.validation = false;
            config.require_validation = false;
        } else if (arg == "--require-validation") {
            config.validation = true;
            config.require_validation = true;
        } else if (arg == "--title") {
            config.title = std::string(need_value("--title"));
        } else if (arg == "--width") {
            config.width = parse_u32(need_value("--width"), "--width");
        } else if (arg == "--height") {
            config.height = parse_u32(need_value("--height"), "--height");
        } else if (arg == "--grid-width") {
            config.grid_width = parse_positive_u32(need_value("--grid-width"), "--grid-width");
        } else if (arg == "--grid-height") {
            config.grid_height = parse_positive_u32(need_value("--grid-height"), "--grid-height");
        } else if (arg == "--grid-depth") {
            config.grid_depth = parse_positive_u32(need_value("--grid-depth"), "--grid-depth");
        } else if (arg == "--shadow-grid-width") {
            config.shadow_grid_width =
                parse_positive_u32(need_value("--shadow-grid-width"), "--shadow-grid-width");
        } else if (arg == "--shadow-grid-height") {
            config.shadow_grid_height =
                parse_positive_u32(need_value("--shadow-grid-height"), "--shadow-grid-height");
        } else if (arg == "--shadow-grid-depth") {
            config.shadow_grid_depth =
                parse_positive_u32(need_value("--shadow-grid-depth"), "--shadow-grid-depth");
        } else if (arg == "--shadow-steps") {
            config.shadow_steps = parse_positive_u32(need_value("--shadow-steps"), "--shadow-steps");
        } else if (arg == "--shadow-update-interval") {
            config.shadow_update_interval = parse_positive_u32(
                need_value("--shadow-update-interval"), "--shadow-update-interval");
        } else if (arg == "--smoke-injectors") {
            config.smoke_injectors =
                parse_positive_u32(need_value("--smoke-injectors"), "--smoke-injectors");
        } else if (arg == "--smoke-injector-force") {
            config.smoke_injector_force =
                parse_float(need_value("--smoke-injector-force"), "--smoke-injector-force");
        } else if (arg == "--smoke-injector-propulsion") {
            config.smoke_injector_propulsion = parse_float(
                need_value("--smoke-injector-propulsion"), "--smoke-injector-propulsion");
        } else if (arg == "--smoke-injector-orbit-radius") {
            config.smoke_injector_orbit_radius = parse_float(
                need_value("--smoke-injector-orbit-radius"), "--smoke-injector-orbit-radius");
        } else if (arg == "--smoke-injector-orbit-radius-spread") {
            config.smoke_injector_orbit_radius_spread =
                parse_float(need_value("--smoke-injector-orbit-radius-spread"),
                            "--smoke-injector-orbit-radius-spread");
        } else if (arg == "--smoke-injector-orbit-angular-speed") {
            config.smoke_injector_orbit_angular_speed =
                parse_float(need_value("--smoke-injector-orbit-angular-speed"),
                            "--smoke-injector-orbit-angular-speed");
        } else if (arg == "--smoke-injector-orbit-angular-speed-spread") {
            config.smoke_injector_orbit_angular_speed_spread =
                parse_float(need_value("--smoke-injector-orbit-angular-speed-spread"),
                            "--smoke-injector-orbit-angular-speed-spread");
        } else if (arg == "--smoke-injector-orbit-phase-spread") {
            config.smoke_injector_orbit_phase_spread =
                parse_float(need_value("--smoke-injector-orbit-phase-spread"),
                            "--smoke-injector-orbit-phase-spread");
        } else if (arg == "--fluid-buoyancy") {
            config.fluid_buoyancy = parse_float(need_value("--fluid-buoyancy"), "--fluid-buoyancy");
        } else if (arg == "--fluid-scenario") {
            config.fluid_scenario = std::string(need_value("--fluid-scenario"));
        } else if (arg == "--fluid-sources") {
            config.fluid_sources = parse_positive_u32(need_value("--fluid-sources"),
                                                      "--fluid-sources");
        } else if (arg == "--fluid-source-radius") {
            config.fluid_source_radius =
                parse_float(need_value("--fluid-source-radius"), "--fluid-source-radius");
        } else if (arg == "--fluid-source-force") {
            config.fluid_source_force =
                parse_float(need_value("--fluid-source-force"), "--fluid-source-force");
        } else if (arg == "--fluid-smoke") {
            config.fluid_smoke = parse_float(need_value("--fluid-smoke"), "--fluid-smoke");
        } else if (arg == "--fluid-heat") {
            config.fluid_heat = parse_float(need_value("--fluid-heat"), "--fluid-heat");
        } else if (arg == "--fluid-flame") {
            config.fluid_flame = parse_float(need_value("--fluid-flame"), "--fluid-flame");
        } else if (arg == "--fluid-explosion-interval") {
            config.fluid_explosion_interval_seconds = parse_float(
                need_value("--fluid-explosion-interval"), "--fluid-explosion-interval");
        } else if (arg == "--fluid-explosion-duration") {
            config.fluid_explosion_duration_seconds = parse_float(
                need_value("--fluid-explosion-duration"), "--fluid-explosion-duration");
        } else if (arg == "--fluid-explosion-boost") {
            config.fluid_explosion_boost =
                parse_float(need_value("--fluid-explosion-boost"), "--fluid-explosion-boost");
        } else if (arg == "--fluid-fire-ignition-temperature") {
            config.fluid_fire_ignition_temperature =
                parse_float(need_value("--fluid-fire-ignition-temperature"),
                            "--fluid-fire-ignition-temperature");
        } else if (arg == "--fluid-fire-burn-rate") {
            config.fluid_fire_burn_rate =
                parse_float(need_value("--fluid-fire-burn-rate"), "--fluid-fire-burn-rate");
        } else if (arg == "--fluid-fire-heat-output") {
            config.fluid_fire_heat_output =
                parse_float(need_value("--fluid-fire-heat-output"), "--fluid-fire-heat-output");
        } else if (arg == "--fluid-fire-soot-yield") {
            config.fluid_fire_soot_yield =
                parse_float(need_value("--fluid-fire-soot-yield"), "--fluid-fire-soot-yield");
        } else if (arg == "--fluid-fire-expansion") {
            config.fluid_fire_expansion =
                parse_float(need_value("--fluid-fire-expansion"), "--fluid-fire-expansion");
        } else if (arg == "--fluid-fire-flame-cooling") {
            config.fluid_fire_flame_cooling = parse_float(
                need_value("--fluid-fire-flame-cooling"), "--fluid-fire-flame-cooling");
        } else if (arg == "--fluid-fire-shredding") {
            config.fluid_fire_shredding =
                parse_float(need_value("--fluid-fire-shredding"), "--fluid-fire-shredding");
        } else if (arg == "--fluid-fire-turbulence") {
            config.fluid_fire_turbulence =
                parse_float(need_value("--fluid-fire-turbulence"), "--fluid-fire-turbulence");
        } else if (arg == "--frames") {
            config.frames = parse_u32(need_value("--frames"), "--frames");
        } else if (arg == "--fps") {
            config.fps = parse_u32(need_value("--fps"), "--fps");
        } else if (arg == "--print-frame-stats") {
            config.print_frame_stats = true;
        } else if (arg == "--capture") {
            const std::string_view mode = need_value("--capture");
            if (mode == "png") {
                config.capture_mode = CaptureMode::Png;
            } else if (mode == "video") {
                config.capture_mode = CaptureMode::Video;
            } else {
                throw std::runtime_error("capture mode must be png or video");
            }
        } else if (arg == "--input") {
            config.input_path = std::string(need_value("--input"));
        } else if (arg == "--environment") {
            config.environment_path = std::string(need_value("--environment"));
        } else if (arg == "--debug-view") {
            config.debug_view = std::string(need_value("--debug-view"));
        } else if (arg == "--ibl-intensity") {
            config.ibl_intensity = parse_float(need_value("--ibl-intensity"), "--ibl-intensity");
        } else if (arg == "--environment-rotation-degrees") {
            config.environment_rotation_degrees = parse_float(
                need_value("--environment-rotation-degrees"), "--environment-rotation-degrees");
        } else if (arg == "--exposure") {
            config.exposure = parse_float(need_value("--exposure"), "--exposure");
        } else if (arg == "--animation-index") {
            config.animation_index =
                parse_u32(need_value("--animation-index"), "--animation-index");
        } else if (arg == "--animation-speed") {
            config.animation_speed =
                parse_float(need_value("--animation-speed"), "--animation-speed");
        } else if (arg == "--pause-animation") {
            config.animation_paused = true;
        } else if (arg == "--smoke-obstacles") {
            config.smoke_obstacles = true;
        } else if (arg == "--output") {
            config.output_path = std::string(need_value("--output"));
            output_path_explicit = true;
        } else {
            throw std::runtime_error("unknown argument: " + std::string(arg));
        }
    }

    if (config.width == 0 || config.height == 0) {
        throw std::runtime_error("width and height must be positive");
    }
    if (config.ibl_intensity < 0.0F) {
        throw std::runtime_error("IBL intensity must be nonnegative");
    }
    if (config.fps == 0) {
        throw std::runtime_error("fps must be positive");
    }
    if (config.smoke_injector_force < 0.0F) {
        throw std::runtime_error("smoke injector force must be nonnegative");
    }
    if (config.smoke_injector_propulsion < 0.0F) {
        throw std::runtime_error("smoke injector propulsion must be nonnegative");
    }
    if (config.smoke_injector_orbit_radius <= 0.0F) {
        throw std::runtime_error("smoke injector orbit radius must be positive");
    }
    if (config.smoke_injector_orbit_radius_spread < 0.0F) {
        throw std::runtime_error("smoke injector orbit radius spread must be nonnegative");
    }
    if (config.smoke_injector_orbit_angular_speed_spread < 0.0F) {
        throw std::runtime_error("smoke injector orbit angular speed spread must be nonnegative");
    }
    if (config.smoke_injector_orbit_phase_spread < 0.0F) {
        throw std::runtime_error("smoke injector orbit phase spread must be nonnegative");
    }
    if (config.fluid_source_radius <= 0.0F) {
        throw std::runtime_error("fluid source radius must be positive");
    }
    if (config.fluid_source_force < 0.0F) {
        throw std::runtime_error("fluid source force must be nonnegative");
    }
    if (config.fluid_smoke < 0.0F) {
        throw std::runtime_error("fluid smoke must be nonnegative");
    }
    if (config.fluid_heat < 0.0F) {
        throw std::runtime_error("fluid heat must be nonnegative");
    }
    if (config.fluid_flame < 0.0F) {
        throw std::runtime_error("fluid flame must be nonnegative");
    }
    if (config.fluid_explosion_interval_seconds <= 0.0F) {
        throw std::runtime_error("fluid explosion interval must be positive");
    }
    if (config.fluid_explosion_duration_seconds <= 0.0F) {
        throw std::runtime_error("fluid explosion duration must be positive");
    }
    if (config.fluid_explosion_duration_seconds > config.fluid_explosion_interval_seconds) {
        throw std::runtime_error("fluid explosion duration must not exceed the interval");
    }
    if (config.fluid_explosion_boost < 0.0F) {
        throw std::runtime_error("fluid explosion boost must be nonnegative");
    }
    if (config.fluid_fire_ignition_temperature < 0.0F) {
        throw std::runtime_error("fluid fire ignition temperature must be nonnegative");
    }
    if (config.fluid_fire_burn_rate < 0.0F) {
        throw std::runtime_error("fluid fire burn rate must be nonnegative");
    }
    if (config.fluid_fire_heat_output < 0.0F) {
        throw std::runtime_error("fluid fire heat output must be nonnegative");
    }
    if (config.fluid_fire_soot_yield < 0.0F) {
        throw std::runtime_error("fluid fire soot yield must be nonnegative");
    }
    if (config.fluid_fire_expansion < 0.0F) {
        throw std::runtime_error("fluid fire expansion must be nonnegative");
    }
    if (config.fluid_fire_flame_cooling < 0.0F) {
        throw std::runtime_error("fluid fire flame cooling must be nonnegative");
    }
    if (config.fluid_fire_shredding < 0.0F) {
        throw std::runtime_error("fluid fire shredding must be nonnegative");
    }
    if (config.fluid_fire_turbulence < 0.0F) {
        throw std::runtime_error("fluid fire turbulence must be nonnegative");
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

    return config;
}

} // namespace cubey
