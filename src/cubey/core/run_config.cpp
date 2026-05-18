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
        } else if (arg == "--injectors") {
            config.injectors = parse_positive_u32(need_value("--injectors"), "--injectors");
        } else if (arg == "--injector-motion") {
            config.injector_motion = std::string(need_value("--injector-motion"));
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
            config.environment_rotation_degrees =
                parse_float(need_value("--environment-rotation-degrees"),
                            "--environment-rotation-degrees");
        } else if (arg == "--exposure") {
            config.exposure = parse_float(need_value("--exposure"), "--exposure");
        } else if (arg == "--animation-index") {
            config.animation_index = parse_u32(need_value("--animation-index"),
                                               "--animation-index");
        } else if (arg == "--animation-speed") {
            config.animation_speed = parse_float(need_value("--animation-speed"),
                                                 "--animation-speed");
        } else if (arg == "--pause-animation") {
            config.animation_paused = true;
        } else if (arg == "--obstacles") {
            config.obstacles = true;
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
