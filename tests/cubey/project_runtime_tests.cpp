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
    cubey::RenderPacket packet =
        project.render_packet({.delta_seconds = 0.016, .frame_index = 1, .ticket = {.value = 1}},
                              context);
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
