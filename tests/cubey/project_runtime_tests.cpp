#include <cubey/project_runtime.h>

#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct TestProject {
    void setup(cubey::ProjectContext& context) {
        setup_seen = true;
        upload = context.upload_queue().enqueue({
            .label = "project upload",
            .bytes = {1, 2, 3},
        });
    }

    void update(cubey::ProjectFrame frame, cubey::ProjectContext& unused_context) {
        (void)unused_context;
        last_frame = frame.frame_index;
    }

    cubey::RenderPacket render_packet(cubey::ProjectFrame unused_frame,
                                      cubey::ProjectContext& unused_context) {
        (void)unused_frame;
        (void)unused_context;
        return {
            .label = "test project packet",
            .upload_tickets = {upload},
            .draw_count = 1,
        };
    }

    void resize(cubey::ProjectExtent extent, cubey::ProjectContext& unused_context) {
        (void)unused_context;
        resized_width = extent.width;
    }

    void shutdown(cubey::ProjectContext& context) {
        context.deferred_destruction().defer_after(cubey::FrameTicket{.value = 1},
                                                   [this] { shutdown_seen = true; });
    }

    cubey::UploadTicket upload;
    std::uint64_t last_frame = 0;
    std::uint32_t resized_width = 0;
    bool setup_seen = false;
    bool shutdown_seen = false;
};

static_assert(cubey::ProjectLike<TestProject>);

} // namespace

void test_project_context_exposes_async_runtime_services() {
    cubey::jobs::JobSystem jobs(1);
    cubey::UploadQueue uploads;
    cubey::CaptureQueue captures(jobs);
    cubey::FrameTicketIssuer tickets;
    cubey::DeferredDestructionQueue deferred;
    cubey::ProjectContext context(jobs, uploads, captures, tickets, deferred);

    auto job = context.jobs().submit([] { return 9; });
    require(job.get() == 9, "project context should expose job system");

    cubey::UploadTicket upload = context.upload_queue().enqueue({
        .label = "context upload",
        .bytes = {4, 5},
    });
    require(upload.id == 1, "project context should expose upload queue");

    cubey::FrameTicket frame_ticket = context.frame_tickets().issue();
    require(frame_ticket.value == 1, "project context should expose frame tickets");
}

void test_project_runtime_contract_supports_lifecycle_shape() {
    cubey::jobs::JobSystem jobs(1);
    cubey::UploadQueue uploads;
    cubey::CaptureQueue captures(jobs);
    cubey::FrameTicketIssuer tickets;
    cubey::DeferredDestructionQueue deferred;
    cubey::ProjectContext context(jobs, uploads, captures, tickets, deferred);
    TestProject project;

    project.setup(context);
    project.update({.delta_seconds = 0.016, .frame_index = 1, .ticket = {.value = 1}}, context);
    cubey::RenderPacket packet = project.render_packet(
        {.delta_seconds = 0.016, .frame_index = 1, .ticket = {.value = 1}}, context);
    project.resize({.width = 800, .height = 600}, context);
    project.shutdown(context);

    require(project.setup_seen, "project setup should run");
    require(project.last_frame == 1, "project update should receive frame index");
    require(packet.label == "test project packet", "project should return render packet");
    require(packet.upload_tickets.size() == 1, "render packet should carry upload tickets");
    require(packet.draw_count == 1, "render packet should carry draw count metadata");
    require(project.resized_width == 800, "project resize should receive extent");
    require(deferred.retire_completed({.value = 1}) == 1,
            "project shutdown should be able to defer destruction");
    require(project.shutdown_seen, "deferred shutdown action should run");
}

void test_project_runtime_services_create_project_frames_and_context() {
    cubey::ProjectRuntimeServices services(1);
    cubey::ProjectContext context = services.context();

    auto job = context.jobs().submit([] { return 12; });
    require(job.get() == 12, "runtime services should expose a working job system");

    cubey::UploadTicket upload = context.upload_queue().enqueue({
        .label = "runtime upload",
        .bytes = {7, 8, 9},
    });
    require(upload.id == 1, "runtime services should expose upload queue state");

    cubey::ProjectFrame frame = services.begin_frame({
        .delta_seconds = 0.25,
        .elapsed_seconds = 1.5,
        .frame_index = 4,
    });
    require(frame.delta_seconds == 0.25, "project frame should preserve delta time");
    require(frame.elapsed_seconds == 1.5, "project frame should preserve elapsed time");
    require(frame.frame_index == 4, "project frame should preserve frame index");
    require(frame.ticket.value == 1, "project frame should issue a frame ticket");

    context.deferred_destruction().defer_after(frame.ticket, [] {});
    require(context.deferred_destruction().retire_completed(frame.ticket) == 1,
            "runtime services should expose deferred destruction state");
}

void test_project_runtime_adapter_reuses_frame_for_same_timing() {
    cubey::ProjectRuntimeAdapter adapter(1);

    const cubey::ProjectFrame& initial = adapter.frame_for_timing({});
    require(initial.ticket.value == 1,
            "runtime adapter should issue a valid ticket for an initial zero timing");

    const cubey::FrameTiming first_timing{
        .delta_seconds = 0.016,
        .elapsed_seconds = 0.5,
        .frame_index = 7,
    };
    const cubey::ProjectFrame& first = adapter.frame_for_timing(first_timing);
    const cubey::ProjectFrame& repeated = adapter.frame_for_timing(first_timing);

    require(first.ticket.value == repeated.ticket.value,
            "runtime adapter should reuse the project frame for the same host frame");
    require(first.delta_seconds == 0.016, "runtime adapter should preserve delta time");
    require(first.elapsed_seconds == 0.5, "runtime adapter should preserve elapsed time");
    require(first.frame_index == 7, "runtime adapter should preserve frame index");
    const std::uint64_t first_ticket = first.ticket.value;

    const cubey::ProjectFrame& second = adapter.frame_for_timing({
        .delta_seconds = 0.02,
        .elapsed_seconds = 0.52,
        .frame_index = 8,
    });
    require(second.ticket.value == first_ticket + 1,
            "runtime adapter should issue a new ticket for a new host frame");
    require(second.delta_seconds == 0.02, "runtime adapter should update delta time");
    require(second.elapsed_seconds == 0.52, "runtime adapter should update elapsed time");
    require(second.frame_index == 8, "runtime adapter should update frame index");
}

void test_project_runtime_adapter_exposes_context_and_retirement() {
    cubey::ProjectRuntimeAdapter adapter(1);
    cubey::ProjectContext context = adapter.context();

    auto job = context.jobs().submit([] { return 21; });
    require(job.get() == 21, "runtime adapter should expose project job services");

    const cubey::ProjectFrame& frame = adapter.frame_for_timing({
        .delta_seconds = 0.033,
        .elapsed_seconds = 2.0,
        .frame_index = 3,
    });
    bool retired = false;
    context.deferred_destruction().defer_after(frame.ticket, [&retired] { retired = true; });

    require(adapter.retire_deferred_destruction() == 1,
            "runtime adapter should retire deferred actions through the current frame ticket");
    require(retired, "runtime adapter should run retired deferred actions");
}
