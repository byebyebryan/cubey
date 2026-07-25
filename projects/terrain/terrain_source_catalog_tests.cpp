#include "terrain_source_catalog.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#ifndef CUBEY_TERRAIN_SOURCE_PRESET_CATALOG
#error "CUBEY_TERRAIN_SOURCE_PRESET_CATALOG must be defined by the terrain CMake target"
#endif

#ifndef CUBEY_TERRAIN_OPTIONAL_PRESET_ASSETS
#error "CUBEY_TERRAIN_OPTIONAL_PRESET_ASSETS must be defined by the terrain CMake target"
#endif

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
                ("cubey-terrain-source-catalog-" + std::to_string(suffix));
        std::filesystem::create_directories(root_);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return root_;
    }

  private:
    std::filesystem::path root_{};
};

nlohmann::json default_entry() {
    return {
        {"id", "mountain-backdrop-1"},
        {"label", "Mountain backdrop 1"},
        {"tier", "default"},
        {"asset_directory", "default"},
        {"generation_target", "cubey_terrain_generate_default_asset"},
        {"generator",
         {
             {"mode", "canonical-default"},
             {"seed", 0},
             {"climate_output", false},
             {"model_native_origin", {{"i", -2048}, {"j", -6144}}},
         }},
        {"expected", {{"elevation_sha256", std::string(64U, 'a')}, {"climate_sha256", nullptr}}},
    };
}

nlohmann::json optional_entry(std::string id = "alpine-range-1") {
    std::string target_suffix = id;
    std::replace(target_suffix.begin(), target_suffix.end(), '-', '_');
    return {
        {"id", id},
        {"label", "Alpine range 1"},
        {"tier", "optional"},
        {"asset_directory", id},
        {"generation_target", "cubey_terrain_generate_" + target_suffix},
        {"source_study", "landscape-variations/alpine-range"},
        {"generator",
         {
             {"mode", "natural-region"},
             {"climate_output", true},
             {"candidate", {{"seed", 12345}}},
         }},
        {"expected",
         {
             {"elevation_sha256", std::string(64U, 'b')},
             {"climate_sha256", std::string(64U, 'c')},
         }},
    };
}

void write_catalog(const std::filesystem::path& path, nlohmann::json presets) {
    std::ofstream(path)
        << nlohmann::json{
               {"schema", "cubey.terrain.source-presets.v1"},
               {"default_preset", "mountain-backdrop-1"},
               {"policy",
                {
                    {"generated_assets_committed", false},
                    {"default_generation", "single-preset-single-query"},
                    {"optional_generation", "one-preset-per-explicit-target"},
                    {"study_generation", "developer-only"},
                }},
               {"presets", std::move(presets)},
           }
               .dump(2)
        << '\n';
}

void test_loads_catalog_without_generated_optional_assets() {
    TemporaryDirectory temporary;
    const std::filesystem::path catalog_path = temporary.path() / "presets.json";
    const std::filesystem::path asset_root = temporary.path() / "assets";
    write_catalog(catalog_path, nlohmann::json::array({default_entry(), optional_entry()}));

    const auto catalog =
        cubey::projects::terrain::load_terrain_source_preset_catalog(catalog_path, asset_root);

    require(catalog.default_id == "mountain-backdrop-1" &&
                catalog.default_label == "Mountain backdrop 1" && catalog.default_seed == 0U,
            "catalog should preserve default identity");
    require(catalog.optional_presets.size() == 1U, "catalog should expose one optional recipe");
    const auto& entry = catalog.optional_presets.front();
    require(entry.id == "alpine-range-1" && entry.seed == 12345U,
            "optional recipe should preserve identity");
    require(cubey::projects::terrain::terrain_source_availability(entry) ==
                cubey::projects::terrain::TerrainSourceAvailability::NotGenerated,
            "missing optional bundle should report not generated");
}

void test_reports_available_generating_and_incomplete_states() {
    TemporaryDirectory temporary;
    const std::filesystem::path catalog_path = temporary.path() / "presets.json";
    const std::filesystem::path asset_root = temporary.path() / "assets";
    write_catalog(catalog_path, nlohmann::json::array({default_entry(), optional_entry()}));
    const auto catalog =
        cubey::projects::terrain::load_terrain_source_preset_catalog(catalog_path, asset_root);
    const auto& entry = catalog.optional_presets.front();

    std::filesystem::create_directories(entry.heightfield_path.parent_path());
    std::ofstream(entry.heightfield_path) << "{}\n";
    require(cubey::projects::terrain::terrain_source_availability(entry) ==
                cubey::projects::terrain::TerrainSourceAvailability::Incomplete,
            "partial optional bundle should report incomplete");

    std::ofstream(entry.surface_fields_path) << "{}\n";
    require(cubey::projects::terrain::terrain_source_availability(entry) ==
                cubey::projects::terrain::TerrainSourceAvailability::Available,
            "complete optional bundle should report available");

    std::ofstream(entry.generation_marker_path) << "{}\n";
    require(cubey::projects::terrain::terrain_source_availability(entry) ==
                cubey::projects::terrain::TerrainSourceAvailability::Generating,
            "generation marker should take precedence over an old bundle");
}

void test_loads_committed_catalog_contract() {
    const auto catalog = cubey::projects::terrain::load_terrain_source_preset_catalog(
        std::filesystem::path(CUBEY_TERRAIN_SOURCE_PRESET_CATALOG),
        std::filesystem::path(CUBEY_TERRAIN_OPTIONAL_PRESET_ASSETS));

    require(catalog.default_id == "mountain-backdrop-1",
            "committed catalog should preserve the canonical default");
    require(catalog.optional_presets.size() == 4U,
            "committed catalog should expose four optional recipes");
}

void test_rejects_duplicate_ids() {
    TemporaryDirectory temporary;
    const std::filesystem::path catalog_path = temporary.path() / "presets.json";
    const nlohmann::json duplicate = optional_entry();
    write_catalog(catalog_path, nlohmann::json::array({default_entry(), duplicate, duplicate}));

    bool rejected = false;
    try {
        static_cast<void>(cubey::projects::terrain::load_terrain_source_preset_catalog(
            catalog_path, temporary.path() / "assets"));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "catalog should reject duplicate ids");
}

void test_rejects_unsafe_asset_identity() {
    TemporaryDirectory temporary;
    const std::filesystem::path catalog_path = temporary.path() / "presets.json";
    nlohmann::json entry = optional_entry();
    entry["asset_directory"] = "../alpine-range-1";
    write_catalog(catalog_path, nlohmann::json::array({default_entry(), std::move(entry)}));

    bool rejected = false;
    try {
        static_cast<void>(cubey::projects::terrain::load_terrain_source_preset_catalog(
            catalog_path, temporary.path() / "assets"));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "catalog should reject a directory that differs from its id");
}

} // namespace

int main() {
    try {
        test_loads_catalog_without_generated_optional_assets();
        test_reports_available_generating_and_incomplete_states();
        test_loads_committed_catalog_contract();
        test_rejects_duplicate_ids();
        test_rejects_unsafe_asset_identity();
        std::cout << "terrain source catalog tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain source catalog tests failed: " << error.what() << '\n';
        return 1;
    }
}
