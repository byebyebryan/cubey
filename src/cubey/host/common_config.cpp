#include <cubey/host/common_config.h>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cubey::host {
namespace {

using config::OptionSpec;
using config::ValueType;

OptionSpec option(std::string path, std::string cli_name, std::string label, std::string group_path,
                  std::string help, ValueType type, config::Range range = {},
                  std::vector<std::string> enum_values = {}) {
    return {
        .path = std::move(path),
        .cli_name = std::move(cli_name),
        .negative_cli_name = {},
        .label = std::move(label),
        .group_path = std::move(group_path),
        .help = std::move(help),
        .type = type,
        .range = range,
        .enum_values = std::move(enum_values),
    };
}

bool parse_bool(std::string_view value, std::string_view path) {
    if (value == "true" || value == "1" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "false" || value == "0" || value == "no" || value == "off") {
        return false;
    }
    throw std::runtime_error("invalid bool for " + std::string(path));
}

void set_bool(bool& target, std::string_view value, std::string_view path,
              bool* coupled = nullptr) {
    target = parse_bool(value, path);
    if (coupled != nullptr && !target) {
        *coupled = false;
    }
}

void set_bool_json(bool& target, const nlohmann::json& value, std::string_view path,
                   bool* coupled = nullptr) {
    if (value.is_null()) {
        return;
    }
    if (!value.is_boolean()) {
        throw std::runtime_error("wrong JSON type for config option: " + std::string(path));
    }
    target = value.get<bool>();
    if (coupled != nullptr && !target) {
        *coupled = false;
    }
}

} // namespace

void normalize_common_run_config(CommonRunConfig& config, bool output_path_explicit) {
    if (config.width == 0U || config.height == 0U) {
        throw std::runtime_error("width and height must be positive");
    }
    if (config.fps == 0U) {
        throw std::runtime_error("fps must be positive");
    }
    if (config.profile_diagnostics && config.profile_output_prefix.empty()) {
        throw std::runtime_error("profile diagnostics require --profile-output");
    }
    if (config.require_validation) {
        config.validation = true;
    } else if (!config.validation) {
        config.require_validation = false;
    }
    if (config.capture_mode == CaptureMode::Video) {
        if (!config.headless) {
            throw std::runtime_error("video capture requires --headless");
        }
        if (config.frames == 0U) {
            config.frames = 300U;
        }
        if (!output_path_explicit && config.output_path == "cubey-output.png") {
            config.output_path = "cubey-output.mp4";
        }
    }
}

config::Schema common_run_config_schema(CommonRunConfig& config) {
    config::Schema::Builder builder = config::Schema::builder();
    builder
        .bind(option("title", "--title", "Title", "App",
                     "Window title. Project defaults are applied when this remains cubey.",
                     ValueType::String),
              config.title)
        .bind(option("width", "--width", "Width", "Window", "Initial render width in pixels.",
                     ValueType::UInt32, {.has_min = true, .min = 1.0}),
              config.width)
        .bind(option("height", "--height", "Height", "Window", "Initial render height in pixels.",
                     ValueType::UInt32, {.has_min = true, .min = 1.0}),
              config.height)
        .bind(option("frames", "--frames", "Frames", "Capture",
                     "Frame limit. Zero keeps a windowed app running until closed.",
                     ValueType::UInt32),
              config.frames)
        .bind(option("fps", "--fps", "FPS", "Capture", "Capture playback frame rate.",
                     ValueType::UInt32, {.has_min = true, .min = 1.0}),
              config.fps)
        .bind(option("output", "--output", "Output", "Capture",
                     "Output path for headless PNG or video capture.", ValueType::Path),
              config.output_path)
        .bind(option("headless", "--headless", "Headless", "Host", "Run without opening a window.",
                     ValueType::Bool),
              config.headless)
        .bind(option("print_frame_stats", "--print-frame-stats", "Print Frame Stats", "Host",
                     "Print periodic frame statistics to stdout.", ValueType::Bool),
              config.print_frame_stats)
        .bind(option("capture", "--capture", "Capture", "Capture", "Capture output mode.",
                     ValueType::Enum, {}, {"png", "video"}),
              config.capture_mode)
        .bind(option("profile.warmup_frames", "--profile-warmup-frames", "Warmup Frames",
                     "Profiling", "Frames to skip before profiling output starts.",
                     ValueType::UInt32),
              config.profile_warmup_frames)
        .bind(option("profile.diagnostic_interval", "--profile-diagnostic-interval",
                     "Diagnostic Interval", "Profiling", "Frame interval for profile diagnostics.",
                     ValueType::UInt32, {.has_min = true, .min = 1.0}),
              config.profile_diagnostic_interval)
        .bind(
            [&] {
                OptionSpec spec = option(
                    "profile.diagnostics", "--profile-diagnostics", "Diagnostics", "Profiling",
                    "Record project diagnostics into profile output.", ValueType::Bool);
                spec.negative_cli_name = "--no-profile-diagnostics";
                return spec;
            }(),
            config.profile_diagnostics);

    OptionSpec validation = option("validation", "--validation", "Validation", "Host",
                                   "Enable Vulkan validation when available.", ValueType::Bool);
    validation.negative_cli_name = "--no-validation";
    builder.bind_custom(
        std::move(validation),
        [&config](std::string_view value) {
            set_bool(config.validation, value, "validation", &config.require_validation);
        },
        [&config](const nlohmann::json& value) {
            set_bool_json(config.validation, value, "validation", &config.require_validation);
        },
        [&config] { return nlohmann::json(config.validation); });

    OptionSpec require_validation =
        option("require_validation", "--require-validation", "Require Validation", "Host",
               "Fail startup if validation layers are unavailable.", ValueType::Bool);
    builder.bind_custom(
        std::move(require_validation),
        [&config](std::string_view value) {
            set_bool(config.require_validation, value, "require_validation");
            if (config.require_validation) {
                config.validation = true;
            }
        },
        [&config](const nlohmann::json& value) {
            set_bool_json(config.require_validation, value, "require_validation");
            if (config.require_validation) {
                config.validation = true;
            }
        },
        [&config] { return nlohmann::json(config.require_validation); });

    OptionSpec profile_output =
        option("profile.output", "--profile-output", "Profile Output", "Profiling",
               "Profile output prefix. Relative bare names are written under outputs/profiles.",
               ValueType::Path);
    builder.bind_custom(
        std::move(profile_output),
        [&config](std::string_view value) {
            if (value.empty()) {
                throw std::runtime_error("profile.output must not be empty");
            }
            config.profile_output_prefix = std::filesystem::path(std::string(value));
            if (!config.profile_output_prefix.has_parent_path() &&
                !config.profile_output_prefix.is_absolute()) {
                config.profile_output_prefix =
                    std::filesystem::path("outputs") / "profiles" / config.profile_output_prefix;
            }
        },
        [&config](const nlohmann::json& value) {
            if (value.is_null()) {
                return;
            }
            if (!value.is_string()) {
                throw std::runtime_error("wrong JSON type for config option: profile.output");
            }
            const std::string text = value.get<std::string>();
            if (text.empty()) {
                throw std::runtime_error("profile.output must not be empty");
            }
            config.profile_output_prefix = std::filesystem::path(text);
            if (!config.profile_output_prefix.has_parent_path() &&
                !config.profile_output_prefix.is_absolute()) {
                config.profile_output_prefix =
                    std::filesystem::path("outputs") / "profiles" / config.profile_output_prefix;
            }
        },
        [&config] {
            return config.profile_output_prefix.empty()
                       ? nlohmann::json(nullptr)
                       : nlohmann::json(config.profile_output_prefix.string());
        });
    return std::move(builder).build();
}

CommonRunConfig parse_common_run_config(int argc, char** argv, config::ParseResult* result) {
    CommonRunConfig config;
    config::Schema schema = common_run_config_schema(config);
    config::ParseResult parsed = schema.parse_cli(argc, argv);
    const bool output_path_explicit =
        parsed.path_was_assigned("output") ||
        config.output_path != std::filesystem::path("cubey-output.png");
    normalize_common_run_config(config, output_path_explicit);
    if (parsed.write_config_template_path.has_value()) {
        schema.write_template(parsed.write_config_template_path.value());
    }
    if (result != nullptr) {
        *result = std::move(parsed);
    }
    return config;
}

} // namespace cubey::host
