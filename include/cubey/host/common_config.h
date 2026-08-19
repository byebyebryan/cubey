#pragma once

#include <cubey/core/capture_mode.h>
#include <cubey/core/config_schema.h>

#include <cstdint>
#include <filesystem>
#include <string>

namespace cubey::host {

// The host consumes only this boundary. Project fields remain in the owning
// project's config and are adapted at the application boundary as needed.
struct CommonRunConfig {
    std::string title = "cubey";
    std::uint32_t width = 1280U;
    std::uint32_t height = 720U;
    std::uint32_t frames = 0U;
    std::uint32_t fps = 60U;
    std::filesystem::path output_path = "cubey-output.png";
    std::filesystem::path profile_output_prefix{};
    std::uint32_t profile_warmup_frames = 0U;
    std::uint32_t profile_diagnostic_interval = 1U;
    CaptureMode capture_mode = CaptureMode::Png;
    bool headless = false;
    bool print_frame_stats = false;
    bool profile_diagnostics = false;
    bool validation = true;
    bool require_validation = false;

    CommonRunConfig() = default;
};

// Applies host-level invariants after all configuration sources have been
// layered. output_path_explicit distinguishes an explicit default path from
// the implicit capture default.
void normalize_common_run_config(CommonRunConfig& config, bool output_path_explicit = false);

[[nodiscard]] config::Schema common_run_config_schema(CommonRunConfig& config);
[[nodiscard]] CommonRunConfig parse_common_run_config(int argc, char** argv,
                                                      config::ParseResult* result = nullptr);

} // namespace cubey::host
