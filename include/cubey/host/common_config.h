#pragma once

#include <cubey/core/capture_mode.h>
#include <cubey/core/config_schema.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace cubey::host {

// The host consumes only this boundary. Project fields remain in the owning
// project's config and are adapted at the application boundary as needed.
struct CommonRunConfig {
    std::string title = "cubey";
    std::string anti_aliasing = "auto";
    std::uint32_t width = 1280U;
    std::uint32_t height = 720U;
    std::uint32_t frames = 0U;
    std::uint32_t fps = 60U;
    std::filesystem::path output_path = "cubey-output.png";
    std::filesystem::path profile_output_prefix{};
    std::uint32_t profile_warmup_frames = 0U;
    std::uint32_t profile_sample_interval = 1U;
    std::uint32_t profile_maximum_frames = 0U;
    std::uint32_t profile_diagnostic_interval = 1U;
    CaptureMode capture_mode = CaptureMode::Png;
    bool headless = false;
    bool headless_render_only = false;
    double run_seconds = 0.0;
    std::uint32_t render_fps = 0U; // Zero is unpaced; independent of capture fps.
    bool print_frame_stats = false;
    bool profile_diagnostics = false;
    bool validation = true;
    bool require_validation = false;

    CommonRunConfig() = default;
};

// A producer still has to admit the view before jittering or allocating history.
[[nodiscard]] inline bool native_taa_requested(const CommonRunConfig& config) {
    return config.anti_aliasing == "taa" || (config.anti_aliasing == "auto" && !config.headless);
}

void report_anti_aliasing(const CommonRunConfig& config, bool effective,
                          std::string_view bypass_reason);

// Applies host-level invariants after all configuration sources have been
// layered. output_path_explicit distinguishes an explicit default path from
// the implicit capture default.
void normalize_common_run_config(CommonRunConfig& config, bool output_path_explicit = false);

[[nodiscard]] config::Schema common_run_config_schema(
    CommonRunConfig& config, std::string_view anti_aliasing_path = "anti_aliasing");
[[nodiscard]] CommonRunConfig parse_common_run_config(int argc, char** argv,
                                                      config::ParseResult* result = nullptr);

} // namespace cubey::host
