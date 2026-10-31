#include "../sdf_cached_commit.h"
#include <chrono>
#include <cubey/asset/file_digest.h>
#include <cubey/engine/project_runtime.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/render/pass.h>
#include <cubey/vulkan/command_recorder.h>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using namespace cubey;
using namespace cubey::projects::sdf;
namespace {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}
void save(const std::filesystem::path& path, const Json& value) {
    std::ofstream f(path);
    f << value.dump(2) << '\n';
    if (!f)
        throw std::runtime_error("cannot save commit receipt");
}
template <class T> std::string digest(const std::vector<T>& v) {
    return asset::sha256_hex(std::as_bytes(std::span(v)));
}
template <class T> void binary(const std::filesystem::path& p, const std::vector<T>& v) {
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(v.data()),
            static_cast<std::streamsize>(v.size() * sizeof(T)));
    if (!f)
        throw std::runtime_error("cannot save raw cache snapshot");
}
Json describe(const CommitSnapshot& s, const std::filesystem::path& prefix, bool dump) {
    Json result{{"topology_entries", s.topology.size()},
                {"bricks", s.cells.size()},
                {"texels", s.samples.size()},
                {"topology_sha256", digest(s.topology)},
                {"cells_sha256", digest(s.cells)},
                {"raw_r16_sha256", digest(s.samples)}};
    result["physical_slots_sha256"] = digest(s.slots);
    if (dump) {
        for (const auto& suffix : {".topology.bin", ".cells.bin", ".r16.bin"})
            result["files"].push_back(prefix.string() + suffix);
        binary(prefix.string() + ".topology.bin", s.topology);
        binary(prefix.string() + ".cells.bin", s.cells);
        binary(prefix.string() + ".r16.bin", s.samples);
        result["slots_file"] = prefix.string() + ".slots.bin";
        binary(prefix.string() + ".slots.bin", s.slots);
    }
    return result;
}
Json compare(const CommitSnapshot& a, const CommitSnapshot& b) {
    const bool topology = a.topology == b.topology, cells = a.cells == b.cells;
    std::uint64_t differences = 0, zero_differences = 0;
    Json examples = Json::array();
    if (cells && a.samples.size() == b.samples.size()) {
        for (std::size_t i = 0; i < a.samples.size(); ++i)
            if (a.samples[i] != b.samples[i]) {
                ++differences;
                if ((a.samples[i] & 0x7fffU) == 0 && (b.samples[i] & 0x7fffU) == 0)
                    ++zero_differences;
                if (examples.size() < 32)
                    examples.push_back({{"morton", a.cells[i / 512]},
                                        {"sample", i % 512},
                                        {"candidate_bits", a.samples[i]},
                                        {"canonical_bits", b.samples[i]}});
            }
    } else
        differences = std::max(a.samples.size(), b.samples.size());
    return {{"passed", topology && cells && differences == 0},
            {"topology_equal", topology},
            {"logical_leaves_equal", cells},
            {"r16_differences", differences},
            {"signed_zero_differences", zero_differences},
            {"examples", examples}};
}
} // namespace
int main(int argc, char** argv) {
    try {
        std::filesystem::path input_path, output_path;
        std::string mode_name = "reuse";
        unsigned trials = 1, axis = 4, warmup = 0, fallback_limit = 2U * 1024U * 1024U;
        bool qualify = false, cold = false, chain = false, dump = false, validate = false,
             copy_only = false;
        unsigned copy_slab = 0;
        for (int i = 1; i < argc; ++i) {
            const std::string_view flag = argv[i];
            if (flag == "--commit-qualify")
                qualify = true;
            else if (flag == "--commit-validate-only")
                validate = true;
            else if (flag == "--commit-cold")
                cold = true;
            else if (flag == "--commit-chain")
                chain = true;
            else if (flag == "--commit-dump")
                dump = true;
            else if (flag == "--commit-copy-only")
                copy_only = true;
            else {
                if (++i >= argc)
                    throw std::invalid_argument("missing commit argument");
                if (flag == "--commit-input")
                    input_path = argv[i];
                else if (flag == "--commit-output")
                    output_path = argv[i];
                else if (flag == "--commit-mode")
                    mode_name = argv[i];
                else if (flag == "--commit-trials")
                    trials = static_cast<unsigned>(std::stoul(argv[i]));
                else if (flag == "--commit-regions")
                    axis = static_cast<unsigned>(std::stoul(argv[i]));
                else if (flag == "--commit-warmup")
                    warmup = static_cast<unsigned>(std::stoul(argv[i]));
                else if (flag == "--commit-fallback-limit")
                    fallback_limit = static_cast<unsigned>(std::stoul(argv[i]));
                else if (flag == "--commit-copy-slab-mib")
                    copy_slab = static_cast<unsigned>(std::stoul(argv[i])) * 1024U * 1024U;
                else
                    throw std::invalid_argument("unknown commit argument");
            }
        }
        if (input_path.empty() || output_path.empty() || trials < 1 || trials > 100 ||
            warmup > 10 || (qualify && (trials != 1 || warmup)) || (chain && (!qualify || cold)) ||
            (dump && !qualify) || (axis != 4 && axis != 16))
            throw std::invalid_argument("invalid commit study configuration");
        const auto mode = mode_name == "reuse"       ? CommitBuilder::Reuse
                          : mode_name == "full"      ? CommitBuilder::Full
                          : mode_name == "local"     ? CommitBuilder::Local
                          : mode_name == "local-dom" ? CommitBuilder::LocalDominance
                                                     : CommitBuilder::Pruned;
        if (mode_name != "reuse" && mode_name != "full" && mode_name != "pruned" &&
            mode_name != "local" && mode_name != "local-dom")
            throw std::invalid_argument("invalid commit builder mode");
        const auto input = Json::parse(std::ifstream(input_path));
        if (input.at("format") != "cubey.sdf.cached-edit-study" || input.at("version") != 1 ||
            input.at("cases").empty() || input.at("cases").size() > 128)
            throw std::invalid_argument("invalid commit manifest");
        std::vector<CachedEditProgram> programs;
        std::vector<double> admission_ms;
        for (const auto& c : input.at("cases")) {
            const auto t = Clock::now();
            programs.push_back(prepare_cached_edit(parse_authored_scene(c.at("base").dump()),
                                                   parse_authored_scene(c.at("scene").dump())));
            admission_ms.push_back(elapsed(t));
        }
        if (validate) {
            Json ids = Json::array();
            for (const auto& p : programs)
                ids.push_back({{"base_geometry_hash", authored_geometry_hash(p.base)},
                               {"edited_geometry_hash", authored_geometry_hash(p.edited)},
                               {"base_leaves", p.base.leaf_count},
                               {"edited_leaves", p.edited.leaf_count}});
            save(output_path, {{"validated_without_gpu", true}, {"programs", ids}});
            return 0;
        }
        host::CommonRunConfig config;
        config.headless = config.headless_render_only = config.require_validation = true;
        config.width = config.height = 16;
        config.frames = 1;
        Json receipt{{"format", "cubey.sdf.cached-commit-results"},
                     {"version", 1},
                     {"mode", mode_name},
                     {"region_axis", axis},
                     {"cold", cold},
                     {"qualified", qualify},
                     {"chain", chain},
                     {"warmup", warmup},
                     {"fallback_limit", fallback_limit},
                     {"copy_only", copy_only},
                     {"copy_slab_bytes", copy_slab},
                     {"scope", "Private serialized builder; qualification readbacks/encoding "
                               "excluded from commit timing"},
                     {"runs", Json::array()}};
        ProjectRuntimeAdapter runtime;
        host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [&](host::HeadlessPngContext& ctx) {
            runtime.attach_gpu_if_needed(ctx.gpu());
            CachedCommitStudy builder(ctx.device(), runtime.gpu(), fallback_limit);
            receipt["pipeline_prepare_ms"] = builder.prepare(mode);
            auto base = std::make_unique<Field>(ctx.device(), runtime.gpu());
            std::unique_ptr<Field> work;
            if (!cold)
                work = std::make_unique<Field>(ctx.device(), runtime.gpu());
            bool all_passed = true;
            for (std::size_t n = 0; n < programs.size(); ++n) {
                const auto& p = programs[n];
                if (!chain || n == 0)
                    base->build(p.base);
                Json row{{"name", input.at("cases").at(n).at("name")},
                         {"source_admission_cpu_ms", admission_ms[n]},
                         {"base_geometry_hash", authored_geometry_hash(p.base)},
                         {"edited_geometry_hash", authored_geometry_hash(p.edited)},
                         {"retained_history_steps", p.edited.history.size()},
                         {"base_from_previous_candidate", chain && n > 0},
                         {"trials", Json::array()}};
                std::optional<CommitSnapshot> old_snapshot;
                if (qualify &&
                    (mode == CommitBuilder::Local || mode == CommitBuilder::LocalDominance))
                    old_snapshot = CachedCommitStudy::snapshot(ctx.device(), runtime.gpu(), *base);
                for (unsigned trial = 0; trial < trials + warmup; ++trial) {
                    const auto t = Clock::now();
                    if (cold)
                        work = std::make_unique<Field>(ctx.device(), runtime.gpu());
                    const auto allocation = elapsed(t);
                    const auto stats = [&] {
                        if (!copy_only)
                            return builder.build(*work, *base, p, mode, axis);
                        CommitBuildStats result;
                        result.copy = builder.clone(*work, *base, copy_slab, true);
                        result.wall_ms = result.copy.wall_ms;
                        return result;
                    }();
                    const auto ready = elapsed(t);
                    const auto& f = work->stats();
                    row["trials"].push_back({{"trial", trial},
                                             {"warmup", trial < warmup},
                                             {"allocation_ms", cold ? allocation : 0.},
                                             {"admission_ms", stats.admission_ms},
                                             {"reuse_gpu_ms", stats.reuse_gpu_ms},
                                             {"fallback_gpu_ms", stats.fallback_gpu_ms},
                                             {"full_fallback", stats.full_fallback},
                                             {"copy_wall_ms", stats.copy.wall_ms},
                                             {"copy_gpu_ms", stats.copy.gpu_ms},
                                             {"copy_max_chunk_gpu_ms", stats.copy.max_chunk_gpu_ms},
                                             {"copy_chunks", stats.copy.chunks},
                                             {"copy_bytes", stats.copy.bytes},
                                             {"locality_bound", stats.locality_bound},
                                             {"locality_guard", stats.locality_guard},
                                             {"local_work", stats.local_work},
                                             {"pipeline_prepare_ms", stats.pipeline_prepare_ms},
                                             {"region_prepare_ms", stats.region_prepare_ms},
                                             {"region_bytes", stats.region_bytes},
                                             {"region_scratch_bytes", stats.region_scratch_bytes},
                                             {"region_references", stats.region_references},
                                             {"builder_wall_ms", stats.wall_ms},
                                             {"field_wall_ms", f.build_ms},
                                             {"filter_gpu_ms", f.filter_gpu_ms},
                                             {"brick_gpu_ms", f.brick_gpu_ms},
                                             {"ready_to_render_ms", ready},
                                             {"bricks", f.brick_count},
                                             {"work_counts", stats.work},
                                             {"geometry_payload_bytes",
                                              base->stats().allocation_bytes + f.allocation_bytes +
                                                  stats.region_bytes + builder.working_bytes()}});
                    if (cold && trial + 1 < trials + warmup)
                        work.reset();
                }
                if (qualify) {
                    auto candidate =
                        CachedCommitStudy::snapshot(ctx.device(), runtime.gpu(), *work);
                    if (old_snapshot) {
                        std::size_t a = 0, b = 0, retained = 0, moved = 0;
                        while (a < old_snapshot->cells.size() && b < candidate.cells.size()) {
                            if (old_snapshot->cells[a] < candidate.cells[b])
                                ++a;
                            else if (candidate.cells[b] < old_snapshot->cells[a])
                                ++b;
                            else {
                                ++retained;
                                moved += old_snapshot->slots[a] != candidate.slots[b];
                                ++a;
                                ++b;
                            }
                        }
                        row["retained_logical_bricks"] = retained;
                        row["retained_slots_moved"] = moved;
                        if (moved)
                            throw std::runtime_error("local candidate moved a retained slot");
                    }
                    if (!copy_only)
                        base->build(p.edited);
                    auto canonical =
                        CachedCommitStudy::snapshot(ctx.device(), runtime.gpu(), *base);
                    row["comparison"] = compare(candidate, canonical);
                    auto prefix = output_path;
                    prefix.replace_extension("case-" + std::to_string(n));
                    row["candidate"] = describe(candidate, prefix.string() + "-candidate", dump);
                    row["canonical"] = describe(canonical, prefix.string() + "-canonical", dump);
                    all_passed = all_passed && row["comparison"].at("passed").get<bool>();
                }
                receipt["runs"].push_back(row);
                save(output_path, receipt);
                if (chain)
                    std::swap(base, work);
                else if (cold)
                    work.reset();
            }
            receipt["all_comparisons_passed"] = all_passed;
            receipt["snapshot_staging_peak_bytes"] = qualify ? base->nodes().size() : 0;
            receipt["complete"] = true;
            save(output_path, receipt);
        };
        callbacks.record_frame = [](host::HeadlessPngContext&, const host::HeadlessCaptureFrame&,
                                    VkCommandBuffer command,
                                    const host::HeadlessRenderTarget& target) {
            render::record_render_target_pass(vulkan::CommandRecorder(command),
                                              render::render_target_view(target),
                                              {.color = render::color_clear_value(0, 0, 0, 1)},
                                              [](const vulkan::CommandRecorder&) {});
        };
        callbacks.shutdown = [&](host::HeadlessPngContext&) { runtime.detach_gpu_if_attached(); };
        const int status = host::HeadlessPngHost({.run_config = config,
                                                  .required_queue_flags =
                                                      VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT},
                                                 std::move(callbacks))
                               .run();
        if (status == 0 && qualify && !receipt.at("all_comparisons_passed").get<bool>())
            return 2;
        return status;
    } catch (const std::exception& e) {
        std::cerr << "cached commit study: " << e.what() << '\n';
        return 1;
    }
}
