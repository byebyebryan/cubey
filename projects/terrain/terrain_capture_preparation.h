#pragma once
#include <cubey/engine/terrain_water_capture_runtime.h>
#include <filesystem>
#include <memory>

namespace cubey::projects::terrain {
struct TerrainProjectConfig;
// Shared by application startup and staged Apply. CPU preparation only; upload,
// target creation and safe retirement stay with the existing GPU owner path.
[[nodiscard]] std::unique_ptr<cubey::TerrainWaterCaptureRuntime>
prepare_terrain_capture(const TerrainProjectConfig& config, const std::filesystem::path& shaders);
} // namespace cubey::projects::terrain
