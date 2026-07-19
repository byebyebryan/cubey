#include "terrain_raster_height_source.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function> void require_throws(Function&& function, std::string_view message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

class Fixture {
  public:
    Fixture() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() /
               ("cubey-terrain-raster-study-" + std::to_string(suffix));
        std::filesystem::create_directories(root);
        values.resize(16U);
        for (std::uint32_t z = 0U; z < 4U; ++z) {
            for (std::uint32_t x = 0U; x < 4U; ++x) {
                values[z * 4U + x] = static_cast<float>(z * 10U + x);
            }
        }
        manifest = {
            {"schema", "cubey.terrain.raster-study.v1"},
            {"source", {{"id", "test-raster"}}},
            {"seed", 9012U},
            {"grid",
             {
                 {"width", 4U},
                 {"height", 4U},
                 {"sample_spacing_m", 10.0F},
                 {"sample_origin_x_m", 0.0F},
                 {"sample_origin_z_m", 0.0F},
             }},
            {"comparison",
             {
                 {"height_offset_m", -1.0F},
                 {"height_scale", 2.0F},
                 {"target_relief_m", 3500.0F},
             }},
            {"files",
             {
                 {"elevation",
                  {
                      {"path", "elevation.f32"},
                      {"dtype", "float32-le"},
                      {"layout", "row-major-zx"},
                      {"shape", {4U, 4U}},
                      {"byte_count", 16U * sizeof(float)},
                      {"sha256", std::string(64U, '0')},
                  }},
                 {"climate",
                  {
                      {"path", "climate.f32"},
                      {"dtype", "float32-le"},
                      {"layout", "channel-major-zx"},
                      {"shape", {4U, 4U, 4U}},
                      {"byte_count", 64U * sizeof(float)},
                      {"sha256", std::string(64U, '0')},
                  }},
             }},
        };
        write();
    }

    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    void write() const {
        {
            std::ofstream stream(root / "elevation.f32", std::ios::binary);
            stream.write(reinterpret_cast<const char*>(values.data()),
                         static_cast<std::streamsize>(values.size() * sizeof(float)));
        }
        {
            const std::vector<float> climate(64U, 0.0F);
            std::ofstream stream(root / "climate.f32", std::ios::binary);
            stream.write(reinterpret_cast<const char*>(climate.data()),
                         static_cast<std::streamsize>(climate.size() * sizeof(float)));
        }
        std::ofstream manifest_stream(root / "manifest.json");
        manifest_stream << manifest.dump(2) << '\n';
    }

    std::filesystem::path root{};
    std::vector<float> values{};
    nlohmann::json manifest{};
};

void test_loads_and_samples_calibrated_field() {
    Fixture fixture;
    const cubey::projects::terrain::TerrainRasterHeightSource source(fixture.root);
    require(source.metadata().id == "test-raster" && source.metadata().seed == 9012U &&
                source.width() == 4U && source.height() == 4U && source.sample_spacing_m() == 10.0F,
            "raster source should retain manifest metadata");
    require(source.sample_height({.world_xz = {10.0F, 20.0F}}) == 40.0F,
            "raster source should apply the shared comparison calibration");
    require(source.sample_height({.world_xz = {5.0F, 5.0F}}) == 9.0F,
            "raster source should bilinearly interpolate raw samples");
    require(source.contains_disk({15.0F, 15.0F}, 15.0F) &&
                !source.contains_disk({15.0F, 15.0F}, 16.0F),
            "raster source should expose strict field coverage");
    require_throws(
        [&source] { static_cast<void>(source.sample_height({.world_xz = {31.0F, 0.0F}})); },
        "raster source should reject out-of-domain queries");
}

void test_footprint_selects_filtered_mip() {
    Fixture fixture;
    for (std::uint32_t z = 0U; z < 4U; ++z) {
        for (std::uint32_t x = 0U; x < 4U; ++x) {
            fixture.values[z * 4U + x] = static_cast<float>((x + z) & 1U);
        }
    }
    fixture.manifest["comparison"]["height_offset_m"] = 0.0F;
    fixture.manifest["comparison"]["height_scale"] = 1.0F;
    fixture.write();
    const cubey::projects::terrain::TerrainRasterHeightSource source(fixture.root);
    const float fine = source.sample_height({.world_xz = {0.0F, 0.0F}, .footprint_m = 0.0F});
    const float filtered = source.sample_height({.world_xz = {0.0F, 0.0F}, .footprint_m = 20.0F});
    require(fine == 0.0F && std::abs(filtered - 0.5F) < 1.0e-6F,
            "raster source should filter unresolved checkerboard detail");
}

void test_rejects_invalid_contracts() {
    {
        Fixture fixture;
        fixture.manifest["schema"] = "unknown";
        fixture.write();
        require_throws(
            [&fixture] {
                cubey::projects::terrain::TerrainRasterHeightSource source(fixture.root);
            },
            "raster source should reject unknown schemas");
    }
    {
        Fixture fixture;
        fixture.manifest["files"]["elevation"]["byte_count"] = 4U;
        fixture.write();
        require_throws(
            [&fixture] {
                cubey::projects::terrain::TerrainRasterHeightSource source(fixture.root);
            },
            "raster source should reject mismatched byte counts");
    }
    {
        Fixture fixture;
        fixture.values[3] = std::numeric_limits<float>::quiet_NaN();
        fixture.write();
        require_throws(
            [&fixture] {
                cubey::projects::terrain::TerrainRasterHeightSource source(fixture.root);
            },
            "raster source should reject non-finite heights");
    }
    {
        Fixture fixture;
        fixture.manifest["files"]["elevation"]["path"] = "../elevation.f32";
        fixture.write();
        require_throws(
            [&fixture] {
                cubey::projects::terrain::TerrainRasterHeightSource source(fixture.root);
            },
            "raster source should reject paths outside the field directory");
    }
}

} // namespace

int main() {
    try {
        test_loads_and_samples_calibrated_field();
        test_footprint_selects_filtered_mip();
        test_rejects_invalid_contracts();
        std::cout << "terrain raster height source tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain raster height source tests failed: " << error.what() << '\n';
        return 1;
    }
}
