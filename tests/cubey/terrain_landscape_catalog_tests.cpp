#include <chrono>
#include <cubey/asset/file_digest.h>
#include <cubey/asset/terrain_landscape_catalog.h>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
void write(const std::filesystem::path& p, const std::string& value) {
    std::ofstream(p) << value;
}
std::string hash(const std::string& s) {
    return cubey::asset::sha256_hex(std::as_bytes(std::span{s}));
}
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("invalid landscape was accepted");
}
Json bounds(float r) {
    return {{"minimum", {-r, -r}}, {"maximum", {r, r}}};
}
} // namespace
int main(int argc, char** argv) {
    try {
        using namespace cubey::asset;
        if (argc == 3) {
            const auto catalog = load_terrain_landscape_catalog(argv[1]);
            for (const auto& preset : catalog.presets) {
                const auto probe = probe_terrain_landscape(preset, argv[2]);
                require(probe.availability == TerrainLandscapeAvailability::Available,
                        probe.reason.c_str());
                std::cout << preset.id << ": metadata/payload-size admission PASS\n";
            }
            return 0;
        }
        const auto root =
            std::filesystem::temp_directory_path() /
            ("cubey-landscape-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(root);
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::filesystem::remove_all(path);
            }
        } cleanup{root};
        std::filesystem::create_directories(root / "cache/site");
        const auto site_root = root / "cache/site";
        const std::string world_id(64, '1');
        const Json producer{{"id", "test"}};
        const Json height{{"offset_m", 0}, {"scale", 1}};
        const Json world{{"schema", "cubey.terrain.world-recipe.v1"},
                         {"world_id", world_id},
                         {"seed", 0},
                         {"producer", producer},
                         {"height_transform", height},
                         {"survey_bounds", bounds(131072)},
                         {"coordinate_mapping",
                          {{"native_spacing_m", 30},
                           {"world_origin_xz_m", {0, 0}},
                           {"model_origin_ij", {0, 0}}}}};
        const Json field{
            {"schema", "cubey.terrain.heightfield.v1"},
            {"seed", 0},
            {"source", producer},
            {"height", height},
            {"provenance",
             {{"world_id", world_id}, {"model_native_origin", {{"i", -2}, {"j", -2}}}}},
            {"grid",
             {{"width", 4},
              {"height", 4},
              {"sample_spacing_m", 30},
              {"sample_origin_x_m", -60},
              {"sample_origin_z_m", -60},
              {"axis_mapping", {{"world_x", "model_j"}, {"world_z", "model_i"}}}}},
            {"files", {{"elevation", {{"path", "elevation.f32"}, {"byte_count", 4}}}}}};
        Json levels = Json::array();
        for (float span : {2.F, 4.F, 8.F, 16.F})
            levels.push_back({{"span_m", span}, {"bounds", bounds(span * .5F)}});
        const Json site{{"schema", "cubey.terrain.site-coverage.v1"},
                        {"site_id", "test"},
                        {"world_id", world_id},
                        {"center_xz_m", {0, 0}},
                        {"levels", levels},
                        {"camera_margin_m", 0},
                        {"sampling_margin_m", 0},
                        {"admitted", true},
                        {"rejection_reason", nullptr},
                        {"heightfield", "heightfield.json"},
                        {"surface_fields", "surface-fields.json"}};
        write(site_root / "world-recipe.json", world.dump());
        write(site_root / "heightfield.json", field.dump());
        write(site_root / "surface-fields.json",
              Json{{"files", {{"climate", {{"path", "climate.f32"}, {"byte_count", 4}}}}}}.dump());
        write(site_root / "elevation.f32", "1234");
        write(site_root / "climate.f32", "1234");
        write(site_root / "site.json", site.dump());
        Json preset{{"id", "test-dry"},
                    {"label", "Test"},
                    {"site_id", "test"},
                    {"water_label", "Terrain only"},
                    {"site", {{"path", "site/site.json"}, {"sha256", hash(site.dump())}}},
                    {"water", nullptr}};
        Json catalog{{"schema", "cubey.terrain.landscape-catalog.v1"},
                     {"proposed_default", "test-dry"},
                     {"presets", Json::array({preset})}};
        const auto path = root / "catalog.json";
        auto load = [&] { return load_terrain_landscape_catalog(path); };
        write(path, catalog.dump());
        auto valid = load();
        require(probe_terrain_landscape(valid.presets[0], root / "cache").availability ==
                    TerrainLandscapeAvailability::Available,
                "valid dry preset unavailable");
        require(!resolve_terrain_landscape(valid.presets[0], root / "cache").capture,
                "dry preset fabricated a capture");
        require(probe_terrain_landscape(valid.presets[0], root / "missing").availability ==
                    TerrainLandscapeAvailability::Missing,
                "missing cache not classified");
        std::filesystem::create_directories(root / "partial/site");
        require(probe_terrain_landscape(valid.presets[0], root / "partial").availability ==
                    TerrainLandscapeAvailability::Incomplete,
                "partial cache not classified");
        for (const std::string badpath : {"../site.json", "/tmp/site.json", "./site.json"}) {
            auto bad = catalog;
            bad["presets"][0]["site"]["path"] = badpath;
            write(path, bad.dump());
            rejects(load);
        }
        auto bad = catalog;
        bad["presets"].push_back(preset);
        write(path, bad.dump());
        rejects(load);
        bad = catalog;
        bad["proposed_default"] = "unknown";
        write(path, bad.dump());
        rejects(load);
        bad = catalog;
        bad["extra"] = true;
        write(path, bad.dump());
        rejects(load);
        write(path, catalog.dump().insert(1, "\"schema\":\"duplicate\","));
        rejects(load);
        write(path, catalog.dump());
        write(site_root / "site.json", "changed");
        require(probe_terrain_landscape(valid.presets[0], root / "cache").availability ==
                    TerrainLandscapeAvailability::Invalid,
                "changed pin was accepted");
        write(site_root / "site.json", site.dump());
        const Json capture{{"fields", {{"depth", {{"path", "depth.f32"}, {"byte_count", 4}}}}}};
        write(site_root / "capture.json", capture.dump());
        write(site_root / "depth.f32", "1234");
        const Json binding{
            {"schema", "cubey.terrain.site-water-binding.v1"},
            {"site_id", "test"},
            {"world_id", world_id},
            {"site_manifest_sha256", hash(site.dump())},
            {"world_recipe_sha256", hash(world.dump())},
            {"heightfield_manifest_sha256", hash(field.dump())},
            {"capture", {{"path", "capture.json"}, {"sha256", hash(capture.dump())}}}};
        write(site_root / "binding.json", binding.dump());
        auto wet = valid.presets[0];
        wet.water = TerrainLandscapeReference{"site/binding.json", hash(binding.dump())};
        require(probe_terrain_landscape(wet, root / "cache").availability ==
                    TerrainLandscapeAvailability::Available,
                "valid wet metadata unavailable");
        const auto probe_wet = [&] {
            return probe_terrain_landscape(wet, root / "cache").availability;
        };
        for (const std::string name :
             {"elevation.f32", "climate.f32", "depth.f32", "world-recipe.json", "heightfield.json",
              "surface-fields.json", "capture.json", "binding.json", "site.json"}) {
            const auto original = site_root / name;
            const auto retained = site_root / (name + ".retained");
            std::filesystem::rename(original, retained);
            require(probe_wet() == TerrainLandscapeAvailability::Incomplete,
                    "missing component not incomplete");
            std::filesystem::rename(retained, original);
        }
        // Present corruption wins over another missing component.
        std::filesystem::rename(site_root / "binding.json", site_root / "binding.retained");
        write(site_root / "site.json", "changed");
        require(probe_wet() == TerrainLandscapeAvailability::Invalid,
                "absent water masked changed site pin");
        write(site_root / "site.json", site.dump());
        std::filesystem::rename(site_root / "binding.retained", site_root / "binding.json");
        std::filesystem::rename(site_root / "elevation.f32", site_root / "elevation.retained");
        write(site_root / "depth.f32", "bad-size");
        require(probe_wet() == TerrainLandscapeAvailability::Invalid,
                "absent terrain masked wrong water size");
        write(site_root / "depth.f32", "1234");
        std::filesystem::rename(site_root / "elevation.retained", site_root / "elevation.f32");
        // Unsafe dangling links are invalid, not missing-cache fallback.
        std::filesystem::remove(site_root / "depth.f32");
        std::filesystem::create_symlink(root / "outside-missing", site_root / "depth.f32");
        require(probe_wet() == TerrainLandscapeAvailability::Invalid,
                "unsafe dangling link classified incomplete");
        std::filesystem::remove(site_root / "depth.f32");
        write(site_root / "depth.f32", "1234");
        const auto permissions =
            std::filesystem::status(site_root / "surface-fields.json").permissions();
        std::filesystem::permissions(site_root / "surface-fields.json",
                                     std::filesystem::perms::none);
        if (!std::ifstream(site_root / "surface-fields.json"))
            require(probe_wet() == TerrainLandscapeAvailability::Invalid,
                    "unreadable metadata classified incomplete");
        std::filesystem::permissions(site_root / "surface-fields.json", permissions);
        std::filesystem::rename(site_root / "world-recipe.json", site_root / "world.retained");
        auto changed_field = field;
        changed_field["extra"] = true;
        write(site_root / "heightfield.json", changed_field.dump());
        require(probe_wet() == TerrainLandscapeAvailability::Invalid,
                "absent world masked changed source manifest");
        write(site_root / "heightfield.json", field.dump());
        std::filesystem::rename(site_root / "world.retained", site_root / "world-recipe.json");
        write(site_root / "depth.f32", "bad-size");
        require(probe_terrain_landscape(wet, root / "cache").availability ==
                    TerrainLandscapeAvailability::Invalid,
                "wrong payload size accepted");
        std::filesystem::create_directory_symlink(site_root, root / "partial/site/link");
        auto escaped = valid.presets[0];
        escaped.site.path = "site/link/site.json";
        require(probe_terrain_landscape(escaped, root / "partial").availability ==
                    TerrainLandscapeAvailability::Invalid,
                "escaping symlink accepted");
        rejects([&] { (void)terrain_landscape_preset(valid, "unknown"); });
        std::cout << "terrain_landscape_catalog: PASS catalog, pins, dry/wet, "
                     "missing/partial/invalid, paths and symlinks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
