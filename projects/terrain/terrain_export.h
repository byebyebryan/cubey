#pragma once

#include "terrain_patch.h"

#include <cubey/engine/capture_queue.h>

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace cubey::projects::terrain {

[[nodiscard]] std::vector<std::uint8_t>
render_terrain_scalar_field_rgba8(const cubey::procedural::ScalarField2D& field,
                                  std::string_view field_name);
[[nodiscard]] cubey::CaptureTicket enqueue_terrain_scalar_field_png(
    cubey::CaptureQueue& captures, const cubey::procedural::ScalarField2D& field,
    std::string_view field_name, const std::filesystem::path& output_path);
void write_terrain_manifest(const TerrainPatchProduct& product,
                            const std::filesystem::path& output_dir);
void write_terrain_field_exports(const TerrainPatchProduct& product,
                                 const std::filesystem::path& output_dir);

} // namespace cubey::projects::terrain
