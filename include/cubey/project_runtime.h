#pragma once

#include <cubey/capture_queue.h>
#include <cubey/frame_tickets.h>
#include <cubey/jobs.h>
#include <cubey/upload_queue.h>

#include <concepts>
#include <cstdint>
#include <string>
#include <vector>

namespace cubey {

struct ProjectExtent {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct ProjectFrame {
    double delta_seconds = 0.0;
    std::uint64_t frame_index = 0;
    FrameTicket ticket;
};

struct RenderPacket {
    std::string label;
    std::vector<UploadTicket> upload_tickets;
    std::size_t draw_count = 0;
    std::size_t dispatch_count = 0;
};

class ProjectContext {
public:
    ProjectContext(jobs::JobSystem& jobs, UploadQueue& uploads, CaptureQueue& captures,
                   FrameTicketIssuer& frame_tickets,
                   DeferredDestructionQueue& deferred_destruction);

    ProjectContext(const ProjectContext&) = delete;
    ProjectContext& operator=(const ProjectContext&) = delete;
    ProjectContext(ProjectContext&&) = delete;
    ProjectContext& operator=(ProjectContext&&) = delete;

    [[nodiscard]] jobs::JobSystem& jobs() const;
    [[nodiscard]] UploadQueue& upload_queue() const;
    [[nodiscard]] CaptureQueue& capture_queue() const;
    [[nodiscard]] FrameTicketIssuer& frame_tickets() const;
    [[nodiscard]] DeferredDestructionQueue& deferred_destruction() const;

private:
    jobs::JobSystem* jobs_;
    UploadQueue* uploads_;
    CaptureQueue* captures_;
    FrameTicketIssuer* frame_tickets_;
    DeferredDestructionQueue* deferred_destruction_;
};

template <typename T>
concept ProjectLike = requires(T project, ProjectContext& context, ProjectFrame frame,
                               ProjectExtent extent) {
    { project.setup(context) } -> std::same_as<void>;
    { project.update(frame, context) } -> std::same_as<void>;
    { project.render_packet(frame, context) } -> std::same_as<RenderPacket>;
    { project.resize(extent, context) } -> std::same_as<void>;
    { project.shutdown(context) } -> std::same_as<void>;
};

} // namespace cubey
