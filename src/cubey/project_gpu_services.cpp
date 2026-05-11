#include <cubey/project_gpu_services.h>

#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/image.h>

#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
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

ProjectGpuServices::ProjectGpuServices(vulkan::GpuRuntime& gpu, UploadQueue& uploads,
                                       DeferredDestructionQueue& deferred_destruction)
    : gpu_(&gpu), uploads_(&uploads), deferred_destruction_(&deferred_destruction) {}

ProjectGpuUploadDrainResult
ProjectGpuServices::enqueue_pending_uploads(ProjectGpuUploadHandler handler) {
    if (!handler) {
        throw std::runtime_error("project GPU upload drain requires a handler");
    }

    std::vector<QueuedUpload> pending_uploads = uploads_->drain();
    ProjectGpuUploadDrainResult result{
        .upload_count = pending_uploads.size(),
        .work_tickets = {},
    };
    result.work_tickets.reserve(pending_uploads.size());

    for (QueuedUpload& upload : pending_uploads) {
        UploadTicket upload_ticket = upload.ticket;
        result.work_tickets.push_back(gpu_->enqueue({
            .label = upload_ticket.label,
            .work =
                [this, handler, upload = std::move(upload),
                 upload_ticket](vulkan::GpuOwnerContext& owner) mutable {
                    try {
                        handler(upload, owner);
                        uploads_->mark_completed(upload_ticket, owner.completed_submission());
                    } catch (const std::exception& error) {
                        uploads_->mark_failed(upload_ticket, error.what());
                        throw;
                    } catch (...) {
                        uploads_->mark_failed(upload_ticket, "unknown GPU upload failure");
                        throw;
                    }
                },
        }));
    }

    return result;
}

vulkan::GpuWorkTicket ProjectGpuServices::enqueue(vulkan::GpuWorkRequest request) {
    return gpu_->enqueue(std::move(request));
}

vulkan::GpuWorkTicket ProjectGpuServices::submit_and_wait(vulkan::GpuWorkRequest request) {
    return gpu_->submit_and_wait(std::move(request));
}

void ProjectGpuServices::wait_queue_idle(std::string label) {
    gpu_->wait_queue_idle(std::move(label));
}

vulkan::GpuDrainResult ProjectGpuServices::drain() {
    return gpu_->drain();
}

std::size_t ProjectGpuServices::retire_deferred_destruction() {
    const vulkan::GpuDrainResult result = gpu_->drain();
    return deferred_destruction_->retire_completed(result.completed_submission);
}

ProjectGpuReadbackTicket ProjectGpuServices::enqueue_rgba8_image_readback(VkImage source,
                                                                          VkExtent2D extent,
                                                                          std::string label) {
    const std::size_t byte_count = rgba8_readback_byte_count(extent);
    ProjectGpuReadbackTicket ticket;
    {
        std::scoped_lock lock(readback_mutex_);
        ticket = {
            .id = next_readback_id_,
            .label = std::move(label),
            .byte_count = byte_count,
        };
        ++next_readback_id_;
        readback_statuses_.emplace(ticket.id, ProjectGpuReadbackStatus{
                                                  .state = ProjectGpuReadbackState::Pending,
                                                  .completed_submission = {},
                                                  .byte_count = byte_count,
                                                  .error = {},
                                              });
    }

    try {
        static_cast<void>(gpu_->enqueue({
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

ProjectGpuReadbackStatus
ProjectGpuServices::readback_status(const ProjectGpuReadbackTicket& ticket) const {
    std::scoped_lock lock(readback_mutex_);
    auto found = readback_statuses_.find(ticket.id);
    if (found == readback_statuses_.end()) {
        throw std::runtime_error("unknown project GPU readback ticket");
    }
    return found->second;
}

ProjectGpuReadbackResult
ProjectGpuServices::take_completed_readback(const ProjectGpuReadbackTicket& ticket) {
    std::scoped_lock lock(readback_mutex_);
    auto status = readback_statuses_.find(ticket.id);
    if (status == readback_statuses_.end()) {
        throw std::runtime_error("unknown project GPU readback ticket");
    }
    if (status->second.state == ProjectGpuReadbackState::Pending) {
        throw std::runtime_error("project GPU readback is still pending");
    }
    if (status->second.state == ProjectGpuReadbackState::Failed) {
        throw std::runtime_error("project GPU readback failed: " + status->second.error);
    }

    auto result = readback_results_.find(ticket.id);
    if (result == readback_results_.end()) {
        throw std::runtime_error("project GPU readback result was already taken");
    }

    ProjectGpuReadbackResult completed = std::move(result->second);
    readback_results_.erase(result);
    return completed;
}

void ProjectGpuServices::mark_readback_completed(const ProjectGpuReadbackTicket& ticket,
                                                 std::uint32_t width, std::uint32_t height,
                                                 std::vector<std::uint8_t> rgba8,
                                                 FrameTicket completed_submission) {
    std::scoped_lock lock(readback_mutex_);
    auto status = readback_statuses_.find(ticket.id);
    if (status == readback_statuses_.end()) {
        throw std::runtime_error("unknown project GPU readback ticket");
    }

    status->second = {
        .state = ProjectGpuReadbackState::Completed,
        .completed_submission = completed_submission,
        .byte_count = rgba8.size(),
        .error = {},
    };
    readback_results_[ticket.id] = {
        .ticket = ticket,
        .width = width,
        .height = height,
        .rgba8 = std::move(rgba8),
    };
}

void ProjectGpuServices::mark_readback_failed(const ProjectGpuReadbackTicket& ticket,
                                              std::string error) {
    std::scoped_lock lock(readback_mutex_);
    auto status = readback_statuses_.find(ticket.id);
    if (status == readback_statuses_.end()) {
        return;
    }
    status->second.state = ProjectGpuReadbackState::Failed;
    status->second.error = std::move(error);
    readback_results_.erase(ticket.id);
}

} // namespace cubey
