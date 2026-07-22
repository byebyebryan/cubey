#include <cubey/asset/terrain_raster_height_source.h>

#include <nlohmann/json.hpp>
#include <openssl/evp.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

template <typename Function>
void require_throws_containing(Function&& function, std::string_view expected,
                               std::string_view message) {
    try {
        function();
    } catch (const std::exception& error) {
        if (std::string_view(error.what()).find(expected) != std::string_view::npos) {
            return;
        }
    }
    throw std::runtime_error(std::string(message));
}

[[nodiscard]] std::string sha256(std::span<const std::byte> bytes) {
    const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(),
                                                                          EVP_MD_CTX_free);
    if (!context) {
        throw std::runtime_error("failed to allocate test SHA-256 context");
    }
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_size = 0U;
    if (EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(context.get(), bytes.data(), bytes.size()) != 1 ||
        EVP_DigestFinal_ex(context.get(), digest.data(), &digest_size) != 1) {
        throw std::runtime_error("failed to compute test SHA-256");
    }
    constexpr char kHexDigits[] = "0123456789abcdef";
    std::string result;
    result.reserve(static_cast<std::size_t>(digest_size) * 2U);
    for (unsigned int index = 0U; index < digest_size; ++index) {
        const unsigned char value = digest[index];
        result.push_back(kHexDigits[value >> 4U]);
        result.push_back(kHexDigits[value & 0x0FU]);
    }
    return result;
}

class Fixture {
  public:
    Fixture() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() /
               ("cubey-terrain-heightfield-" + std::to_string(suffix));
        std::filesystem::create_directories(root);
        values.resize(16U);
        for (std::uint32_t z = 0U; z < 4U; ++z) {
            for (std::uint32_t x = 0U; x < 4U; ++x) {
                values[z * 4U + x] = static_cast<float>(z * 10U + x);
            }
        }
        manifest = {
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
            {"height",
             {
                 {"offset_m", -1.0F},
                 {"scale", 2.0F},
                 {"relief_scale_m", 3500.0F},
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
             }},
        };
        write();
    }

    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    void write() {
        manifest["files"]["elevation"]["sha256"] = sha256(std::as_bytes(std::span{values}));
        {
            std::ofstream stream(root / "elevation.f32", std::ios::binary);
            stream.write(reinterpret_cast<const char*>(values.data()),
                         static_cast<std::streamsize>(values.size() * sizeof(float)));
        }
        std::ofstream manifest_stream(root / "heightfield.json");
        manifest_stream << manifest.dump(2) << '\n';
    }

    std::filesystem::path root{};
    std::vector<float> values{};
    nlohmann::json manifest{};
};

void test_terrain_raster_height_source_loads_and_samples_calibrated_field() {
    Fixture fixture;
    const cubey::asset::TerrainRasterHeightSource source(fixture.root);
    require(source.metadata().id == "test-raster" && source.metadata().seed == 9012U &&
                source.width() == 4U && source.height() == 4U && source.sample_spacing_m() == 10.0F,
            "raster source should retain manifest metadata");
    require(source.provenance().manifest_path == fixture.root / "heightfield.json" &&
                source.provenance().generator == "test-raster" &&
                source.provenance().elevation_sha256 ==
                    fixture.manifest["files"]["elevation"]["sha256"].get<std::string>(),
            "raster source should expose manifest provenance");
    require(source.sample_height({.world_xz = {10.0F, 20.0F}}) == 40.0F,
            "raster source should apply the manifest height transform");
    require(source.sample_height({.world_xz = {5.0F, 5.0F}}) == 9.0F,
            "raster source should bilinearly interpolate raw samples");
    require(source.contains_disk({15.0F, 15.0F}, 15.0F) &&
                !source.contains_disk({15.0F, 15.0F}, 16.0F),
            "raster source should expose strict field coverage");
    const cubey::asset::TerrainHeightSourceBounds bounds = source.bounds();
    require(bounds.minimum_xz.x == 0.0F && bounds.minimum_xz.y == 0.0F &&
                bounds.maximum_xz.x == 30.0F && bounds.maximum_xz.y == 30.0F &&
                cubey::asset::terrain_height_source_bounds_center(bounds).x == 15.0F,
            "raster source should expose its complete world-space sample bounds");
    require_throws(
        [&source] { static_cast<void>(source.sample_height({.world_xz = {31.0F, 0.0F}})); },
        "raster source should reject out-of-domain queries");
}

void test_terrain_raster_height_source_footprint_selects_filtered_mip() {
    Fixture fixture;
    for (std::uint32_t z = 0U; z < 4U; ++z) {
        for (std::uint32_t x = 0U; x < 4U; ++x) {
            fixture.values[z * 4U + x] = static_cast<float>((x + z) & 1U);
        }
    }
    fixture.manifest["height"]["offset_m"] = 0.0F;
    fixture.manifest["height"]["scale"] = 1.0F;
    fixture.write();
    const cubey::asset::TerrainRasterHeightSource source(fixture.root);
    const float fine = source.sample_height({.world_xz = {0.0F, 0.0F}, .footprint_m = 0.0F});
    const float filtered = source.sample_height({.world_xz = {0.0F, 0.0F}, .footprint_m = 20.0F});
    require(fine == 0.0F && std::abs(filtered - 0.5F) < 1.0e-6F,
            "raster source should filter unresolved checkerboard detail");
}

void test_terrain_raster_height_source_rejects_invalid_contracts() {
    {
        Fixture fixture;
        fixture.manifest["files"]["elevation"]["sha256"] = std::string(64U, '0');
        std::ofstream manifest_stream(fixture.root / "heightfield.json");
        manifest_stream << fixture.manifest.dump(2) << '\n';
        manifest_stream.close();
        require_throws_containing(
            [&fixture] { cubey::asset::TerrainRasterHeightSource source(fixture.root); },
            "elevation SHA-256 does not match manifest",
            "raster source should report an elevation checksum mismatch distinctly");
    }
    {
        Fixture fixture;
        fixture.manifest["schema"] = "unknown";
        fixture.write();
        require_throws(
            [&fixture] {
                cubey::asset::TerrainRasterHeightSource source(fixture.root);
            },
            "raster source should reject unknown schemas");
    }
    {
        Fixture fixture;
        fixture.manifest["files"]["elevation"]["byte_count"] = 4U;
        fixture.write();
        require_throws(
            [&fixture] {
                cubey::asset::TerrainRasterHeightSource source(fixture.root);
            },
            "raster source should reject mismatched byte counts");
    }
    {
        Fixture fixture;
        fixture.values[3] = std::numeric_limits<float>::quiet_NaN();
        fixture.write();
        require_throws(
            [&fixture] {
                cubey::asset::TerrainRasterHeightSource source(fixture.root);
            },
            "raster source should reject non-finite heights");
    }
    {
        Fixture fixture;
        fixture.manifest["files"]["elevation"]["path"] = "../elevation.f32";
        fixture.write();
        require_throws(
            [&fixture] {
                cubey::asset::TerrainRasterHeightSource source(fixture.root);
            },
            "raster source should reject paths outside the field directory");
    }
    {
        Fixture fixture;
        fixture.manifest["height"]["scale"] = 0.0F;
        fixture.write();
        require_throws(
            [&fixture] {
                cubey::asset::TerrainRasterHeightSource source(fixture.root);
            },
            "raster source should reject an invalid height transform");
    }
    {
        Fixture fixture;
        fixture.manifest["files"]["elevation"]["shape"] = {2U, 8U};
        fixture.write();
        require_throws(
            [&fixture] {
                cubey::asset::TerrainRasterHeightSource source(fixture.root);
            },
            "raster source should reject an elevation shape mismatch");
    }
}
