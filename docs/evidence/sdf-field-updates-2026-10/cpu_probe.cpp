// Research-only timing harness. Links the existing, unmodified scene/history code.
#include "sdf_authored.h"
#include "sdf_history.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

using namespace cubey::projects::sdf;
using Json = nlohmann::json;
std::uint64_t checksum = 0;

template <typename F> Json measure(F function, int count = 31) {
    for (int i = 0; i < 7; ++i) function();
    std::vector<double> samples;
    for (int i = 0; i < count; ++i) {
        const auto start = std::chrono::steady_clock::now();
        function();
        samples.push_back(std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count());
    }
    auto sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    return {{"samples_ms", samples}, {"median_ms", sorted[sorted.size() / 2]},
            {"p95_ms", sorted[static_cast<std::size_t>(std::ceil(.95 * static_cast<double>(sorted.size()))) - 1]},
            {"min_ms", sorted.front()}, {"max_ms", sorted.back()}};
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto path = std::filesystem::path(argv[1]);
    const auto scene = load_authored_scene(path);
    Json result{{"scene", path.string()}, {"leaves", scene.leaf_count},
                {"instructions", scene.program.size()}, {"history_count", scene.history.size()}};
    result["load_warm_fs"] = measure([&] {
        const auto loaded = load_authored_scene(path);
        checksum ^= loaded.program.size();
    });
    result["geometry_hash"] = measure([&] { checksum ^= authored_geometry_hash(scene); }, 101);
    result["appearance_hash"] = measure([&] { checksum ^= authored_appearance_hash(scene); }, 101);
    std::vector<std::size_t> prefixes{0, 1, scene.history.size() / 4,
        scene.history.size() / 2, scene.history.size() * 3 / 4,
        scene.history.size() - 1, scene.history.size()};
    for (const auto prefix : prefixes) {
        const auto state = authored_history_scene(scene, prefix);
        auto timing = measure([&] {
            const auto state = authored_history_scene(scene, prefix);
            checksum ^= state.program.size();
        });
        timing["prefix"] = prefix;
        timing["leaves"] = state.leaf_count;
        timing["instructions"] = state.program.size();
        result["history_prefix"].push_back(timing);
    }
    std::vector<std::size_t> stack;
    std::size_t max_stack = 0;
    result["shape_code_counts"] = Json::object();
    for (const auto& node : scene.program) {
        const auto code = static_cast<unsigned>(node.center_code.w);
        const auto name = std::to_string(code);
        result["shape_code_counts"][name] = result["shape_code_counts"].value(name, 0) + 1;
        if (code < 10) stack.push_back(1);
        else { stack.pop_back(); }
        max_stack = std::max(max_stack, stack.size());
    }
    result["max_postfix_stack"] = max_stack;
    result["checksum"] = checksum;
    std::cout << result.dump(2) << '\n';
}
