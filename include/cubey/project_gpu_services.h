#pragma once

#include <cubey/frame_tickets.h>
#include <cubey/upload_queue.h>
#include <cubey/vulkan/gpu_runtime.h>

#include <cstddef>
#include <functional>
#include <vector>

namespace cubey {

struct ProjectGpuUploadDrainResult {
    std::size_t upload_count = 0;
    std::vector<vulkan::GpuWorkTicket> work_tickets;
};

using ProjectGpuUploadHandler = std::function<void(const QueuedUpload&, vulkan::GpuOwnerContext&)>;

class ProjectGpuServices {
  public:
    ProjectGpuServices(vulkan::GpuRuntime& gpu, UploadQueue& uploads,
                       DeferredDestructionQueue& deferred_destruction);

    ProjectGpuServices(const ProjectGpuServices&) = delete;
    ProjectGpuServices& operator=(const ProjectGpuServices&) = delete;
    ProjectGpuServices(ProjectGpuServices&&) = delete;
    ProjectGpuServices& operator=(ProjectGpuServices&&) = delete;

    [[nodiscard]] ProjectGpuUploadDrainResult
    enqueue_pending_uploads(ProjectGpuUploadHandler handler);
    [[nodiscard]] vulkan::GpuDrainResult drain();
    [[nodiscard]] std::size_t retire_deferred_destruction();

  private:
    vulkan::GpuRuntime* gpu_;
    UploadQueue* uploads_;
    DeferredDestructionQueue* deferred_destruction_;
};

} // namespace cubey
