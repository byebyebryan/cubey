#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace cubey {

struct RunConfig {
    std::string title = "cubey";
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    std::uint32_t frames = 0;
    std::filesystem::path output_path = "cubey-output.png";
    bool headless = false;
    bool validation = true;
    bool require_validation = false;
};

RunConfig parse_run_config(int argc, char** argv);

} // namespace cubey
