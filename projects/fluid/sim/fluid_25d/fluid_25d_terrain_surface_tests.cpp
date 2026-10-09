#include "fluid_25d_terrain_surface.h"

#include <cubey/asset/file_digest.h>
#include <cubey/terrain/terrain_surface_field.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace cubey::projects::fluid::fluid_25d;
namespace {
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn) {
    bool failed = false;
    try {
        fn();
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, "invalid surface input accepted");
}
void test_source_binding() {
    const auto root = std::filesystem::temp_directory_path() /
                      ("cubey-surface-binding-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    require(std::filesystem::create_directory(root), "cannot create private fixture");
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove_all(root, e);
        }
    } cleanup{root};
    std::vector<float> elevation(16U), climate(64U);
    for (unsigned z = 0; z < 4U; ++z)
        for (unsigned x = 0; x < 4U; ++x) {
            const auto i = std::size_t(z) * 4U + x;
            elevation[i] = float(z * 100U + x);
            climate[i] = 8.0F + float(z) + 0.1F * float(x);
            climate[16U + i] = 4.0F;
            climate[32U + i] = 1000.0F;
            climate[48U + i] = 0.25F;
        }
    const auto write = [](const std::filesystem::path& p, auto bytes) {
        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size_bytes()));
        require(bool(f), "failed fixture write");
    };
    write(root / "elevation.f32", std::span(elevation));
    write(root / "climate.f32", std::span(climate));
    const auto elevation_hash = cubey::asset::sha256_hex(std::as_bytes(std::span(elevation)));
    nlohmann::json hm{{"schema", "cubey.terrain.heightfield.v1"},
                      {"source", {{"id", "fixture"}}},
                      {"seed", 5},
                      {"grid",
                       {{"width", 4},
                        {"height", 4},
                        {"sample_spacing_m", 10},
                        {"sample_origin_x_m", 0},
                        {"sample_origin_z_m", 0}}},
                      {"height", {{"offset_m", 1.25}, {"scale", 0.75}, {"relief_scale_m", 1000}}},
                      {"files",
                       {{"elevation",
                         {{"path", "elevation.f32"},
                          {"dtype", "float32-le"},
                          {"layout", "row-major-zx"},
                          {"shape", {4, 4}},
                          {"byte_count", 64},
                          {"sha256", elevation_hash}}}}}};
    const auto hm_text = hm.dump();
    write(root / "heightfield.json", std::span(hm_text));
    nlohmann::json cm{{"schema", "cubey.terrain.surface-fields.study.v1"},
                      {"source", {{"generator", "fixture"}}},
                      {"seed", 5},
                      {"heightfield", {{"elevation_sha256", elevation_hash}}},
                      {"grid", hm["grid"]}};
    cm["files"]["climate"] = {
        {"path", "climate.f32"},
        {"dtype", "float32-le"},
        {"layout", "channel-major-zx"},
        {"shape", {4, 4, 4}},
        {"byte_count", 256},
        {"sha256", cubey::asset::sha256_hex(std::as_bytes(std::span(climate)))},
        {"channels",
         {{{"name", "temperature_mean"}, {"unit", "deg_c"}},
          {{"name", "temperature_stddev"}, {"unit", "deg_c"}},
          {{"name", "precipitation_annual"}, {"unit", "mm_per_year"}},
          {{"name", "precipitation_cv"}, {"unit", "fraction"}}}}};
    const auto cm_text = cm.dump();
    write(root / "surface-fields.json", std::span(cm_text));
    std::vector<float> bed;
    for (unsigned z = 1; z < 3U; ++z)
        for (unsigned x = 1; x < 4U; ++x)
            bed.push_back((elevation[std::size_t(z) * 4U + x] + 1.25F) * 0.75F);
    nlohmann::json identity{
        {"elevation_sha256", elevation_hash},
        {"manifest_sha256", cubey::asset::sha256_hex(std::as_bytes(std::span(hm_text)))},
        {"crop_xzwh", {1, 1, 3, 2}},
        {"crop_cell_size_m", 10},
        {"transformed_crop_sha256", cubey::asset::sha256_hex(std::as_bytes(std::span(bed)))}};
    const auto load = [&](const auto& id, const auto& source_bed) {
        return fluid_25d_load_terrain_surface(root / "heightfield.json", id, 3U, 2U, 10.0F, bed,
                                              source_bed);
    };
    const auto valid = load(identity, bed);
    require(valid.climate.size() == 6U, "rectangular source crop was not loaded");
    const cubey::asset::TerrainRasterHeightSource source(root / "heightfield.json");
    const cubey::asset::TerrainRasterClimateSource companion(root / "surface-fields.json");
    const auto field =
        cubey::terrain::make_terrain_surface_field(source, source.bounds(), &companion);
    require(valid.correlated.size() == 6U, "missing correlated source field");
    for (unsigned z = 0; z < 2; ++z)
        for (unsigned x = 0; x < 3; ++x)
            require(valid.correlated[std::size_t(z) * 3 + x] ==
                        field.sample({float(x + 1) * 10, float(z + 1) * 10}),
                    "Fluid weights differ from Terrain foundation samples");
    auto overlap_identity = identity;
    overlap_identity["crop_xzwh"] = {2, 1, 2, 2};
    const std::vector<float> overlap_bed{bed[1], bed[2], bed[4], bed[5]};
    overlap_identity["transformed_crop_sha256"] =
        cubey::asset::sha256_hex(std::as_bytes(std::span(overlap_bed)));
    const auto overlap = fluid_25d_load_terrain_surface(root / "heightfield.json", overlap_identity,
                                                        2, 2, 10, overlap_bed, overlap_bed);
    require(overlap.correlated[0] == valid.correlated[1] &&
                overlap.correlated[2] == valid.correlated[4],
            "crop changed source-correlated material weights");
    auto bad = identity;
    bad["manifest_sha256"] = std::string(64U, '0');
    rejects([&] { (void)load(bad, bed); });
    bad = identity;
    bad["elevation_sha256"] = std::string(64U, '0');
    rejects([&] { (void)load(bad, bed); });
    bad = identity;
    bad["crop_xzwh"] = {2, 1, 3, 2};
    rejects([&] { (void)load(bad, bed); });
    auto flipped = bed;
    std::reverse(flipped.begin(), flipped.end());
    bad = identity;
    bad["transformed_crop_sha256"] = cubey::asset::sha256_hex(std::as_bytes(std::span(flipped)));
    rejects([&] { (void)load(bad, flipped); });
    cm["seed"] = 6;
    const auto changed_cm = cm.dump();
    write(root / "surface-fields.json", std::span(changed_cm));
    rejects([&] { (void)load(identity, bed); });
}
void test_surface() {
    constexpr unsigned width = 129U, height = 97U;
    constexpr std::size_t count = std::size_t(width) * height;
    std::vector<float> bed(count, 100.0F);
    const auto original = bed;
    const std::vector<cubey::math::Vec4> wet(count, {16.0F, 4.0F, 2000.0F, 0.2F});
    const std::vector<cubey::math::Vec4> dry(count, {16.0F, 4.0F, 30.0F, 0.8F});
    const std::vector<cubey::math::Vec4> cold(count, {-10.0F, 4.0F, 2000.0F, 0.2F});
    const auto a = fluid_25d_build_terrain_surface(width, height, 30.0F, bed, wet);
    const auto b = fluid_25d_build_terrain_surface(width, height, 30.0F, bed, dry);
    const auto c = fluid_25d_build_terrain_surface(width, height, 30.0F, bed, cold);
    require(bed == original, "surface bake modified physical bed");
    require(a.landform == b.landform && b.landform == c.landform,
            "climate contaminated landform-only control");
    require(a.climate[0].y > b.climate[0].y && a.climate[0].z > b.climate[0].z,
            "wet/dry climate did not affect cover/moisture potential");
    require(c.climate[0].y < a.climate[0].y, "cold climate did not suppress cover potential");
    require(std::all_of(a.landform.begin(), a.landform.end(),
                        [&](auto v) { return v == a.landform[0]; }),
            "flat landform generated edge seams or invented detail");
    require(fluid_25d_build_terrain_surface(width, height, 30.0F, bed, wet).receipt == a.receipt,
            "surface bake is not deterministic");
    for (unsigned z = 0; z < height; ++z)
        for (unsigned x = 0; x < width; ++x)
            bed[std::size_t(z) * width + x] = float(x) * 90.0F;
    const auto steep = fluid_25d_build_terrain_surface(width, height, 30.0F, bed, wet);
    require(steep.climate[count / 2U].x > a.climate[0].x,
            "exposed slope did not receive more rock");
    for (const auto& weights : {steep.landform, steep.climate})
        for (auto v : weights) {
            for (int channel = 0; channel < 4; ++channel)
                require(std::isfinite(v[channel]) && v[channel] >= 0.0F && v[channel] <= 1.0F,
                        "surface weight out of bounds");
            require(v.x + v.w <= 1.00001F, "rock and snow overlap beyond ground budget");
        }
    auto bad = wet;
    bad[10].z = -1.0F;
    rejects([&] { (void)fluid_25d_build_terrain_surface(width, height, 30.0F, bed, bad); });
    bad = wet;
    bad[10].w = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { (void)fluid_25d_build_terrain_surface(width, height, 30.0F, bed, bad); });
    rejects([&] { (void)fluid_25d_build_terrain_surface(width, height, 0.0F, bed, wet); });
    rejects([&] {
        (void)fluid_25d_build_terrain_surface(width, height, 30.0F,
                                              std::span(bed).first(count - 1U), wet);
    });
    bed[0] = std::numeric_limits<float>::infinity();
    rejects([&] { (void)fluid_25d_build_terrain_surface(width, height, 30.0F, bed, wet); });
    require(fluid_25d_terrain_surface_mode("legacy") == 0U &&
                fluid_25d_terrain_surface_mode("landform") == 1U &&
                fluid_25d_terrain_surface_mode("climate") == 2U,
            "surface mode contract changed");
    rejects([] { (void)fluid_25d_terrain_surface_mode("biome"); });
    const auto receipt = nlohmann::json::parse(a.receipt);
    require(receipt.at("bed_sha256") ==
                    cubey::asset::sha256_hex(std::as_bytes(std::span(original))) &&
                receipt.at("height_modified") == false &&
                receipt.at("climate_is_live_wetness") == false,
            "surface provenance changed numerical meaning");
}
} // namespace
int main() {
    try {
        test_surface();
        test_source_binding();
        std::cout << "terrain surface controls PASS: deterministic, bounded, climate-isolated, bed "
                     "immutable\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
