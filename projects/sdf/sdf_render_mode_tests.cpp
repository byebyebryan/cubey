#include "sdf_render_modes.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>
using namespace cubey::projects::sdf;
namespace {
void require(bool condition) {
    if (!condition)
        throw std::runtime_error("render-mode contract failed");
}
SdfConfig parse(std::vector<std::string> args) {
    args.insert(args.begin(), "sdf");
    std::vector<char*> argv;
    for (auto& arg : args)
        argv.push_back(arg.data());
    return parse_sdf_config(static_cast<int>(argv.size()), argv.data());
}
template <typename F> void rejects(F&& run) {
    try {
        run();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("incompatible rendering request accepted");
}
} // namespace
int main() {
    auto c = parse({});
    require(c.named_render_mode && c.render_mode == "auto");
    resolve_render_mode(c, false);
    require(c.effective_render_mode == "cached");
    for (const auto mode : render_modes) {
        auto selected = parse({"--render-mode", std::string(mode)});
        resolve_render_mode(selected, true);
        require(selected.named_render_mode);
        if (mode == "balanced")
            require(selected.cached_quality == "refine-normal" && selected.region_axis == 64);
        if (mode == "cached-refined")
            require(selected.cached_quality == "refine-0.03125");
        if (mode == "analytic" || mode == "hybrid")
            require(selected.surface_trace == mode && selected.csg_evaluation == "pruned");
    }
    c = parse({"--surface-trace", "sampled"});
    resolve_render_mode(c, true);
    require(!c.named_render_mode && c.cached_quality == "ordinary");
    c = parse({"--surface-trace", "analytic", "--region-axis", "64", "--acceleration-variant",
               "root-far"});
    resolve_render_mode(c, true);
    require(!c.named_render_mode && c.region_axis == 64 && c.acceleration_variant == "root-far");
    c = parse({"--render-mode", "analytic", "--surface-trace", "analytic"});
    resolve_render_mode(c, true);
    require(c.effective_render_mode == "analytic");
    rejects([] {
        auto x = parse({"--render-mode", "balanced", "--surface-trace", "analytic"});
        resolve_render_mode(x, true);
    });
    rejects([] {
        auto x = parse({"--render-mode", "balanced", "--region-axis", "16"});
        resolve_render_mode(x, true);
    });
    rejects([] {
        auto x = parse({"--render-mode", "balanced"});
        resolve_render_mode(x, false);
    });
    rejects([] {
        auto x = parse({"--render-mode", "analytic", "--debug", "4"});
        resolve_render_mode(x, true);
    });
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("cubey-sdf-modes-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto file = directory / "config.json";
    auto initial = SdfConfig{};
    auto schema = sdf_config_schema(initial);
    const auto document = schema.template_json();
    require(document.at("surface-trace").is_null() && document.at("region-axis").is_null());
    schema.write_template(file);
    c = parse({"--config", file.string()});
    resolve_render_mode(c, true);
    require(c.named_render_mode);
    {
        std::ofstream output(file);
        output << R"({"render-mode":"cached","supersample":2})";
    }
    c = parse({"--config", file.string(), "--render-mode", "balanced", "--set", "supersample=1"});
    resolve_render_mode(c, true);
    require(c.effective_render_mode == "balanced" && c.supersample == 1);
    {
        std::ofstream output(file);
        output << R"({"surface-trace":"sampled","csg-evaluation":"full","region-axis":16})";
    }
    c = parse({"--config", file.string()});
    resolve_render_mode(c, true);
    require(!c.named_render_mode && c.surface_trace == "sampled");
    rejects([&] {
        auto x = parse({"--config", file.string(), "--render-mode", "balanced"});
        resolve_render_mode(x, true);
    });
    std::filesystem::remove_all(directory);
    std::cout << "named modes, explicit legacy controls, conflicts, recipes, debug and "
                 "template/layering contracts passed\n";
}
