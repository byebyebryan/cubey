#include "../sdf_renderer.h"
#include "stroke_service.h"
#include "stroke_service_gate.h"
#include "stroke_stream_sampling.h"
#include <atomic>
#include <cubey/engine/project_runtime.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/immediate_commands.h>
#include <deque>
#include <fstream>
#include <future>
#include <map>
#include <thread>

namespace cubey::projects::sdf {
namespace {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
constexpr double tolerance = 1.0 / (256 * 7 * 4);
double ms(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}
Json primitive(const StreamSegment& segment, const std::string& name, const std::string& material) {
    const auto& a = segment.first;
    const auto& b = segment.last;
    std::array<double, 3> delta{}, center{};
    for (unsigned k = 0; k < 3; ++k) {
        delta[k] = b.point[k] - a.point[k];
        center[k] = (a.point[k] + b.point[k]) / 2;
    }
    const double length = std::hypot(delta[0], delta[1], delta[2]);
    if (length <= std::abs(a.radius - b.radius)) {
        const auto& p = a.radius >= b.radius ? a : b;
        return {{"name", name},
                {"type", "sphere"},
                {"position", p.point},
                {"radius", p.radius},
                {"material", material}};
    }
    if (length < .000002)
        throw std::invalid_argument("stream segment below exact shape admission");
    std::array<double, 4> q{delta[2] / length, 0, -delta[0] / length, 1 + delta[1] / length};
    const double norm = std::sqrt(q[0] * q[0] + q[2] * q[2] + q[3] * q[3]);
    if (norm < 1e-8)
        q = {1, 0, 0, 0};
    else
        for (auto& x : q)
            x /= norm;
    Json shape{{"name", name},           {"type", "round-cone"},      {"position", center},
               {"rotation_xyzw", q},     {"half_length", length / 2}, {"bottom_radius", a.radius},
               {"top_radius", b.radius}, {"material", material}};
    if (a.radius == b.radius) {
        shape["type"] = "capsule";
        shape["radius"] = a.radius;
        shape.erase("bottom_radius");
        shape.erase("top_radius");
    }
    return shape;
}
AuthoredScene append(const AuthoredScene& base, const std::vector<Json>& shapes, bool cut,
                     const std::string& material) {
    auto doc = Json::parse(serialize_authored_scene(base));
    doc.erase("geometry_hash");
    doc.erase("appearance_hash");
    for (const auto& shape : shapes) {
        const auto name = shape.at("name").get<std::string>();
        Json root{{"name", "group-" + name},
                  {"type", cut ? "subtract" : "union"},
                  {"children", {doc.at("root"), shape}}};
        if (cut)
            root["cut_material"] = material;
        if (doc.at("root").is_null()) {
            if (cut)
                throw std::invalid_argument("stream cut needs a body");
            root = shape;
        }
        doc["root"] = std::move(root);
        if (doc.contains("history"))
            doc["history"].push_back({{"node", name}, {"label", name}, {"seconds", .2}});
    }
    return parse_authored_scene(doc.dump());
}
struct Accepted {
    std::uint64_t id;
    Json shape;
    SceneInstruction instruction;
    double source_ms, admitted_ms;
};
struct JobResult {
    PreparedCachedStroke prepared;
    CommitBuildStats stats;
    double preparation_ms;
};
} // namespace

int run_cached_stroke_stream(SdfConfig config, const Json& input,
                             const CachedStrokeProgram& initial,
                             const std::filesystem::path& output) {
    const auto& spec = input.at("stream");
    const bool cut = spec.at("subtract").get<bool>();
    const unsigned groups = spec.value("groups", 2048U);
    const double budget = spec.value("budget_ms", 4.0);
    const auto material = spec.at("material").get<std::string>();
    if (initial.controls.header[0] || initial.controls.header[3] != 1 || !groups ||
        (budget != 4 && budget != 6 && budget != 8) || spec.at("poses").empty() ||
        spec.at("poses").size() > 128)
        throw std::invalid_argument(
            "stream requires rounded zero-edit intake and bounded recorded poses");
    std::vector<StreamPose> poses;
    for (const auto& p : spec.at("poses")) {
        StreamPose pose{p.at("t").get<double>(),
                        p.at("r").get<double>(),
                        p.at("p").get<std::array<double, 3>>(),
                        static_cast<unsigned>(poses.size()),
                        p.value("begin", false),
                        p.value("end", false),
                        p.value("corner", false)};
        if (pose.t < 0 || pose.t > 1 || pose.radius < .000001)
            throw std::invalid_argument(
                "stream needs one-second duration and positive admitted widths");
        poses.push_back(pose);
    }
    StreamSampler preflight(tolerance);
    std::vector<Json> all_shapes;
    for (const auto& pose : poses)
        for (const auto& segment : preflight.feed(pose))
            all_shapes.push_back(
                primitive(segment, "stream-" + std::to_string(all_shapes.size() + 1), material));
    if (preflight.down() || all_shapes.empty() || all_shapes.size() > 20)
        throw std::invalid_argument("stream must release and fit finite twenty-segment screen");
    const auto final_source = append(initial.base, all_shapes, cut, material);
    // Preflight all source prefixes, including depth/stack/history limits.
    for (unsigned n = 1; n < all_shapes.size(); ++n)
        static_cast<void>(append(initial.base,
                                 std::vector<Json>(all_shapes.begin(), all_shapes.begin() + n), cut,
                                 material));
    Json receipt{{"format", "cubey.sdf.finite-stroke-service"},
                 {"version", 1},
                 {"scope", "Recorded online poses; single completed GPU reader, bounded two-bank "
                           "Cached+8; no UI/XR claim"},
                 {"budget_ms", budget},
                 {"groups", groups},
                 {"tolerance", tolerance},
                 {"input_capacity", 64},
                 {"preflight_segments", all_shapes.size()},
                 {"frames", Json::array()},
                 {"jobs", Json::array()},
                 {"accepted", Json::array()},
                 {"complete", false}};
    ProjectRuntimeAdapter runtime;
    host::HeadlessPngHostCallbacks callbacks;
    callbacks.create_resources = [&](host::HeadlessPngContext& ctx) {
        runtime.attach_gpu_if_needed(ctx.gpu());
        Field a(ctx.device(), runtime.gpu()), b(ctx.device(), runtime.gpu());
        CachedCommitStudy builder(ctx.device(), runtime.gpu());
        receipt["pipeline_prepare_ms"] = builder.prepare(CommitBuilder::LocalDominance);
        std::map<std::string, double> bounds;
        builder.set_slicing(
            {.groups = groups,
             .copy_slab_bytes = 16U * 1024 * 1024,
             .before = {},
             .after =
                 [&](const CommitUnit& u) { bounds[u.kind] = std::max(bounds[u.kind], u.wall_ms); },
             .pooled = true});
        receipt["submission_prepare_ms"] = builder.prepare_submission();
        // Startup calibration only. Future job construction is repeated on-clock.
        for (unsigned n = 0; n < all_shapes.size(); n += 4) {
            const auto base =
                append(initial.base, std::vector<Json>(all_shapes.begin(), all_shapes.begin() + n),
                       cut, material);
            const auto edited =
                append(base,
                       std::vector<Json>(all_shapes.begin() + n,
                                         all_shapes.begin() +
                                             std::min<std::size_t>(n + 4, all_shapes.size())),
                       cut, material);
            const auto p = prepare_cached_stroke(base, edited, true, StrokePreparation::Palette);
            a.build(base);
            for (unsigned repeat = 0; repeat < 3; ++repeat)
                static_cast<void>(builder.build(b, a, p, CommitBuilder::LocalDominance, 4,
                                                StrokePreparation::Palette));
        }
        for (auto& [kind, bound] : bounds)
            bound = bound * 1.25 + .05;
        receipt["frozen_unit_wall_bounds_ms"] = bounds;
        a.build(initial.base);
        Field* published = &a;
        Field* candidate = &b;
        const auto geometry =
            a.stats().allocation_bytes + b.stats().allocation_bytes + builder.working_bytes();
        if (geometry > 2ULL * 1024 * 1024 * 1024)
            throw std::runtime_error("stream geometry screen exceeded");
        receipt["geometry_payload_bytes"] = geometry;
        auto control = std::make_unique<vulkan::Buffer>(
            ctx.device(),
            vulkan::BufferConfig{.size = sizeof(CachedStrokeControls),
                                 .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                 .memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT});
        CachedStrokeControls controls{};
        controls.header = {0, cut ? 1U : 0U,
                           static_cast<std::uint32_t>(initial.base.program.size()), 1};
        control->upload(&controls, sizeof(controls));
        Renderer renderer;
        const auto target = ctx.render_target();
        // Allocate source-program capacity during preflight, then activate only
        // the initial source. No future source is sampled by measured draws.
        renderer.create(ctx.device(), ctx.gpu(), target.extent, target.format, 1, published, config,
                        &final_source, nullptr, {}, nullptr, control.get());
        renderer.update_authored(ctx.device(), &initial.base, nullptr);
        auto draw = [&](unsigned frame) {
            const auto start = Clock::now();
            static_cast<void>(runtime.gpu().submit_and_wait(
                {.label = "stream completed draw", .work = [&](auto& owner) {
                     vulkan::ImmediateCommands command(owner);
                     auto c = command.command_buffer();
                     vulkan::transition_image_layout(
                         c, vulkan::begin_color_attachment_transition(target.image));
                     renderer.record(ctx.device(), c, target, render::FrameSlot{0}, false, config,
                                     config.yaw, config.pitch, config.distance, nullptr, frame);
                     command.submit_and_wait();
                 }}));
            return ms(start);
        };
        for (unsigned i = 0; i < 12; ++i)
            static_cast<void>(draw(i));
        StrokeServiceGate gate;
        gate.received = Clock::now();
        const auto received = gate.received;
        builder.set_slicing({.groups = groups,
                             .copy_slab_bytes = 16U * 1024 * 1024,
                             .before = [&](const CommitUnit& u) { gate.before(u); },
                             .after = [&](const CommitUnit& u) { gate.after(u); },
                             .pooled = true});
        std::mutex input_mutex;
        std::condition_variable input_cv;
        std::deque<StreamPose> queue;
        std::atomic<bool> stop_input = false, producer_done = false;
        unsigned max_queue = 0;
        std::thread producer([&] {
            for (const auto& pose : poses) {
                std::this_thread::sleep_until(received +
                                              std::chrono::duration_cast<Clock::duration>(
                                                  std::chrono::duration<double>(pose.t)));
                std::unique_lock lock(input_mutex);
                input_cv.wait(lock, [&] { return stop_input || queue.size() < 64; });
                if (stop_input)
                    break;
                queue.push_back(pose);
                max_queue = std::max(max_queue, static_cast<unsigned>(queue.size()));
            }
            producer_done = true;
        });
        StreamSampler sampler(tolerance);
        std::deque<Accepted> pending;
        std::optional<PreparedCachedStroke> committed;
        const AuthoredScene* source = &initial.base;
        std::future<JobResult> worker;
        std::uint64_t base_version = 1, next_id = 1;
        unsigned job_count = 0, consumed = 0, max_slots = 0;
        Clock::time_point job_received;
        std::vector<std::uint64_t> job_ids;
        bool drained = false;
        std::string blocked;
        // A tiny standalone scene compiles tool controls using the fixed palette.
        auto tool_doc = Json::parse(initial.base.document);
        tool_doc.erase("history");
        tool_doc.erase("geometry_hash");
        tool_doc.erase("appearance_hash");
        tool_doc["root"] = nullptr;
        tool_doc["materials"] = Json::parse(serialize_authored_palette(initial.base));
        auto instruction = [&](const Json& shape) {
            auto d = tool_doc;
            d["root"] = shape;
            return parse_authored_scene(d.dump()).program.at(0);
        };
        auto stop = [&] {
            stop_input = true;
            input_cv.notify_all();
            gate.request_stop();
        };
        try {
            for (unsigned frame = 0; frame < 120; ++frame) {
                const auto start = Clock::now();
                const auto scheduled = received + std::chrono::microseconds(16667ULL * frame);
                std::vector<unsigned> displayed;
                while (true) {
                    std::optional<StreamPose> pose;
                    {
                        std::lock_guard lock(input_mutex);
                        if (!queue.empty())
                            pose = queue.front();
                    }
                    if (!pose)
                        break;
                    auto proposed = sampler;
                    const auto accepted = proposed.feed(*pose);
                    const auto slots = pending.size() + accepted.size() + (proposed.tip() ? 1 : 0);
                    if (slots > 8)
                        break; // Backpressure, without losing or accepting an ID.
                    sampler = std::move(proposed);
                    {
                        std::lock_guard lock(input_mutex);
                        queue.pop_front();
                    }
                    input_cv.notify_all();
                    for (const auto& segment : accepted) {
                        auto shape =
                            primitive(segment, "stream-" + std::to_string(next_id), material);
                        pending.push_back(
                            {next_id, shape, instruction(shape), pose->t * 1000, ms(received)});
                        receipt["accepted"].push_back({{"id", next_id},
                                                       {"first", segment.first.index},
                                                       {"last", segment.last.index},
                                                       {"error", segment.error},
                                                       {"shape", shape},
                                                       {"source_accept_ms", pose->t * 1000},
                                                       {"admitted_ms", ms(received)}});
                        ++next_id;
                    }
                    ++consumed;
                    displayed.push_back(pose->index);
                    max_slots = std::max(max_slots, static_cast<unsigned>(slots));
                }
                if (!worker.valid() && !pending.empty()) {
                    job_received = Clock::now();
                    job_count = std::min<unsigned>(4, pending.size());
                    job_ids.clear();
                    std::vector<Json> shapes;
                    for (unsigned i = 0; i < job_count; ++i) {
                        job_ids.push_back(pending[i].id);
                        shapes.push_back(pending[i].shape);
                    }
                    const StrokeVersion version{1, base_version, 1, job_ids};
                    // Source copying, JSON construction, parsing and validation
                    // all happen after the accepted-input clock, on the worker.
                    worker = std::async(std::launch::async, [&, version, shapes,
                                                             base_source = source,
                                                             begin = job_received, dst = candidate,
                                                             src = published] {
                        auto edited = append(*base_source, shapes, cut, material);
                        auto p = seal_cached_stroke(*base_source, std::move(edited), version, true);
                        const double preparation = ms(begin);
                        auto stats =
                            builder.build(*dst, *src, p, version, CommitBuilder::LocalDominance);
                        return JobResult{std::move(p), stats, preparation};
                    });
                }
                const auto window = Clock::now();
                while (worker.valid() && ms(window) < budget) {
                    if (worker.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                        break;
                    std::unique_lock lock(gate.mutex);
                    if (gate.waiting && !gate.active && !gate.permit) {
                        const auto it = bounds.find(gate.kind);
                        if (it == bounds.end() || it->second > budget) {
                            blocked = gate.kind;
                            break;
                        }
                        if (ms(window) + it->second <= budget) {
                            gate.frame = frame;
                            gate.grant_ms = ms(received);
                            gate.permit = true;
                            gate.cv.notify_all();
                        }
                    }
                    gate.cv.wait_for(lock, std::chrono::microseconds(50));
                }
                const double maintenance_ms = ms(window);
                bool crossed;
                {
                    std::lock_guard lock(gate.mutex);
                    crossed = gate.active || gate.permit;
                }
                std::vector<std::uint64_t> publishing;
                if (worker.valid() &&
                    worker.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                    auto result = worker.get();
                    std::vector<std::uint64_t> current_ids;
                    for (unsigned i = 0; i < job_count; ++i)
                        current_ids.push_back(pending.at(i).id);
                    result.prepared.require_version({1, base_version, 1, current_ids});
                    publishing = job_ids;
                    committed.emplace(std::move(result.prepared));
                    source = &committed->program().edited;
                    std::swap(published, candidate);
                    ++base_version;
                    renderer.update_authored(ctx.device(), source, nullptr);
                    renderer.update_field(ctx.device(), *published);
                    for (unsigned i = 0; i < job_count; ++i)
                        pending.pop_front();
                    receipt["jobs"].push_back({{"ids", job_ids},
                                               {"worker_preparation_ms", result.preparation_ms},
                                               {"builder_admission_ms", result.stats.admission_ms},
                                               {"builder_wall_ms", result.stats.wall_ms},
                                               {"publication_ready_ms", ms(received)},
                                               {"received_to_publication_ms", ms(job_received)}});
                }
                controls = {};
                controls.header = {static_cast<std::uint32_t>(pending.size()), cut ? 1U : 0U,
                                   static_cast<std::uint32_t>(source->program.size()), 1};
                for (unsigned i = 0; i < pending.size(); ++i)
                    controls.tools[i] = pending[i].instruction;
                if (const auto tip = sampler.tip())
                    controls.tools[controls.header[0]++] =
                        instruction(primitive(*tip, "provisional-tip", material));
                if (controls.header[0] > 8)
                    throw std::logic_error("pending/tip capacity violated");
                control->upload(&controls, sizeof(controls));
                const double draw_ms = draw(frame), completed = ms(received);
                if (!publishing.empty())
                    receipt["jobs"].back()["first_completed_draw_ms"] = completed;
                receipt["frames"].push_back({{"frame", frame},
                                             {"completed_ms", completed},
                                             {"scheduled_to_completion_ms", ms(scheduled)},
                                             {"service_wall_ms", ms(start)},
                                             {"maintenance_wall_ms", maintenance_ms},
                                             {"draw_complete_wall_ms", draw_ms},
                                             {"pending", controls.header[0]},
                                             {"displayed_pose_indices", displayed},
                                             {"published_ids", publishing},
                                             {"unit_crossed_window", crossed}});
                bool empty;
                {
                    std::lock_guard lock(input_mutex);
                    empty = queue.empty();
                }
                if (producer_done && empty && !sampler.down() && pending.empty() &&
                    !worker.valid()) {
                    drained = true;
                    break;
                }
                if (crossed || !blocked.empty())
                    break;
                std::this_thread::sleep_until(received +
                                              std::chrono::microseconds(16667ULL * (frame + 1)));
            }
            stop();
            producer.join();
            if (worker.valid()) {
                try {
                    (void)worker.get();
                } catch (const StopService&) {
                }
            }
        } catch (...) {
            stop();
            producer.join();
            if (worker.valid()) {
                try {
                    (void)worker.get();
                } catch (...) {
                }
            }
            renderer.destroy();
            throw;
        }
        renderer.destroy();
        receipt["drained"] = drained;
        receipt["consumed_poses"] = consumed;
        receipt["max_input_queue"] = max_queue;
        receipt["max_pending_including_tip"] = max_slots;
        receipt["blocked_kind"] = blocked;
        receipt["units"] = gate.units;
        receipt["accepted_count"] = next_id - 1;
        receipt["retained_final_leaves"] = source->leaf_count;
        receipt["uncommitted_ids"] = Json::array();
        for (const auto& edit : pending)
            receipt["uncommitted_ids"].push_back(edit.id);
        if (drained) {
            const auto actual =
                CachedCommitStudy::snapshot(ctx.device(), runtime.gpu(), *published);
            candidate->build(final_source);
            const auto expected =
                CachedCommitStudy::snapshot(ctx.device(), runtime.gpu(), *candidate);
            receipt["canonical_cache_equal"] = actual.topology == expected.topology &&
                                               actual.cells == expected.cells &&
                                               actual.samples == expected.samples;
            receipt["canonical_source_equal"] =
                serialize_authored_scene(*source) == serialize_authored_scene(final_source);
            const auto scene_path = output.parent_path() / (output.stem().string() + "-scene.json");
            save_authored_scene(scene_path, *source);
            receipt["retained_scene"] = scene_path.string();
            if (!receipt["canonical_cache_equal"].get<bool>() ||
                !receipt["canonical_source_equal"].get<bool>())
                throw std::runtime_error("stream failed canonical comparison");
        }
        receipt["complete"] = true;
        std::ofstream(output) << receipt.dump(2) << '\n';
    };
    callbacks.record_frame = [](auto&, const auto&, VkCommandBuffer, const auto&) {};
    callbacks.shutdown = [&](auto&) { runtime.detach_gpu_if_attached(); };
    config.common.headless_render_only = true;
    config.common.frames = 1;
    config.common.render_fps = 0;
    return host::HeadlessPngHost(
               {.run_config = config.common,
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT},
               std::move(callbacks))
        .run();
}
} // namespace cubey::projects::sdf
