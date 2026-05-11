#pragma once

#include <cubey/frame_tickets.h>
#include <cubey/upload_queue.h>
#include <cubey/vulkan/gpu_runtime.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

namespace cubey {

struct ProjectGpuUploadDrainResult {
    std::size_t upload_count = 0;
    std::vector<vulkan::GpuWorkTicket> work_tickets;
};

using ProjectGpuUploadHandler = std::function<void(const QueuedUpload&, vulkan::GpuOwnerContext&)>;

enum class ProjectGpuReadbackState {
    Pending,
    Completed,
    Failed,
};

struct ProjectGpuReadbackTicket {
    std::uint64_t id = 0;
    std::string label;
    std::size_t byte_count = 0;
};

struct ProjectGpuReadbackStatus {
    ProjectGpuReadbackState state = ProjectGpuReadbackState::Pending;
    FrameTicket completed_submission;
    std::size_t byte_count = 0;
    std::string error;
};

struct ProjectGpuReadbackResult {
    ProjectGpuReadbackTicket ticket;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba8;
};

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
    [[nodiscard]] vulkan::GpuWorkTicket enqueue(vulkan::GpuWorkRequest request);
    [[nodiscard]] vulkan::GpuWorkTicket submit_and_wait(vulkan::GpuWorkRequest request);
    void wait_queue_idle(std::string label);
    [[nodiscard]] vulkan::GpuDrainResult drain();
    [[nodiscard]] std::size_t retire_deferred_destruction();
    [[nodiscard]] ProjectGpuReadbackTicket
    enqueue_rgba8_image_readback(VkImage source, VkExtent2D extent, std::string label);
    [[nodiscard]] ProjectGpuReadbackStatus
    readback_status(const ProjectGpuReadbackTicket& ticket) const;
    [[nodiscard]] ProjectGpuReadbackResult
    take_completed_readback(const ProjectGpuReadbackTicket& ticket);

  private:
    void mark_readback_completed(const ProjectGpuReadbackTicket& ticket, std::uint32_t width,
                                 std::uint32_t height, std::vector<std::uint8_t> rgba8,
                                 FrameTicket completed_submission);
    void mark_readback_failed(const ProjectGpuReadbackTicket& ticket, std::string error);

    vulkan::GpuRuntime* gpu_;
    UploadQueue* uploads_;
    DeferredDestructionQueue* deferred_destruction_;
    mutable std::mutex readback_mutex_;
    std::uint64_t next_readback_id_ = 1;
    std::unordered_map<std::uint64_t, ProjectGpuReadbackStatus> readback_statuses_;
    std::unordered_map<std::uint64_t, ProjectGpuReadbackResult> readback_results_;
};

} // namespace cubey
