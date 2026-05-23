#include <cubey/engine/project_gpu_services.h>

#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/image.h>

#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cubey {
namespace {

constexpr std::size_t kRgba8BytesPerPixel = 4;

std::size_t rgba8_readback_byte_count(VkExtent2D extent) {
    if (extent.width == 0 || extent.height == 0) {
        throw std::runtime_error("project GPU readback extent must be positive");
    }

    const std::size_t checked_width = static_cast<std::size_t>(extent.width);
    const std::size_t checked_height = static_cast<std::size_t>(extent.height);
    if (checked_width > std::numeric_limits<std::size_t>::max() / checked_height) {
        throw std::runtime_error("project GPU readback is too large");
    }

    const std::size_t pixel_count = checked_width * checked_height;
    if (pixel_count > std::numeric_limits<std::size_t>::max() / kRgba8BytesPerPixel) {
        throw std::runtime_error("project GPU readback is too large");
    }
    return pixel_count * kRgba8BytesPerPixel;
}

} // namespace

struct ProjectGpuServices::Impl {
    Impl(vulkan::GpuRuntime& gpu_runtime, UploadQueue& upload_queue,
         vulkan::DeferredGpuDestructionQueue& deferred_queue)
        : gpu(&gpu_runtime), uploads(&upload_queue), deferred_destruction(&deferred_queue) {}

    vulkan::GpuRuntime* gpu = nullptr;
    UploadQueue* uploads = nullptr;
    vulkan::DeferredGpuDestructionQueue* deferred_destruction = nullptr;
    mutable std::mutex readback_mutex;
    std::uint64_t next_readback_id = 1;
    std::unordered_map<std::uint64_t, ProjectGpuReadbackStatus> readback_statuses;
    std::unordered_map<std::uint64_t, ProjectGpuReadbackResult> readback_results;
};

ProjectGpuServices::ProjectGpuServices(vulkan::GpuRuntime& gpu, UploadQueue& uploads,
                                       vulkan::DeferredGpuDestructionQueue& deferred_destruction)
    : impl_(std::make_unique<Impl>(gpu, uploads, deferred_destruction)) {}

ProjectGpuServices::~ProjectGpuServices() = default;

ProjectGpuUploadDrainResult
ProjectGpuServices::enqueue_pending_uploads(ProjectGpuUploadHandler handler) {
    if (!handler) {
        throw std::runtime_error("project GPU upload drain requires a handler");
    }

    std::vector<QueuedUpload> pending_uploads = impl_->uploads->drain();
    ProjectGpuUploadDrainResult result{
        .upload_count = pending_uploads.size(),
        .work_tickets = {},
    };
    result.work_tickets.reserve(pending_uploads.size());

    for (QueuedUpload& upload : pending_uploads) {
        UploadTicket upload_ticket = upload.ticket;
        result.work_tickets.push_back(impl_->gpu->enqueue({
            .label = upload_ticket.label,
            .work =
                [this, handler, upload = std::move(upload),
                 upload_ticket](vulkan::GpuOwnerContext& owner) mutable {
                    try {
                        handler(upload, owner);
                        impl_->uploads->mark_completed(upload_ticket, owner.completed_submission());
                    } catch (const std::exception& error) {
                        impl_->uploads->mark_failed(upload_ticket, error.what());
                        throw;
                    } catch (...) {
                        impl_->uploads->mark_failed(upload_ticket, "unknown GPU upload failure");
                        throw;
                    }
                },
        }));
    }

    return result;
}

vulkan::GpuWorkTicket ProjectGpuServices::enqueue(vulkan::GpuWorkRequest request) {
    return impl_->gpu->enqueue(std::move(request));
}

vulkan::GpuWorkTicket ProjectGpuServices::submit_and_wait(vulkan::GpuWorkRequest request) {
    return impl_->gpu->submit_and_wait(std::move(request));
}

void ProjectGpuServices::wait_queue_idle(std::string label) {
    impl_->gpu->wait_queue_idle(std::move(label));
}

vulkan::GpuDrainResult ProjectGpuServices::drain() {
    return impl_->gpu->drain();
}

std::size_t ProjectGpuServices::retire_deferred_destruction() {
    const vulkan::GpuDrainResult result = impl_->gpu->drain();
    return impl_->deferred_destruction->retire_completed(result.completed_submission);
}

ProjectGpuReadbackTicket ProjectGpuServices::enqueue_rgba8_image_readback(VkImage source,
                                                                          VkExtent2D extent,
                                                                          std::string label) {
    const std::size_t byte_count = rgba8_readback_byte_count(extent);
    ProjectGpuReadbackTicket ticket;
    {
        std::scoped_lock lock(impl_->readback_mutex);
        ticket = {
            .id = impl_->next_readback_id,
            .label = std::move(label),
            .byte_count = byte_count,
        };
        ++impl_->next_readback_id;
        impl_->readback_statuses.emplace(ticket.id, ProjectGpuReadbackStatus{
                                                        .state = ProjectGpuReadbackState::Pending,
                                                        .completed_submission = {},
                                                        .byte_count = byte_count,
                                                        .error = {},
                                                    });
    }

    try {
        static_cast<void>(impl_->gpu->enqueue({
            .label = ticket.label,
            .work =
                [this, ticket, source, extent](vulkan::GpuOwnerContext& owner) {
                    try {
                        if (source == VK_NULL_HANDLE) {
                            throw std::runtime_error(
                                "project GPU readback requires a source image");
                        }

                        const VkDeviceSize readback_byte_size =
                            static_cast<VkDeviceSize>(ticket.byte_count);
                        vulkan::Buffer readback(owner.device(),
                                                vulkan::readback_buffer_config(readback_byte_size));
                        vulkan::copy_image_to_buffer(owner, source, readback.handle(),
                                                     {extent.width, extent.height, 1});

                        std::vector<std::uint8_t> rgba8(ticket.byte_count);
                        readback.download(rgba8.data(), readback_byte_size);
                        mark_readback_completed(ticket, extent.width, extent.height,
                                                std::move(rgba8), owner.completed_submission());
                    } catch (const std::exception& error) {
                        mark_readback_failed(ticket, error.what());
                        throw;
                    } catch (...) {
                        mark_readback_failed(ticket, "unknown GPU readback failure");
                        throw;
                    }
                },
        }));
    } catch (const std::exception& error) {
        mark_readback_failed(ticket, error.what());
        throw;
    } catch (...) {
        mark_readback_failed(ticket, "unknown GPU readback enqueue failure");
        throw;
    }

    return ticket;
}

std::vector<std::uint8_t>
ProjectGpuServices::readback_buffer(VkBuffer source, VkDeviceSize byte_size, std::string label) {
    if (source == VK_NULL_HANDLE) {
        throw std::runtime_error("project GPU buffer readback requires a source buffer");
    }
    if (byte_size == 0) {
        throw std::runtime_error("project GPU buffer readback size must be positive");
    }
    if (byte_size > static_cast<VkDeviceSize>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("project GPU buffer readback is too large");
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(byte_size));
    static_cast<void>(impl_->gpu->submit_and_wait({
        .label = std::move(label),
        .work =
            [source, byte_size, &bytes](vulkan::GpuOwnerContext& owner) {
                vulkan::Buffer readback(owner.device(), vulkan::readback_buffer_config(byte_size));
                vulkan::copy_buffer(owner, source, readback.handle(), byte_size);
                readback.download(bytes.data(), byte_size);
            },
    }));
    return bytes;
}

ProjectGpuReadbackStatus
ProjectGpuServices::readback_status(const ProjectGpuReadbackTicket& ticket) const {
    std::scoped_lock lock(impl_->readback_mutex);
    auto found = impl_->readback_statuses.find(ticket.id);
    if (found == impl_->readback_statuses.end()) {
        throw std::runtime_error("unknown project GPU readback ticket");
    }
    return found->second;
}

ProjectGpuReadbackResult
ProjectGpuServices::take_completed_readback(const ProjectGpuReadbackTicket& ticket) {
    std::scoped_lock lock(impl_->readback_mutex);
    auto status = impl_->readback_statuses.find(ticket.id);
    if (status == impl_->readback_statuses.end()) {
        throw std::runtime_error("unknown project GPU readback ticket");
    }
    if (status->second.state == ProjectGpuReadbackState::Pending) {
        throw std::runtime_error("project GPU readback is still pending");
    }
    if (status->second.state == ProjectGpuReadbackState::Failed) {
        throw std::runtime_error("project GPU readback failed: " + status->second.error);
    }

    auto result = impl_->readback_results.find(ticket.id);
    if (result == impl_->readback_results.end()) {
        throw std::runtime_error("project GPU readback result was already taken");
    }

    ProjectGpuReadbackResult completed = std::move(result->second);
    impl_->readback_results.erase(result);
    return completed;
}

void ProjectGpuServices::mark_readback_completed(const ProjectGpuReadbackTicket& ticket,
                                                 std::uint32_t width, std::uint32_t height,
                                                 std::vector<std::uint8_t> rgba8,
                                                 vulkan::GpuSubmissionTicket completed_submission) {
    std::scoped_lock lock(impl_->readback_mutex);
    auto status = impl_->readback_statuses.find(ticket.id);
    if (status == impl_->readback_statuses.end()) {
        throw std::runtime_error("unknown project GPU readback ticket");
    }

    status->second = {
        .state = ProjectGpuReadbackState::Completed,
        .completed_submission = completed_submission,
        .byte_count = rgba8.size(),
        .error = {},
    };
    impl_->readback_results[ticket.id] = {
        .ticket = ticket,
        .width = width,
        .height = height,
        .rgba8 = std::move(rgba8),
    };
}

void ProjectGpuServices::mark_readback_failed(const ProjectGpuReadbackTicket& ticket,
                                              std::string error) {
    std::scoped_lock lock(impl_->readback_mutex);
    auto status = impl_->readback_statuses.find(ticket.id);
    if (status == impl_->readback_statuses.end()) {
        return;
    }
    status->second.state = ProjectGpuReadbackState::Failed;
    status->second.error = std::move(error);
    impl_->readback_results.erase(ticket.id);
}

} // namespace cubey
