#include <cubey/engine/project_gpu_services.h>
#include <cubey/engine/project_runtime.h>

#include <vulkan/vulkan.h>

#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

cubey::vulkan::Device* fake_device() {
    return reinterpret_cast<cubey::vulkan::Device*>(0x55);
}

cubey::vulkan::SubmissionCoordinator fake_submission() {
    return cubey::vulkan::SubmissionCoordinator(
        reinterpret_cast<VkQueue>(0x56),
        [](VkQueue, const cubey::vulkan::QueueSubmitInfo&, const char*) {},
        [](VkQueue, const char*) {});
}

void require_throws(auto&& action, const char* message) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
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
        (void)context;
        shutdown_seen = true;
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
    cubey::ProjectContext context(jobs, uploads, captures);

    auto job = context.jobs().submit([] { return 9; });
    require(job.get() == 9, "project context should expose job system");

    cubey::UploadTicket upload = context.upload_queue().enqueue({
        .label = "context upload",
        .bytes = {4, 5},
    });
    require(upload.id == 1, "project context should expose upload queue");

}

void test_project_runtime_contract_supports_lifecycle_shape() {
    cubey::jobs::JobSystem jobs(1);
    cubey::UploadQueue uploads;
    cubey::CaptureQueue captures(jobs);
    cubey::ProjectContext context(jobs, uploads, captures);
    TestProject project;

    project.setup(context);
    project.update({.delta_seconds = 0.016, .frame_index = 1}, context);
    cubey::RenderPacket packet =
        project.render_packet({.delta_seconds = 0.016, .frame_index = 1}, context);
    project.resize({.width = 800, .height = 600}, context);
    project.shutdown(context);

    require(project.setup_seen, "project setup should run");
    require(project.last_frame == 1, "project update should receive frame index");
    require(packet.label == "test project packet", "project should return render packet");
    require(packet.upload_tickets.size() == 1, "render packet should carry upload tickets");
    require(packet.draw_count == 1, "render packet should carry draw count metadata");
    require(project.resized_width == 800, "project resize should receive extent");
    require(project.shutdown_seen, "project shutdown should run");
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
}

void test_project_runtime_adapter_reuses_frame_for_same_timing() {
    cubey::ProjectRuntimeAdapter adapter(1);

    const cubey::ProjectFrame& initial = adapter.frame_for_timing({});
    require(initial.frame_index == 0,
            "runtime adapter should create an initial frame for zero timing");

    const cubey::FrameTiming first_timing{
        .delta_seconds = 0.016,
        .elapsed_seconds = 0.5,
        .frame_index = 7,
    };
    const cubey::ProjectFrame& first = adapter.frame_for_timing(first_timing);
    const cubey::ProjectFrame& repeated = adapter.frame_for_timing(first_timing);

    require(&first == &repeated,
            "runtime adapter should reuse the project frame storage for the same host frame");
    require(first.delta_seconds == 0.016, "runtime adapter should preserve delta time");
    require(first.elapsed_seconds == 0.5, "runtime adapter should preserve elapsed time");
    require(first.frame_index == 7, "runtime adapter should preserve frame index");
    const cubey::ProjectFrame& second = adapter.frame_for_timing({
        .delta_seconds = 0.02,
        .elapsed_seconds = 0.52,
        .frame_index = 8,
    });
    require(second.delta_seconds == 0.02, "runtime adapter should update delta time");
    require(second.elapsed_seconds == 0.52, "runtime adapter should update elapsed time");
    require(second.frame_index == 8, "runtime adapter should update frame index");
}

void test_project_runtime_adapter_exposes_context() {
    cubey::ProjectRuntimeAdapter adapter(1);
    cubey::ProjectContext context = adapter.context();

    auto job = context.jobs().submit([] { return 21; });
    require(job.get() == 21, "runtime adapter should expose project job services");

    const cubey::ProjectFrame& frame = adapter.frame_for_timing({
        .delta_seconds = 0.033,
        .elapsed_seconds = 2.0,
        .frame_index = 3,
    });
    require(frame.frame_index == 3 && frame.elapsed_seconds == 2.0,
            "runtime adapter should expose host timing through its project frame");
}

void test_project_runtime_adapter_attaches_gpu_services_to_context() {
    cubey::ProjectRuntimeAdapter adapter(0);
    cubey::ProjectContext detached_context = adapter.context();
    require(!detached_context.has_gpu(),
            "runtime adapter context should start without GPU services");
    require_throws([&detached_context] { static_cast<void>(detached_context.gpu()); },
                   "detached runtime context should reject GPU access");

    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });
    adapter.attach_gpu_if_needed(runtime);

    cubey::ProjectContext attached_context = adapter.context();
    require(adapter.has_gpu(), "runtime adapter should report attached GPU services");
    require(attached_context.has_gpu(), "attached runtime context should expose GPU services");
    require(&attached_context.gpu() == &adapter.gpu(),
            "attached runtime context should reference adapter GPU services");
    cubey::ProjectGpuServices* first_gpu_services = &adapter.gpu();
    adapter.attach_gpu_if_needed(runtime);
    require(&adapter.gpu() == first_gpu_services,
            "attach-if-needed should preserve existing GPU services");

    adapter.detach_gpu_if_attached();
    cubey::ProjectContext detached_again = adapter.context();
    require(!adapter.has_gpu(), "runtime adapter should report detached GPU services");
    require(!detached_again.has_gpu(), "runtime context should lose GPU services after detach");
    require_throws([&detached_again] { static_cast<void>(detached_again.gpu()); },
                   "detached-again runtime context should reject GPU access");
    adapter.detach_gpu_if_attached();
    require(!adapter.has_gpu(), "detach-if-attached should tolerate repeated detach calls");
}
