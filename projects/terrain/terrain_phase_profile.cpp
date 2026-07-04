#include "terrain_phase_profile.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <utility>

namespace cubey::projects::terrain {
namespace {

[[nodiscard]] double elapsed_ms(TerrainPhaseProfile::TimePoint start,
                                TerrainPhaseProfile::TimePoint end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

[[nodiscard]] nlohmann::json metadata_json(const TerrainPhaseProfileMetadata& metadata) {
    nlohmann::json preview = nlohmann::json::object();
    if (!metadata.camera_preset.empty()) {
        preview["camera_preset"] = metadata.camera_preset;
    }
    if (!metadata.preview_surface.empty()) {
        preview["surface"] = metadata.preview_surface;
    }
    if (!metadata.preview_color.empty()) {
        preview["color"] = metadata.preview_color;
    }
    if (metadata.vertex_count > 0U || metadata.index_count > 0U ||
        metadata.triangle_count > 0U) {
        preview["vertices"] = metadata.vertex_count;
        preview["indices"] = metadata.index_count;
        preview["triangles"] = metadata.triangle_count;
    }

    nlohmann::json result{
        {"app_name", metadata.app_name},
        {"recipe_id", metadata.recipe_id},
        {"generator_revision", metadata.generator_revision},
        {"grid",
         {
             {"width", metadata.grid_width},
             {"height", metadata.grid_height},
             {"cell_size_m", metadata.cell_size_m},
         }},
        {"field_count", metadata.field_count},
        {"output_count", metadata.output_count},
    };
    if (!preview.empty()) {
        result["preview"] = std::move(preview);
    }
    return result;
}

} // namespace

TerrainPhaseProfile::TerrainPhaseProfile(std::filesystem::path output_prefix)
    : output_prefix_(std::move(output_prefix)) {}

void TerrainPhaseProfile::set_metadata(TerrainPhaseProfileMetadata metadata) {
    metadata_ = std::move(metadata);
}

void TerrainPhaseProfile::record_elapsed(std::string_view name, TimePoint start) {
    if (!enabled()) {
        return;
    }
    phases_.push_back(TerrainPhaseRecord{
        .name = std::string(name),
        .duration_ms = elapsed_ms(start, now()),
    });
}

void TerrainPhaseProfile::write() const {
    if (!enabled()) {
        return;
    }

    nlohmann::json phases = nlohmann::json::array();
    for (const TerrainPhaseRecord& phase : phases_) {
        phases.push_back({
            {"name", phase.name},
            {"duration_ms", phase.duration_ms},
        });
    }

    nlohmann::json output = metadata_json(metadata_);
    output["schema"] = "cubey.terrain.phase_profile.v1";
    output["phases"] = std::move(phases);

    const std::filesystem::path output_path = terrain_phase_profile_output_path(output_prefix_);
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path());
    }
    std::ofstream file(output_path);
    if (!file) {
        throw std::runtime_error("failed to write terrain phase profile: " +
                                 output_path.string());
    }
    file << std::setw(2) << output << '\n';
}

TerrainPhaseScope::TerrainPhaseScope(TerrainPhaseProfile& profile, std::string_view name)
    : profile_(&profile), name_(name), start_(TerrainPhaseProfile::now()) {}

TerrainPhaseScope::~TerrainPhaseScope() {
    finish();
}

TerrainPhaseScope::TerrainPhaseScope(TerrainPhaseScope&& other) noexcept
    : profile_(std::exchange(other.profile_, nullptr)), name_(std::move(other.name_)),
      start_(other.start_) {}

TerrainPhaseScope& TerrainPhaseScope::operator=(TerrainPhaseScope&& other) noexcept {
    if (this != &other) {
        finish();
        profile_ = std::exchange(other.profile_, nullptr);
        name_ = std::move(other.name_);
        start_ = other.start_;
    }
    return *this;
}

void TerrainPhaseScope::finish() noexcept {
    if (profile_ == nullptr) {
        return;
    }
    try {
        profile_->record_elapsed(name_, start_);
    } catch (...) {
    }
    profile_ = nullptr;
}

std::filesystem::path terrain_phase_profile_output_prefix(std::string_view value) {
    std::filesystem::path prefix{std::string(value)};
    if (prefix.empty()) {
        throw std::runtime_error("profile output prefix must not be empty");
    }
    if (!prefix.has_parent_path() && !prefix.is_absolute()) {
        prefix = std::filesystem::path("outputs") / "profiles" / prefix;
    }
    return prefix;
}

std::filesystem::path terrain_phase_profile_output_path(
    const std::filesystem::path& output_prefix) {
    std::filesystem::path output_path = output_prefix;
    output_path += ".terrain_phases.json";
    return output_path;
}

TerrainPhaseProfileMetadata terrain_phase_profile_metadata(std::string_view app_name,
                                                           const TerrainRegionConfig& config) {
    return TerrainPhaseProfileMetadata{
        .app_name = std::string(app_name),
        .recipe_id = config.recipe_id,
        .grid_width = config.grid_width,
        .grid_height = config.grid_height,
        .cell_size_m = config.cell_size_m,
        .generator_revision = config.generator_revision,
    };
}

} // namespace cubey::projects::terrain
