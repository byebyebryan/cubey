#include <cubey/runtime/capture_queue.h>

#include <cubey/core/image_io.h>

#include <utility>

namespace cubey {

CaptureTicket::CaptureTicket(std::filesystem::path output_path, jobs::JobHandle<void> job)
    : output_path_(std::move(output_path)), job_(std::move(job)) {}

CaptureQueue::CaptureQueue(jobs::JobSystem& jobs)
    : CaptureQueue([&jobs](std::function<void()> job) { return jobs.submit(std::move(job)); }) {}

CaptureQueue::CaptureQueue(jobs::InlineExecutor& jobs)
    : CaptureQueue([&jobs](std::function<void()> job) { return jobs.submit(std::move(job)); }) {}

CaptureQueue::CaptureQueue(SubmitFunction submit) : submit_(std::move(submit)) {}

CaptureTicket CaptureQueue::enqueue_png(CaptureRequest request) {
    std::filesystem::path output_path = request.output_path;
    jobs::JobHandle<void> job = submit_([request = std::move(request)] {
        write_png_rgba8(request.output_path, request.width, request.height, request.rgba8);
    });
    return {std::move(output_path), std::move(job)};
}

} // namespace cubey
