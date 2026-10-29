// Bounded CPU investigation. Does not alter production region construction.
#include "../sdf_regions.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <numeric>
#include <random>

using namespace cubey;
using namespace cubey::projects::sdf;
using Json = nlohmann::json;
namespace {
constexpr std::uint32_t neg = 0x80000000U;
float combine(float a, float b, const SceneInstruction& n) {
    const auto code = static_cast<unsigned>(n.center_code.w);
    if (code == 10)
        return std::min(a, b);
    if (code == 11)
        return std::max(a, -b);
    const float k = n.settings.y;
    if (code == 13) {
        const float h = std::clamp(.5F + .5F * (a + b) / k, 0.F, 1.F);
        return std::lerp(-b, a, h) + k * h * (1 - h);
    }
    const float h = std::clamp(.5F + .5F * (b - a) / k, 0.F, 1.F);
    return std::lerp(b, a, h) - k * h * (1 - h);
}
float clip(math::Vec3 p) {
    const auto q = glm::abs(p) - math::Vec3{.499F};
    return glm::length(glm::max(q, math::Vec3{0})) + std::min(std::max({q.x, q.y, q.z}), 0.F);
}
float eval(const AuthoredScene& s, std::span<const std::uint32_t> refs, math::Vec3 p) {
    std::array<float, 32> stack{};
    unsigned size = 0;
    for (auto ref : refs) {
        const auto& n = s.program.at(ref & ~neg);
        const float b = n.center_code.w >= 10 ? stack.at(--size) : 0;
        const float a = n.center_code.w >= 10 ? stack.at(--size) : 0;
        const float d = n.center_code.w < 10 ? authored_primitive_distance(p, n) : combine(a, b, n);
        stack.at(size++) = (ref & neg) ? -d : d;
    }
    if (size != 1)
        throw std::runtime_error("pilot stack");
    return std::max(stack[0], clip(p));
}
struct Entry {
    float value;
    int a = -1, b = -1, choice = -1;
    std::uint32_t ref;
};
struct Child {
    std::vector<std::uint32_t> refs;
    float certificate = 0;
};
Child prune(const AuthoredScene& s, std::span<const std::uint32_t> refs, math::Vec3 p,
            float guard) {
    constexpr float radius = .5F * 1.7320508075688772F / 256.F;
    std::vector<Entry> entries;
    entries.reserve(refs.size());
    std::array<int, 32> stack{};
    unsigned size = 0;
    for (auto ref : refs) {
        const auto& n = s.program.at(ref & ~neg);
        Entry e{};
        e.ref = ref;
        if (n.center_code.w < 10)
            e.value = authored_primitive_distance(p, n);
        else {
            e.b = stack.at(--size);
            e.a = stack.at(--size);
            const float a = entries.at(static_cast<std::size_t>(e.a)).value;
            const float b = entries.at(static_cast<std::size_t>(e.b)).value;
            const bool sub = n.center_code.w == 11 || n.center_code.w == 13;
            const float cb = sub ? -b : b, k = n.center_code.w >= 12 ? n.settings.y : 0;
            if (std::abs(a - cb) > 2 * radius + k + guard)
                e.choice = (sub ? a > cb : a < cb) ? 0 : 1;
            e.value = combine(a, b, n);
        }
        if (ref & neg)
            e.value = -e.value;
        stack.at(size++) = static_cast<int>(entries.size());
        entries.push_back(e);
    }
    Child child;
    const float d = std::max(entries.back().value, clip(p));
    if (std::abs(d) - guard > 2 * radius) {
        child.certificate = std::nextafter(std::abs(d) - guard - radius, 0.F);
        return child;
    }
    const auto emit = [&](auto&& self, int id, bool flip) -> void {
        const auto& e = entries.at(static_cast<std::size_t>(id));
        const auto code = s.program.at(e.ref & ~neg).center_code.w;
        if (e.choice >= 0) {
            const bool sub = code == 11 || code == 13;
            self(self, e.choice == 0 ? e.a : e.b,
                 flip ^ bool(e.ref & neg) ^ (e.choice == 1 && sub));
        } else {
            if (e.a >= 0) {
                self(self, e.a, false);
                self(self, e.b, false);
            }
            child.refs.push_back(e.ref ^ (flip ? neg : 0U));
        }
    };
    emit(emit, stack[0], false);
    return child;
}
double elapsed(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc != 3)
            throw std::invalid_argument("scene.json receipt.json");
        const auto scene = load_authored_scene(argv[1]);
        const auto base = build_region_programs(scene, 64, region_reference_limit, true);
        std::map<unsigned, std::vector<unsigned>> strata;
        std::size_t active = 0;
        for (unsigned i = 0; i < base.headers.size(); i++) {
            const auto h = base.headers[i];
            if (h.kind != 0 || !h.count)
                continue;
            unsigned bin = 0;
            for (unsigned edge : {1U, 4U, 8U, 16U, 32U, 64U, 128U})
                bin += h.count > edge;
            const unsigned x = i % 64, y = (i / 64) % 64, z = i / 4096;
            const unsigned oct = (x >= 32 ? 1U : 0U) + (y >= 32 ? 2U : 0U) + (z >= 32 ? 4U : 0U);
            strata[oct * 8 + bin].push_back(i);
            active++;
        }
        std::mt19937 rng(20261029);
        for (auto& [key, ids] : strata) {
            static_cast<void>(key);
            std::shuffle(ids.begin(), ids.end(), rng);
        }
        std::map<unsigned, unsigned> sample_counts;
        std::size_t chosen = 0, target = std::min<std::size_t>(256, active);
        while (chosen < target)
            for (const auto& [key, ids] : strata)
                if (chosen < target && sample_counts[key] < ids.size()) {
                    sample_counts[key]++;
                    chosen++;
                }
        Json rows = Json::array();
        double weighted_refs = 0, weighted_far = 0, weighted_build_ms = 0;
        std::uint64_t children = 0, refs_total = 0, far_total = 0, points = 0, failures = 0;
        double max_error = 0, total_prune_ms = 0, total_validation_ms = 0;
        std::uint64_t scratch_peak = 0;
        unsigned smooth_parents = 0;
        for (const auto& [key, ids] : strata) {
            const unsigned samples = sample_counts[key];
            std::uint64_t sr = 0, sf = 0;
            double st = 0;
            for (unsigned sample = 0; sample < samples; sample++) {
                const auto index = ids[sample];
                const auto h = base.headers[index];
                const auto refs =
                    std::span<const std::uint32_t>(base.references).subspan(h.offset, h.count);
                smooth_parents += std::any_of(refs.begin(), refs.end(), [&](auto ref) {
                    return scene.program[ref & ~neg].center_code.w >= 12;
                });
                const math::Vec3 origin{index % 64, (index / 64) % 64, index / 4096};
                Json cr = Json::array();
                for (unsigned child = 0; child < 64; child++) {
                    const math::Vec3 offset{child % 4, (child / 4) % 4, child / 16};
                    const auto p = (origin * 4.F + offset + .5F) / 256.F - .5F;
                    const auto start = std::chrono::steady_clock::now();
                    const auto c = prune(scene, refs, p, base.arithmetic_guard);
                    const double ms = elapsed(start);
                    st += ms;
                    total_prune_ms += ms;
                    scratch_peak = std::max(
                        scratch_peak, static_cast<std::uint64_t>(refs.size() * sizeof(Entry) +
                                                                 c.refs.capacity() * 4 +
                                                                 sizeof(std::array<int, 32>)));
                    sr += c.refs.size();
                    sf += c.certificate > 0;
                    children++;
                    cr.push_back(c.refs.size());
                    const auto validation_start = std::chrono::steady_clock::now();
                    // Center, corners and the normal stencil; independent full-source values.
                    std::vector<math::Vec3> probes{p};
                    for (unsigned corner = 0; corner < 8; corner++)
                        probes.push_back(
                            p + (math::Vec3{corner & 1U, (corner >> 1U) & 1U, (corner >> 2U) & 1U} -
                                 .5F) *
                                    (1.F / 256.F));
                    for (int axis = 0; axis < 3; axis++)
                        for (float sign : {-1.F, 1.F}) {
                            auto q = p;
                            q[axis] += sign / (256.F * 7.F * 16.F);
                            probes.push_back(q);
                        }
                    for (const auto q : probes) {
                        const float exact = authored_distance(q, scene);
                        const float delta = c.certificate > 0
                                                ? std::max(c.certificate - std::abs(exact), 0.F)
                                                : std::abs(eval(scene, c.refs, q) - exact);
                        max_error = std::max(max_error, static_cast<double>(delta));
                        points++;
                        if (delta > 2e-6F)
                            failures++;
                    }
                    total_validation_ms += elapsed(validation_start);
                }
                rows.push_back({{"stratum", key},
                                {"parent", index},
                                {"parent_references", h.count},
                                {"child_counts", cr}});
            }
            const double weight = static_cast<double>(ids.size()) / samples;
            weighted_refs += static_cast<double>(sr) * weight;
            weighted_far += static_cast<double>(sf) * weight;
            weighted_build_ms += st * weight;
            refs_total += sr;
            far_total += sf;
        }
        Json weights = Json::array();
        for (const auto& [key, ids] : strata)
            weights.push_back(
                {{"stratum", key}, {"population", ids.size()}, {"samples", sample_counts[key]}});
        const auto base_bytes = 48 + base.headers.size() * 16 + base.references.size() * 4;
        Json receipt = {
            {"scope", "Bounded CPU pilot, not a constructed GPU level or speedup"},
            {"source", argv[1]},
            {"program_instructions", scene.program.size()},
            {"authored_error_guard", authored_evaluation_guard(scene)},
            {"region_guard", base.arithmetic_guard},
            {"epsilon", 1.F / (256.F * 7.F * 32.F)},
            {"base_build_ms_descriptive", base.build_ms},
            {"active_parents", active},
            {"sampled_parents", chosen},
            {"sampled_smooth_parents", smooth_parents},
            {"children", children},
            {"references", refs_total},
            {"far_children", far_total},
            {"child_prune_ms_descriptive", total_prune_ms},
            {"validation_ms", total_validation_ms},
            {"child_scratch_payload_peak_bytes", scratch_peak},
            {"independent_points", points},
            {"failures", failures},
            {"max_error_or_bound_excess", max_error},
            {"weighted_reference_forecast", weighted_refs},
            {"weighted_far_child_forecast", weighted_far},
            {"weighted_child_build_ms_forecast", weighted_build_ms},
            {"weighted_total_region_bytes_forecast",
             base_bytes + active * 64 * 16 + weighted_refs * 4},
            {"forecast_limits",
             "Stratified point estimates; no confidence interval, GPU lookup cost, allocator "
             "capacities or full publication measurement. Do not use as acceptance."},
            {"strata", weights},
            {"parents", rows},
            {"complete", failures == 0}};
        std::ofstream output(argv[2]);
        output << receipt.dump(2) << '\n';
        if (!output || failures)
            throw std::runtime_error("pilot validation failed");
        std::cout << receipt["sampled_parents"]
                  << " parents; guard=" << receipt["authored_error_guard"]
                  << "; child build forecast=" << weighted_build_ms
                  << " ms; region forecast=" << receipt["weighted_total_region_bytes_forecast"]
                  << " bytes\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
