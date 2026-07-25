#include "terrain_source_catalog.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

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

void write_catalog(const std::filesystem::path& root, nlohmann::json variants) {
    std::ofstream(root / "variation-index.json")
        << nlohmann::json{
               {"schema", "cubey.terrain.landscape-variations.v1"},
               {"variants", std::move(variants)},
           }
               .dump(2)
        << '\n';
}

nlohmann::json valid_entry(std::string id = "alpine-range") {
    return {
        {"id", id},
        {"label", "Alpine range"},
        {"seed", 12345},
        {"heightfield", id + "/heightfield.json"},
        {"surface_fields", id + "/surface-fields.json"},
    };
}

void write_entry_files(const std::filesystem::path& root, std::string_view id) {
    const std::filesystem::path directory = root / id;
    std::filesystem::create_directories(directory);
    std::ofstream(directory / "heightfield.json") << "{}\n";
    std::ofstream(directory / "surface-fields.json") << "{}\n";
}

void test_loads_valid_catalog() {
    TemporaryDirectory temporary;
    write_entry_files(temporary.path(), "alpine-range");
    write_catalog(temporary.path(), nlohmann::json::array({valid_entry()}));

    const auto entries =
        cubey::projects::terrain::load_terrain_landscape_variation_catalog(temporary.path());

    require(entries.size() == 1U, "valid catalog should load one entry");
    require(entries.front().id == "alpine-range" && entries.front().seed == 12345U,
            "valid catalog should preserve identity");
    require(entries.front().heightfield_path.filename() == "heightfield.json" &&
                entries.front().surface_fields_path.filename() == "surface-fields.json",
            "valid catalog should resolve both manifests");
}

void test_rejects_duplicate_ids() {
    TemporaryDirectory temporary;
    write_entry_files(temporary.path(), "alpine-range");
    write_catalog(temporary.path(), nlohmann::json::array({valid_entry(), valid_entry()}));

    bool rejected = false;
    try {
        static_cast<void>(
            cubey::projects::terrain::load_terrain_landscape_variation_catalog(temporary.path()));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "catalog should reject duplicate ids");
}

void test_rejects_unsafe_paths() {
    TemporaryDirectory temporary;
    nlohmann::json entry = valid_entry();
    entry["heightfield"] = "../heightfield.json";
    write_catalog(temporary.path(), nlohmann::json::array({std::move(entry)}));

    bool rejected = false;
    try {
        static_cast<void>(
            cubey::projects::terrain::load_terrain_landscape_variation_catalog(temporary.path()));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "catalog should reject paths outside its root");
}

void test_rejects_missing_manifests() {
    TemporaryDirectory temporary;
    write_catalog(temporary.path(), nlohmann::json::array({valid_entry()}));

    bool rejected = false;
    try {
        static_cast<void>(
            cubey::projects::terrain::load_terrain_landscape_variation_catalog(temporary.path()));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "catalog should reject missing manifests");
}

} // namespace

int main() {
    try {
        test_loads_valid_catalog();
        test_rejects_duplicate_ids();
        test_rejects_unsafe_paths();
        test_rejects_missing_manifests();
        std::cout << "terrain source catalog tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain source catalog tests failed: " << error.what() << '\n';
        return 1;
    }
}
