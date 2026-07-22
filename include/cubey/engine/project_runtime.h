#pragma once

#include <cubey/core/frame_clock.h>
#include <cubey/core/jobs.h>
#include <cubey/engine/capture_queue.h>
#include <cubey/engine/upload_queue.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cubey {

class ProjectGpuServices;
namespace vulkan {
class GpuRuntime;
} // namespace vulkan

struct ProjectExtent {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct ProjectFrame {
    double delta_seconds = 0.0;
    double elapsed_seconds = 0.0;
    std::uint64_t frame_index = 0;
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
                   ProjectGpuServices* gpu = nullptr);

    ProjectContext(const ProjectContext&) = delete;
    ProjectContext& operator=(const ProjectContext&) = delete;
    ProjectContext(ProjectContext&&) = delete;
    ProjectContext& operator=(ProjectContext&&) = delete;

    [[nodiscard]] jobs::JobSystem& jobs() const;
    [[nodiscard]] UploadQueue& upload_queue() const;
    [[nodiscard]] CaptureQueue& capture_queue() const;
    [[nodiscard]] bool has_gpu() const noexcept;
    [[nodiscard]] ProjectGpuServices& gpu() const;

  private:
    jobs::JobSystem* jobs_;
    UploadQueue* uploads_;
    CaptureQueue* captures_;
    ProjectGpuServices* gpu_;
};

class ProjectRuntimeServices {
  public:
    explicit ProjectRuntimeServices(std::size_t worker_count = 0);

    ProjectRuntimeServices(const ProjectRuntimeServices&) = delete;
    ProjectRuntimeServices& operator=(const ProjectRuntimeServices&) = delete;
    ProjectRuntimeServices(ProjectRuntimeServices&&) = delete;
    ProjectRuntimeServices& operator=(ProjectRuntimeServices&&) = delete;

    [[nodiscard]] ProjectContext context(ProjectGpuServices* gpu = nullptr);
    [[nodiscard]] ProjectFrame begin_frame(const FrameTiming& timing);

  private:
    jobs::JobSystem jobs_;
    UploadQueue uploads_;
    CaptureQueue captures_;
};

class ProjectRuntimeAdapter {
  public:
    explicit ProjectRuntimeAdapter(std::size_t worker_count = 0);
    ~ProjectRuntimeAdapter();

    ProjectRuntimeAdapter(const ProjectRuntimeAdapter&) = delete;
    ProjectRuntimeAdapter& operator=(const ProjectRuntimeAdapter&) = delete;
    ProjectRuntimeAdapter(ProjectRuntimeAdapter&&) = delete;
    ProjectRuntimeAdapter& operator=(ProjectRuntimeAdapter&&) = delete;

    [[nodiscard]] ProjectContext context();
    [[nodiscard]] const ProjectFrame& frame_for_timing(const FrameTiming& timing);
    void attach_gpu(vulkan::GpuRuntime& gpu);
    void attach_gpu_if_needed(vulkan::GpuRuntime& gpu);
    void detach_gpu();
    void detach_gpu_if_attached();
    [[nodiscard]] bool has_gpu() const noexcept;
    [[nodiscard]] ProjectGpuServices& gpu() const;

  private:
    ProjectRuntimeServices services_;
    std::unique_ptr<ProjectGpuServices> gpu_services_;
    ProjectFrame active_frame_;
    bool has_active_frame_ = false;
};

template <typename T>
concept ProjectLike =
    requires(T project, ProjectContext& context, ProjectFrame frame, ProjectExtent extent) {
        { project.setup(context) } -> std::same_as<void>;
        { project.update(frame, context) } -> std::same_as<void>;
        { project.render_packet(frame, context) } -> std::same_as<RenderPacket>;
        { project.resize(extent, context) } -> std::same_as<void>;
        { project.shutdown(context) } -> std::same_as<void>;
    };

} // namespace cubey
