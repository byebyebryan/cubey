#include <chrono>
#include <cubey/asset/terrain_site.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
void write(const std::filesystem::path& path, const Json& doc) {
    std::ofstream stream(path);
    stream << doc.dump();
}
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("invalid site was accepted");
}
Json bounds(float radius) {
    return {{"minimum", {-radius, -radius}}, {"maximum", {radius, radius}}};
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2) {
            const auto site = cubey::asset::load_terrain_site(argv[1]);
            std::cout << "terrain_site: PASS " << site.id << '\n';
            return 0;
        }
        const auto root =
            std::filesystem::temp_directory_path() /
            ("cubey-site-tests-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(root);
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::filesystem::remove_all(path);
            }
        } cleanup{root};
        const std::string id(64, '1');
        const Json producer{{"generator", "terrain-diffusion"}, {"id", "test"}};
        const Json height{{"offset_m", 0}, {"scale", 1}, {"relief_scale_m", 3500}};
        Json world{{"schema", "cubey.terrain.world-recipe.v1"},
                   {"world_id", id},
                   {"seed", 0},
                   {"producer", producer},
                   {"height_transform", height},
                   {"survey_bounds", bounds(131072)},
                   {"coordinate_mapping",
                    {{"native_spacing_m", 30},
                     {"world_origin_xz_m", {0, 0}},
                     {"model_origin_ij", {0, 0}}}}};
        Json field{{"schema", "cubey.terrain.heightfield.v1"},
                   {"seed", 0},
                   {"source", producer},
                   {"height", height},
                   {"provenance",
                    {{"world_id", id}, {"model_native_origin", {{"i", -2216}, {"j", -2216}}}}},
                   {"grid",
                    {{"width", 4432},
                     {"height", 4432},
                     {"sample_spacing_m", 30},
                     {"sample_origin_x_m", -66480},
                     {"sample_origin_z_m", -66480},
                     {"axis_mapping", {{"world_x", "model_j"}, {"world_z", "model_i"}}}}}};
        Json levels = Json::array();
        for (float span : {16384.0F, 32768.0F, 65536.0F, 131072.0F})
            levels.push_back({{"span_m", span}, {"bounds", bounds(span * .5F)}});
        Json plan{{"schema", "cubey.terrain.site-coverage.v1"},
                  {"site_id", "test"},
                  {"world_id", id},
                  {"center_xz_m", {0, 0}},
                  {"levels", levels},
                  {"camera_margin_m", 1000},
                  {"sampling_margin_m", 480},
                  {"admitted", true},
                  {"rejection_reason", nullptr},
                  {"heightfield", "heightfield.json"},
                  {"surface_fields", "surface-fields.json"}};
        write(root / "world-recipe.json", world);
        write(root / "heightfield.json", field);
        write(root / "surface-fields.json", Json::object());
        const auto load = [&] { return cubey::asset::load_terrain_site(root / "site.json"); };
        write(root / "site.json", plan);
        require(load().level_spans_m[3] == 131072, "valid site coverage missing");
        auto bad = plan;
        bad["center_xz_m"] = {100000, 0};
        write(root / "site.json", bad);
        rejects(load);
        bad = plan;
        bad["levels"][2]["span_m"] = 16;
        write(root / "site.json", bad);
        rejects(load);
        bad = plan;
        bad["heightfield"] = "../outside.json";
        write(root / "site.json", bad);
        rejects(load);
        bad = plan;
        bad["world_id"] = std::string(64, '2');
        write(root / "site.json", bad);
        rejects(load);
        bad = plan;
        bad["sampling_margin_m"] = -1;
        write(root / "site.json", bad);
        rejects(load);
        write(root / "site.json", plan);
        auto wrong = field;
        wrong["grid"]["sample_origin_x_m"] = -66479;
        write(root / "heightfield.json", wrong);
        rejects(load);
        wrong = field;
        wrong["seed"] = 1;
        write(root / "heightfield.json", wrong);
        rejects(load);
        wrong = field;
        wrong["grid"]["width"] = 10;
        write(root / "heightfield.json", wrong);
        rejects(load);
        std::cout << "terrain_site: PASS admission, coordinates, producer, paths, coverage\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
