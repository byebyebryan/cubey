#include <cubey/core/run_config.h>

#include <charconv>
#include <cmath>
#include <filesystem>
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
    if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(parsed)) {
        throw std::runtime_error("invalid float for " + std::string(name));
    }
    return parsed;
}

std::filesystem::path profile_output_prefix(std::string_view value) {
    std::filesystem::path prefix{std::string(value)};
    if (prefix.empty()) {
        throw std::runtime_error("profile output prefix must not be empty");
    }
    if (!prefix.has_parent_path() && !prefix.is_absolute()) {
        prefix = std::filesystem::path("outputs") / "profiles" / prefix;
    }
    return prefix;
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
            config.grid.width = parse_positive_u32(need_value("--grid-width"), "--grid-width");
        } else if (arg == "--grid-height") {
            config.grid.height = parse_positive_u32(need_value("--grid-height"), "--grid-height");
        } else if (arg == "--grid-depth") {
            config.grid.depth = parse_positive_u32(need_value("--grid-depth"), "--grid-depth");
        } else if (arg == "--shadow-grid-width") {
            config.pyro.shadow_grid.width =
                parse_positive_u32(need_value("--shadow-grid-width"), "--shadow-grid-width");
        } else if (arg == "--shadow-grid-height") {
            config.pyro.shadow_grid.height =
                parse_positive_u32(need_value("--shadow-grid-height"), "--shadow-grid-height");
        } else if (arg == "--shadow-grid-depth") {
            config.pyro.shadow_grid.depth =
                parse_positive_u32(need_value("--shadow-grid-depth"), "--shadow-grid-depth");
        } else if (arg == "--shadow-steps") {
            config.pyro.shadow_steps =
                parse_positive_u32(need_value("--shadow-steps"), "--shadow-steps");
        } else if (arg == "--shadow-update-interval") {
            config.pyro.shadow_update_interval = parse_positive_u32(
                need_value("--shadow-update-interval"), "--shadow-update-interval");
        } else if (arg == "--smoke-injectors") {
            config.smoke.injectors =
                parse_positive_u32(need_value("--smoke-injectors"), "--smoke-injectors");
        } else if (arg == "--smoke-pressure-iterations") {
            config.smoke.pressure_iterations = parse_positive_u32(
                need_value("--smoke-pressure-iterations"), "--smoke-pressure-iterations");
        } else if (arg == "--smoke-pressure-solver") {
            config.smoke.pressure_solver = std::string(need_value("--smoke-pressure-solver"));
        } else if (arg == "--smoke-dye-decay") {
            config.smoke.dye_decay =
                parse_float(need_value("--smoke-dye-decay"), "--smoke-dye-decay");
        } else if (arg == "--smoke-velocity-decay") {
            config.smoke.velocity_decay =
                parse_float(need_value("--smoke-velocity-decay"), "--smoke-velocity-decay");
        } else if (arg == "--smoke-injector-radius") {
            config.smoke.injector_radius =
                parse_float(need_value("--smoke-injector-radius"), "--smoke-injector-radius");
        } else if (arg == "--smoke-injector-force") {
            config.smoke.injector_force =
                parse_float(need_value("--smoke-injector-force"), "--smoke-injector-force");
        } else if (arg == "--smoke-injector-propulsion") {
            config.smoke.injector_propulsion = parse_float(
                need_value("--smoke-injector-propulsion"), "--smoke-injector-propulsion");
        } else if (arg == "--smoke-injector-orbit-radius") {
            config.smoke.injector_orbit_radius = parse_float(
                need_value("--smoke-injector-orbit-radius"), "--smoke-injector-orbit-radius");
        } else if (arg == "--smoke-injector-orbit-radius-spread") {
            config.smoke.injector_orbit_radius_spread =
                parse_float(need_value("--smoke-injector-orbit-radius-spread"),
                            "--smoke-injector-orbit-radius-spread");
        } else if (arg == "--smoke-injector-orbit-angular-speed") {
            config.smoke.injector_orbit_angular_speed =
                parse_float(need_value("--smoke-injector-orbit-angular-speed"),
                            "--smoke-injector-orbit-angular-speed");
        } else if (arg == "--smoke-injector-orbit-angular-speed-spread") {
            config.smoke.injector_orbit_angular_speed_spread =
                parse_float(need_value("--smoke-injector-orbit-angular-speed-spread"),
                            "--smoke-injector-orbit-angular-speed-spread");
        } else if (arg == "--smoke-injector-orbit-phase-spread") {
            config.smoke.injector_orbit_phase_spread =
                parse_float(need_value("--smoke-injector-orbit-phase-spread"),
                            "--smoke-injector-orbit-phase-spread");
        } else if (arg == "--smoke-vorticity") {
            config.smoke.vorticity =
                parse_float(need_value("--smoke-vorticity"), "--smoke-vorticity");
        } else if (arg == "--pyro-sources") {
            config.pyro.sources =
                parse_positive_u32(need_value("--pyro-sources"), "--pyro-sources");
        } else if (arg == "--pyro-source-height") {
            config.pyro.source_height =
                parse_float(need_value("--pyro-source-height"), "--pyro-source-height");
        } else if (arg == "--pyro-source-radius") {
            config.pyro.source_radius =
                parse_float(need_value("--pyro-source-radius"), "--pyro-source-radius");
        } else if (arg == "--pyro-source-force") {
            config.pyro.source_force =
                parse_float(need_value("--pyro-source-force"), "--pyro-source-force");
        } else if (arg == "--pyro-soot") {
            config.pyro.soot = parse_float(need_value("--pyro-soot"), "--pyro-soot");
        } else if (arg == "--pyro-temperature") {
            config.pyro.temperature =
                parse_float(need_value("--pyro-temperature"), "--pyro-temperature");
        } else if (arg == "--pyro-fuel") {
            config.pyro.fuel = parse_float(need_value("--pyro-fuel"), "--pyro-fuel");
        } else if (arg == "--pyro-buoyancy") {
            config.pyro.buoyancy = parse_float(need_value("--pyro-buoyancy"), "--pyro-buoyancy");
        } else if (arg == "--pyro-ignition-temperature") {
            config.pyro.ignition_temperature = parse_float(
                need_value("--pyro-ignition-temperature"), "--pyro-ignition-temperature");
        } else if (arg == "--pyro-burn-rate") {
            config.pyro.burn_rate = parse_float(need_value("--pyro-burn-rate"), "--pyro-burn-rate");
        } else if (arg == "--pyro-heat-output") {
            config.pyro.heat_output =
                parse_float(need_value("--pyro-heat-output"), "--pyro-heat-output");
        } else if (arg == "--pyro-soot-yield") {
            config.pyro.soot_yield =
                parse_float(need_value("--pyro-soot-yield"), "--pyro-soot-yield");
        } else if (arg == "--pyro-expansion") {
            config.pyro.expansion = parse_float(need_value("--pyro-expansion"), "--pyro-expansion");
        } else if (arg == "--pyro-flame-cooling") {
            config.pyro.flame_cooling =
                parse_float(need_value("--pyro-flame-cooling"), "--pyro-flame-cooling");
        } else if (arg == "--pyro-shredding") {
            config.pyro.shredding = parse_float(need_value("--pyro-shredding"), "--pyro-shredding");
        } else if (arg == "--pyro-turbulence") {
            config.pyro.turbulence =
                parse_float(need_value("--pyro-turbulence"), "--pyro-turbulence");
        } else if (arg == "--pyro-obstacle-height") {
            config.pyro.obstacle_height =
                parse_float(need_value("--pyro-obstacle-height"), "--pyro-obstacle-height");
        } else if (arg == "--pyro-obstacle-radius") {
            config.pyro.obstacle_radius =
                parse_float(need_value("--pyro-obstacle-radius"), "--pyro-obstacle-radius");
        } else if (arg == "--explosion-interval") {
            config.pyro.explosion_interval_seconds =
                parse_float(need_value("--explosion-interval"), "--explosion-interval");
        } else if (arg == "--explosion-duration") {
            config.pyro.explosion_duration_seconds =
                parse_float(need_value("--explosion-duration"), "--explosion-duration");
        } else if (arg == "--explosion-boost") {
            config.pyro.explosion_boost =
                parse_float(need_value("--explosion-boost"), "--explosion-boost");
        } else if (arg == "--frames") {
            config.frames = parse_u32(need_value("--frames"), "--frames");
        } else if (arg == "--fps") {
            config.fps = parse_u32(need_value("--fps"), "--fps");
        } else if (arg == "--print-frame-stats") {
            config.print_frame_stats = true;
        } else if (arg == "--profile-output") {
            config.profile_output_prefix = profile_output_prefix(need_value("--profile-output"));
        } else if (arg == "--profile-warmup-frames") {
            config.profile_warmup_frames =
                parse_u32(need_value("--profile-warmup-frames"), "--profile-warmup-frames");
        } else if (arg == "--profile-diagnostics") {
            config.profile_diagnostics = true;
        } else if (arg == "--profile-diagnostic-interval") {
            config.profile_diagnostic_interval = parse_positive_u32(
                need_value("--profile-diagnostic-interval"), "--profile-diagnostic-interval");
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
            config.gltf.input_path = std::string(need_value("--input"));
        } else if (arg == "--environment") {
            config.pbr.environment_path = std::string(need_value("--environment"));
        } else if (arg == "--debug-view") {
            config.debug_view = std::string(need_value("--debug-view"));
        } else if (arg == "--water2d-transfer") {
            config.water2d.transfer_mode = std::string(need_value("--water2d-transfer"));
        } else if (arg == "--water2d-transfer-limit") {
            config.water2d.transfer_limit = parse_positive_u32(
                need_value("--water2d-transfer-limit"), "--water2d-transfer-limit");
        } else if (arg == "--water2d-hose") {
            config.water2d.hose = 1;
        } else if (arg == "--no-water2d-hose") {
            config.water2d.hose = 0;
        } else if (arg == "--water2d-drain") {
            config.water2d.drain = 1;
        } else if (arg == "--no-water2d-drain") {
            config.water2d.drain = 0;
        } else if (arg == "--water2d-wave") {
            config.water2d.wave = 1;
        } else if (arg == "--no-water2d-wave") {
            config.water2d.wave = 0;
        } else if (arg == "--water3d-transfer") {
            config.water3d.transfer_mode = std::string(need_value("--water3d-transfer"));
        } else if (arg == "--water3d-transfer-limit") {
            config.water3d.transfer_limit = parse_positive_u32(
                need_value("--water3d-transfer-limit"), "--water3d-transfer-limit");
        } else if (arg == "--water3d-p2g-mode") {
            config.water3d.p2g_mode = std::string(need_value("--water3d-p2g-mode"));
        } else if (arg == "--water3d-hose") {
            config.water3d.hose = 1;
        } else if (arg == "--no-water3d-hose") {
            config.water3d.hose = 0;
        } else if (arg == "--water3d-drain") {
            config.water3d.drain = 1;
        } else if (arg == "--no-water3d-drain") {
            config.water3d.drain = 0;
        } else if (arg == "--water3d-rain") {
            config.water3d.rain = 1;
        } else if (arg == "--no-water3d-rain") {
            config.water3d.rain = 0;
        } else if (arg == "--water3d-wave") {
            config.water3d.wave = 1;
        } else if (arg == "--no-water3d-wave") {
            config.water3d.wave = 0;
        } else if (arg == "--water3d-whitewater") {
            config.water3d.whitewater = 1;
        } else if (arg == "--no-water3d-whitewater") {
            config.water3d.whitewater = 0;
        } else if (arg == "--ibl-intensity") {
            config.pbr.ibl_intensity =
                parse_float(need_value("--ibl-intensity"), "--ibl-intensity");
        } else if (arg == "--environment-rotation-degrees") {
            config.pbr.environment_rotation_degrees = parse_float(
                need_value("--environment-rotation-degrees"), "--environment-rotation-degrees");
        } else if (arg == "--exposure") {
            config.pbr.exposure = parse_float(need_value("--exposure"), "--exposure");
        } else if (arg == "--animation-index") {
            config.gltf.animation_index =
                parse_u32(need_value("--animation-index"), "--animation-index");
        } else if (arg == "--animation-speed") {
            config.gltf.animation_speed =
                parse_float(need_value("--animation-speed"), "--animation-speed");
        } else if (arg == "--pause-animation") {
            config.gltf.animation_paused = true;
        } else if (arg == "--smoke-obstacles") {
            config.smoke.obstacles = true;
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
    if (config.pbr.ibl_intensity < 0.0F) {
        throw std::runtime_error("IBL intensity must be nonnegative");
    }
    if (config.fps == 0) {
        throw std::runtime_error("fps must be positive");
    }
    if (config.profile_diagnostics && config.profile_output_prefix.empty()) {
        throw std::runtime_error("profile diagnostics require --profile-output");
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

    return config;
}

} // namespace cubey
