#pragma once

#include <cubey/engine/upload_queue.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/submission_tickets.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <utility>
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
    vulkan::GpuSubmissionTicket completed_submission;
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
    ProjectGpuServices(vulkan::GpuRuntime& gpu, UploadQueue& uploads);
    ~ProjectGpuServices();

    ProjectGpuServices(const ProjectGpuServices&) = delete;
    ProjectGpuServices& operator=(const ProjectGpuServices&) = delete;
    ProjectGpuServices(ProjectGpuServices&&) = delete;
    ProjectGpuServices& operator=(ProjectGpuServices&&) = delete;

    [[nodiscard]] ProjectGpuUploadDrainResult
    enqueue_pending_uploads(ProjectGpuUploadHandler handler);
    [[nodiscard]] vulkan::GpuWorkTicket enqueue(vulkan::GpuWorkRequest request);
    [[nodiscard]] vulkan::GpuWorkTicket submit_and_wait(vulkan::GpuWorkRequest request);
    [[nodiscard]] vulkan::Buffer upload_device_buffer(const void* data, VkDeviceSize byte_size,
                                                      VkBufferUsageFlags usage, std::string label);
    [[nodiscard]] vulkan::DeviceBufferUploadBatch
    upload_device_buffers(std::span<const vulkan::DeviceBufferUpload> uploads, std::string label);
    template <typename Value>
    [[nodiscard]] vulkan::Buffer upload_device_buffer(std::span<const Value> values,
                                                      VkBufferUsageFlags usage, std::string label) {
        return upload_device_buffer(values.data(), static_cast<VkDeviceSize>(values.size_bytes()),
                                    usage, std::move(label));
    }
    void wait_queue_idle(std::string label);
    [[nodiscard]] vulkan::GpuDrainResult drain();
    [[nodiscard]] ProjectGpuReadbackTicket
    enqueue_rgba8_image_readback(VkImage source, VkExtent2D extent, std::string label);
    [[nodiscard]] std::vector<std::uint8_t> readback_buffer(VkBuffer source, VkDeviceSize byte_size,
                                                            std::string label);
    [[nodiscard]] ProjectGpuReadbackStatus
    readback_status(const ProjectGpuReadbackTicket& ticket) const;
    [[nodiscard]] ProjectGpuReadbackResult
    take_completed_readback(const ProjectGpuReadbackTicket& ticket);

  private:
    struct Impl;

    void mark_readback_completed(const ProjectGpuReadbackTicket& ticket, std::uint32_t width,
                                 std::uint32_t height, std::vector<std::uint8_t> rgba8,
                                 vulkan::GpuSubmissionTicket completed_submission);
    void mark_readback_failed(const ProjectGpuReadbackTicket& ticket, std::string error);

    std::unique_ptr<Impl> impl_;
};

} // namespace cubey
