#include "terrain_raster_climate_source.h"

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
               ("cubey-terrain-climate-" + std::to_string(suffix));
        std::filesystem::create_directories(root);
        elevation.assign(16U, 100.0F);
        climate.resize(4U * 16U);
        for (std::uint32_t z = 0U; z < 4U; ++z) {
            for (std::uint32_t x = 0U; x < 4U; ++x) {
                const std::size_t index = static_cast<std::size_t>(z) * 4U + x;
                climate[index] = static_cast<float>(z * 10U + x);
                climate[16U + index] = 8.0F;
                climate[32U + index] = 600.0F;
                climate[48U + index] = 0.35F;
            }
        }
        const std::string hash(64U, '0');
        height_manifest = {
            {"schema", "cubey.terrain.heightfield.v1"},
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
            {"height", {{"offset_m", 0.0F}, {"scale", 1.0F}, {"relief_scale_m", 1000.0F}}},
            {"files",
             {{"elevation",
               {
                   {"path", "elevation.f32"},
                   {"dtype", "float32-le"},
                   {"layout", "row-major-zx"},
                   {"shape", {4U, 4U}},
                   {"byte_count", 16U * sizeof(float)},
                   {"sha256", hash},
               }}}},
        };
        climate_manifest = {
            {"schema", "cubey.terrain.surface-fields.study.v1"},
            {"source", {{"generator", "test"}, {"model_id", "test-climate"}}},
            {"seed", 9012U},
            {"heightfield",
             {{"schema", "cubey.terrain.heightfield.v1"}, {"elevation_sha256", hash}}},
            {"grid",
             {
                 {"width", 4U},
                 {"height", 4U},
                 {"sample_spacing_m", 10.0F},
                 {"sample_origin_x_m", 0.0F},
                 {"sample_origin_z_m", 0.0F},
             }},
            {"files",
             {{"climate",
               {
                   {"path", "climate.f32"},
                   {"dtype", "float32-le"},
                   {"layout", "channel-major-zx"},
                   {"shape", {4U, 4U, 4U}},
                   {"byte_count", 64U * sizeof(float)},
                   {"sha256", std::string(64U, '1')},
                   {"channels",
                    {
                        {{"name", "temperature_mean"}, {"unit", "deg_c"}},
                        {{"name", "temperature_stddev"}, {"unit", "deg_c"}},
                        {{"name", "precipitation_annual"}, {"unit", "mm_per_year"}},
                        {{"name", "precipitation_cv"}, {"unit", "fraction"}},
                    }},
               }}}},
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
            stream.write(reinterpret_cast<const char*>(elevation.data()),
                         static_cast<std::streamsize>(elevation.size() * sizeof(float)));
        }
        {
            std::ofstream stream(root / "climate.f32", std::ios::binary);
            stream.write(reinterpret_cast<const char*>(climate.data()),
                         static_cast<std::streamsize>(climate.size() * sizeof(float)));
        }
        std::ofstream(root / "heightfield.json") << height_manifest.dump(2) << '\n';
        std::ofstream(root / "surface-fields.json") << climate_manifest.dump(2) << '\n';
    }

    std::filesystem::path root{};
    std::vector<float> elevation{};
    std::vector<float> climate{};
    nlohmann::json height_manifest{};
    nlohmann::json climate_manifest{};
};

void test_loads_samples_and_binds_matching_source() {
    Fixture fixture;
    const cubey::projects::terrain::TerrainRasterHeightSource height(fixture.root);
    const cubey::projects::terrain::TerrainRasterClimateSource climate(fixture.root);
    cubey::projects::terrain::validate_terrain_climate_binding(height, climate);

    const cubey::projects::terrain::TerrainClimateSample sample = climate.sample({5.0F, 5.0F});
    require(std::abs(sample.temperature_mean_c - 5.5F) < 1.0e-6F &&
                sample.temperature_stddev_c == 8.0F &&
                sample.precipitation_annual_mm == 600.0F && sample.precipitation_cv == 0.35F,
            "climate source should bilinearly sample all normalized channels");
    require(climate.contains({-5.0F, -5.0F}) && climate.contains({35.0F, 35.0F}) &&
                !climate.contains({36.0F, 0.0F}),
            "climate source should expose complete area-averaged cell coverage");
}

void test_rejects_invalid_contracts() {
    {
        Fixture fixture;
        fixture.climate_manifest["files"]["climate"]["channels"][1]["unit"] = "native";
        fixture.write();
        require_throws(
            [&fixture] {
                cubey::projects::terrain::TerrainRasterClimateSource source(fixture.root);
            },
            "climate source should reject unsupported channel units");
    }
    {
        Fixture fixture;
        fixture.climate[48U] = 1.5F;
        fixture.write();
        require_throws(
            [&fixture] {
                cubey::projects::terrain::TerrainRasterClimateSource source(fixture.root);
            },
            "climate source should reject coefficients outside the declared range");
    }
    {
        Fixture fixture;
        fixture.climate[0U] = std::numeric_limits<float>::quiet_NaN();
        fixture.write();
        require_throws(
            [&fixture] {
                cubey::projects::terrain::TerrainRasterClimateSource source(fixture.root);
            },
            "climate source should reject non-finite fields");
    }
    {
        Fixture fixture;
        fixture.climate_manifest["files"]["climate"]["path"] = "../climate.f32";
        fixture.write();
        require_throws(
            [&fixture] {
                cubey::projects::terrain::TerrainRasterClimateSource source(fixture.root);
            },
            "climate source should reject paths outside the package");
    }
}

void test_rejects_mismatched_heightfield_binding() {
    Fixture fixture;
    fixture.climate_manifest["seed"] = 42U;
    fixture.write();
    const cubey::projects::terrain::TerrainRasterHeightSource height(fixture.root);
    const cubey::projects::terrain::TerrainRasterClimateSource climate(fixture.root);
    require_throws(
        [&height, &climate] {
            cubey::projects::terrain::validate_terrain_climate_binding(height, climate);
        },
        "climate source should reject a different heightfield seed");
}

} // namespace

int main() {
    try {
        test_loads_samples_and_binds_matching_source();
        test_rejects_invalid_contracts();
        test_rejects_mismatched_heightfield_binding();
        std::cout << "terrain raster climate source tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain raster climate source tests failed: " << error.what() << '\n';
        return 1;
    }
}
