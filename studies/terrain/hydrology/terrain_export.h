#pragma once

#include "terrain_patch.h"

#include <cubey/engine/capture_queue.h>

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace cubey::projects::terrain_hydrology_lab {

struct TerrainExportOptions {
    bool write_raw_float32 = false;
};

[[nodiscard]] std::vector<std::uint8_t>
render_terrain_scalar_field_rgba8(const cubey::procedural::ScalarField2D& field,
                                  std::string_view field_name);
[[nodiscard]] std::vector<std::uint8_t>
encode_terrain_scalar_field_f32_le(const cubey::procedural::ScalarField2D& field);
[[nodiscard]] cubey::CaptureTicket enqueue_terrain_scalar_field_png(
    cubey::CaptureQueue& captures, const cubey::procedural::ScalarField2D& field,
    std::string_view field_name, const std::filesystem::path& output_path);
void write_terrain_manifest(const TerrainPatchProduct& product,
                            const std::filesystem::path& output_dir,
                            TerrainExportOptions options = {});
void write_terrain_field_exports(const TerrainPatchProduct& product,
                                 const std::filesystem::path& output_dir,
                                 TerrainExportOptions options = {});

} // namespace cubey::projects::terrain_hydrology_lab
