#include "stroke_service.h"
#include "../sdf_cached_commit.h"
#include "../sdf_renderer.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cubey/engine/project_runtime.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/immediate_commands.h>
#include <fstream>
#include <future>
#include <map>
#include <mutex>
#include <thread>

namespace cubey::projects::sdf {
namespace {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
double ms(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}
struct StopService {};
// One worker, one waiting/submitted unit. The frame thread only grants work
// that fits the frozen calibration bound; it never joins a construction job.
struct Gate {
    std::mutex mutex;
    std::condition_variable cv;
    bool stop = false, waiting = false, active = false, permit = false;
    std::string kind;
    unsigned frame = 0;
    Json units = Json::array();
    void before(std::string_view next) {
        std::unique_lock lock(mutex);
        kind = next;
        waiting = true;
        cv.notify_all();
        cv.wait(lock, [&] { return stop || permit; });
        if (stop)
            throw StopService{};
        permit = false;
        waiting = false;
        active = true;
    }
    void after(const CommitUnit& unit) {
        std::lock_guard lock(mutex);
        units.push_back({{"frame", frame},
                         {"kind", unit.kind},
                         {"groups", unit.groups},
                         {"level", unit.level},
                         {"gpu_ms", unit.gpu_ms},
                         {"wall_ms", unit.wall_ms}});
        active = false;
        cv.notify_all();
    }
};
} // namespace

int run_cached_stroke_service(SdfConfig config, const Json& input,
                              const CachedStrokeProgram& program,
                              const std::filesystem::path& output) {
    const double budget = input.at("service").at("budget_ms").get<double>();
    const unsigned groups = input.at("service").value("groups", 512U);
    const unsigned frames = input.at("service").value("frames", 60U);
    if ((budget != 2 && budget != 4) || !groups || frames < 8 || frames > 240 ||
        program.controls.header[0] != 4)
        throw std::invalid_argument(
            "service requires four tools, a fixed 2/4 ms budget and 8..240 frames");
    for (unsigned i = 0; i < 4; ++i)
        if (program.controls.tools[i].center_code.w != 2.F)
            throw std::invalid_argument("service control remains spheres only");
    Json receipt{{"format", "cubey.sdf.cached-stroke-service"},
                 {"version", 1},
                 {"scope", "Single-reader paced desktop control; retained parsed source; "
                           "worker admission and bounded GPU units; no live input or XR"},
                 {"budget_ms", budget},
                 {"groups", groups},
                 {"frame_period_ms", 1000.0 / 60},
                 {"demand", "four accepted edits completed within four frames"},
                 {"frames", Json::array()},
                 {"calibration", Json::array()},
                 {"complete", false}};
    ProjectRuntimeAdapter runtime;
    host::HeadlessPngHostCallbacks callbacks;
    callbacks.create_resources = [&](host::HeadlessPngContext& ctx) {
        runtime.attach_gpu_if_needed(ctx.gpu());
        Field base(ctx.device(), runtime.gpu()), candidate(ctx.device(), runtime.gpu());
        base.build(program.base);
        CachedCommitStudy builder(ctx.device(), runtime.gpu());
        receipt["pipeline_prepare_ms"] = builder.prepare(CommitBuilder::LocalDominance);
        receipt["geometry_payload_bytes"] = base.stats().allocation_bytes +
                                            candidate.stats().allocation_bytes +
                                            builder.working_bytes();
        if (receipt["geometry_payload_bytes"].get<std::uint64_t>() > 2ULL * 1024 * 1024 * 1024)
            throw std::runtime_error("service exceeds geometry resource screen");
        std::map<std::string, double> bounds;
        builder.set_slicing({.groups = groups,
                             .copy_slab_bytes = 16U * 1024 * 1024,
                             .before = {},
                             .after = [&](const CommitUnit& u) {
                                 bounds[u.kind] = std::max(bounds[u.kind], u.wall_ms);
                                 receipt["calibration"].push_back({{"kind", u.kind},
                                                                   {"gpu_ms", u.gpu_ms},
                                                                   {"wall_ms", u.wall_ms}});
                             }});
        for (unsigned i = 0; i < 4; ++i)
            static_cast<void>(
                builder.build(candidate, base, program, CommitBuilder::LocalDominance));
        for (auto& [kind, bound] : bounds)
            bound = bound * 1.25 + .05; // frozen before the measured run
        receipt["frozen_unit_wall_bounds_ms"] = bounds;
        auto control = std::make_unique<vulkan::Buffer>(
            ctx.device(),
            vulkan::BufferConfig{.size = sizeof(program.controls),
                                 .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                 .memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT});
        control->upload(&program.controls, sizeof(program.controls));
        Renderer preview, settled;
        const auto target = ctx.render_target();
        preview.create(ctx.device(), ctx.gpu(), target.extent, target.format, 1, &base, config,
                       &program.edited, nullptr, {}, nullptr, control.get());
        settled.create(ctx.device(), ctx.gpu(), target.extent, target.format, 1, &base, config,
                       &program.edited);
        auto draw = [&](Renderer& renderer, unsigned index) {
            const auto begin = Clock::now();
            static_cast<void>(runtime.gpu().submit_and_wait(
                {.label = "service completed draw", .work = [&](vulkan::GpuOwnerContext& owner) {
                     vulkan::ImmediateCommands commands(owner);
                     const auto cmd = commands.command_buffer();
                     vulkan::transition_image_layout(
                         cmd, vulkan::begin_color_attachment_transition(target.image));
                     renderer.record(ctx.device(), cmd, target, render::FrameSlot{0}, false, config,
                                     config.yaw, config.pitch, config.distance, nullptr, index);
                     commands.submit_and_wait();
                 }}));
            return ms(begin);
        };
        for (unsigned i = 0; i < 12; ++i)
            static_cast<void>(draw(preview, i));
        Gate gate;
        builder.set_slicing({.groups = groups,
                             .copy_slab_bytes = 16U * 1024 * 1024,
                             .before = [&](std::string_view kind) { gate.before(kind); },
                             .after = [&](const CommitUnit& unit) { gate.after(unit); }});
        const auto received = Clock::now();
        std::atomic<double> preparation_ms{0};
        auto worker = std::async(std::launch::async, [&] {
            // Immutable retained parsed snapshots. Copy/validation happens on
            // the worker and remains in total received-to-publication age.
            auto prepared = prepare_cached_stroke(program.base, program.edited);
            preparation_ms = ms(received);
            return builder.build(candidate, base, prepared, CommitBuilder::LocalDominance);
        });
        bool published = false, failed = false;
        unsigned published_frame = frames;
        std::optional<CommitBuildStats> stats;
        double worker_ready_ms = 0;
        double replacement_completed_ms = 0;
        std::string blocked;
        auto stop = [&] {
            {
                std::lock_guard lock(gate.mutex);
                gate.stop = true;
            }
            gate.cv.notify_all();
        };
        try {
            for (unsigned frame = 0; frame < frames; ++frame) {
                const auto start = Clock::now();
                const auto scheduled = received + std::chrono::microseconds(16667ULL * frame);
                if (!published && worker.valid() &&
                    worker.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                    stats = worker.get();
                    worker_ready_ms = ms(received);
                    // Every prior draw completed. The all-slot descriptor update
                    // is used only under this explicit single-reader boundary.
                    settled.update_field(ctx.device(), candidate);
                    published = true;
                    published_frame = frame;
                }
                const double draw_ms = draw(published ? settled : preview, frame);
                if (published && replacement_completed_ms == 0)
                    replacement_completed_ms = ms(received);
                const auto window = Clock::now();
                while (!published && ms(window) < budget) {
                    std::unique_lock lock(gate.mutex);
                    if (gate.waiting && !gate.active && !gate.permit) {
                        const auto it = bounds.find(gate.kind);
                        if (it == bounds.end())
                            throw std::runtime_error("uncalibrated maintenance unit");
                        if (it->second > budget) {
                            blocked = gate.kind;
                            break;
                        }
                        if (ms(window) + it->second <= budget) {
                            gate.frame = frame;
                            gate.permit = true;
                            gate.cv.notify_all();
                        }
                    }
                    gate.cv.wait_for(lock, std::chrono::microseconds(50));
                }
                bool active;
                {
                    std::lock_guard lock(gate.mutex);
                    active = gate.active || gate.permit;
                }
                receipt["frames"].push_back({{"frame", frame},
                                             {"draw_complete_wall_ms", draw_ms},
                                             {"service_wall_ms", ms(start)},
                                             {"job_age_ms", ms(received)},
                                             {"scheduled_to_completion_ms", ms(scheduled)},
                                             {"published", published},
                                             {"pending", published ? 0 : 4},
                                             {"unit_crossed_window", active},
                                             {"blocked_oversized_kind", blocked}});
                std::this_thread::sleep_until(received +
                                              std::chrono::microseconds(16667ULL * (frame + 1)));
            }
            stop();
            if (worker.valid()) {
                try {
                    stats = worker.get();
                } catch (const StopService&) {
                    failed = true;
                }
            }
        } catch (...) {
            stop();
            if (worker.valid()) {
                try {
                    static_cast<void>(worker.get());
                } catch (...) {
                }
            }
            preview.destroy();
            settled.destroy();
            throw;
        }
        receipt["units"] = gate.units;
        receipt["worker_ready_observed_ms"] = worker_ready_ms;
        receipt["worker_preparation_ms"] = preparation_ms.load();
        receipt["replacement_completed_ms"] =
            published ? Json(replacement_completed_ms) : Json(nullptr);
        receipt["published_frame"] = published ? Json(published_frame) : Json(nullptr);
        receipt["cancelled_at_limit"] = failed;
        receipt["drained"] = published;
        bool deadlines = true;
        for (const auto& frame : receipt["frames"])
            deadlines = deadlines &&
                        frame.at("scheduled_to_completion_ms").get<double>() <= 16.67 &&
                        !frame.at("unit_crossed_window").get<bool>();
        receipt["full_frame_and_unit_deadlines_passed"] = deadlines;
        receipt["service_passed"] =
            published && replacement_completed_ms <= 4 * (1000.0 / 60) && deadlines;
        receipt["scope_boundary"] =
            "One completed draw at a time; no delayed/in-flight reader qualification";
        if (stats) {
            receipt["build"] = {
                {"admission_ms", stats->admission_ms}, {"builder_wall_ms", stats->wall_ms},
                {"copy_gpu_ms", stats->copy.gpu_ms},   {"fallback_gpu_ms", stats->fallback_gpu_ms},
                {"reuse_gpu_ms", stats->reuse_gpu_ms}, {"local_work", stats->local_work}};
            receipt["build"]["classification_gpu_ms"] = candidate.stats().filter_gpu_ms;
        }
        preview.destroy();
        settled.destroy();
        if (published) {
            const auto actual = CachedCommitStudy::snapshot(ctx.device(), runtime.gpu(), candidate);
            base.build(program.edited);
            const auto expected = CachedCommitStudy::snapshot(ctx.device(), runtime.gpu(), base);
            receipt["canonical_cache_equal"] = actual.topology == expected.topology &&
                                               actual.cells == expected.cells &&
                                               actual.samples == expected.samples;
            if (!receipt["canonical_cache_equal"].get<bool>())
                throw std::runtime_error("service published a noncanonical cache");
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
